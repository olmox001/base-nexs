# =============================================================================
# NEXS — Top-Level Makefile
# =============================================================================
.DEFAULT_GOAL := all
# Modular layout: core/ registry/ lang/ sys/ runtime/ compiler/ hal/
# Legacy code: old/src/ (not compiled)
# =============================================================================

CC     = gcc
TARGET = nxscript

# Platform Detection
UNAME_S := $(shell uname -s)
ZIG := $(shell command -v zig 2> /dev/null)

ifeq ($(UNAME_S),Darwin)
    HOST_PLATFORM = MACOS
    # Fallback to zig cc if x86_64-linux-gnu-gcc is missing
    LINUX_AMD64_CC = $(shell command -v x86_64-linux-gnu-gcc 2>/dev/null || ( [ -n "$(ZIG)" ] && echo "zig cc -target x86_64-linux-gnu" ) || echo "x86_64-linux-gnu-gcc")
    MACOS_AMD64_CC = gcc
else
    HOST_PLATFORM = LINUX
    LINUX_AMD64_CC = gcc
    MACOS_AMD64_CC = x86_64-apple-darwin20.2-clang # Typical for osxcross
endif

# Include paths
INCS = \
  -Icore/include \
  -Iregistry/include \
  -Iauthority/include \
  -Ihardware/include \
  -Ibuffer/include \
  -Iblock/include \
  -Ijournal/include \
  -Ifs/include \
  -Ilang/include \
  -Isys/include \
  -Iruntime/include \
  -Icompiler/include \
  -Ihal/include

# Production flags & Memory Pool Profiles (4KB, 16KB, 32KB, 512KB, 4MB, 16MB, 64MB)
POOL_PROFILE ?= 64MB
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
         -DPOOL_$(POOL_PROFILE) -DHOST_OS_$(HOST_PLATFORM) -DNEXS_HOST_TOOL \
         -DLINUX_AMD64_CC_BIN='"$(LINUX_AMD64_CC)"' \
         -DMACOS_AMD64_CC_BIN='"$(MACOS_AMD64_CC)"' \
         $(INCS)

# Debug flags
DBGFLAGS = -O0 -g -std=c11 -Wall -Wextra -Wno-unused-parameter \
            -fsanitize=address,undefined -fno-omit-frame-pointer \
            -DNEXS_HOST_TOOL -DHOST_OS_$(HOST_PLATFORM) \
            -DPOOL_$(POOL_PROFILE) $(INCS)

# All runtime source files (no src/)


# Baremetal doesn't use the hosted HAL, but it keeps the AOT compiler


include config/sources.mk

OBJS = $(SRCS:.c=.o)

# All header files (for dependency tracking)
HDRS = \
  core/include/nexs_common.h \
  core/include/nexs_crypto.h \
  core/include/nexs_alloc.h \
  core/include/nexs_value.h \
  core/include/nexs_utils.h \
  authority/include/nexs_capability.h \
  hardware/include/nexs_hardware_capability.h \
  buffer/include/nexs_buffer.h \
  buffer/include/nexs_buffer_capability_adapter.h \
  block/include/nexs_block_device.h \
  block/include/nexs_block_memory.h \
  block/include/nexs_block_memory_device.h \
  journal/include/nexs_journal_wal.h \
  fs/include/nexs_p9_frame.h \
  registry/include/nexs_state_store.h \
  registry/include/nexs_nxreg.h \
  registry/include/nexs_snapshot_pool.h \
  registry/include/nexs_schema.h \
  registry/include/nexs_migration.h \
  registry/include/nexs_registry.h \
  lang/include/nexs_lex.h \
  lang/include/nexs_ast.h \
  lang/include/nexs_fn.h \
  lang/include/nexs_parse.h \
  lang/include/nexs_eval.h \
  sys/include/nexs_sys.h \
  runtime/include/nexs_runtime.h \
  runtime/include/nexs_budget.h \
  runtime/include/nexs_cycle_counter.h \
  runtime/include/nexs_clock.h \
  runtime/include/nexs_operation_metrics.h \
  runtime/include/nexs_operation_catalog.h \
  runtime/include/nexs_operation_ids.h \
  runtime/include/nexs_line.h \
  compiler/include/nexs_compiler.h \
  compiler/targets.h \
  hal/include/nexs_hal.h \
  hal/include/nexs_hal_bc.h \
  include/nexs.h

# Set EMBED_EXAMPLES=1 to also bundle example/ scripts into the seL4 binary
EMBED_EXAMPLES ?= 0

.PHONY: gate quality-check check
gate:
	@python3 scripts/quality_gate.py
	@$(CC) $(CFLAGS) -fsyntax-only tests/test_buddy_geometry_build.c

# Quality and release blockers must NOT waive strict Power12 diagnostics.
# Plain all remains an explicitly UNQUALIFIED development build.
quality-check: power12-audit-all

# Full-source *baseline* assurance gate. Unlike the partial compilation gate,
# this must fail until both Base-NEXS and the integrated NXOLMO source are clean.
# It is not a claim of complete AST/CFG Power12 enforcement (see the standard).
.PHONY: power12-strict power12-inventory power12-ast power12-debt power12-phase0
power12-strict:
	@set +e; \
	python3 scripts/quality_gate.py --mode strict-all --report build/assurance/rearchitecture-strict-all.json; base_rc=$$?; \
	if [ -f external/references/nxolmo/Makefile ]; then \
	  $(MAKE) -C external/references/nxolmo check-power-of-ten; nx_rc=$$?; \
	else \
	  echo '[power12] NXOLMO reference is not initialized'; nx_rc=2; \
	fi; \
	if [ $$base_rc -ne 0 ] || [ $$nx_rc -ne 0 ]; then \
	  echo "[power12] FAIL: Base-NEXS status=$$base_rc; NXOLMO status=$$nx_rc"; exit 1; \
	fi; \
	echo '[power12] Current strict source baselines PASS (full project profile still pending AST/CFG integration)'

# Both lexical/AST reference and compiler-conversion diagnostics must pass.
.PHONY: power12-compiler-strict power12-audit-all
power12-compiler-strict:
	@python3 scripts/power12_compiler_diagnostics.py

power12-audit-all:
	@set +e; \
	$(MAKE) power12-strict; source_rc=$$?; \
	$(MAKE) power12-compiler-strict; compiler_rc=$$?; \
	if [ $$source_rc -ne 0 ] || [ $$compiler_rc -ne 0 ]; then \
	  echo "[power12] QUALIFICATION BLOCKED: source=$$source_rc compiler=$$compiler_rc"; \
	  exit 1; \
	fi

power12-inventory:
	@python3 scripts/rearchitecture_inventory.py

# P0 compiler evidence: fail if any selected target TU cannot be parsed.
# External references, feature-disabled code and missing Microkit targets
# remain explicitly UNQUALIFIED, never implicitly counted as passing.
power12-ast: power12-inventory
	@python3 scripts/rearchitecture_clang_ast.py

# P0 records all existing strict blockers but grants no release exemptions.
power12-debt: power12-inventory
	@python3 scripts/rearchitecture_debt.py

# Every tracked NXOLMO/NXEC source and public header must have an explicit
# preserved/ported/deferred decision, never an invented port status.
.PHONY: power12-crosscheck
power12-crosscheck:
	@python3 scripts/rearchitecture_crosscheck.py

power12-phase0: power12-ast power12-debt power12-ownership power12-crosscheck
	@python3 -m unittest discover -s tests -p 'test_rearchitecture*.py' -v

# Every source-level definition has exactly one canonical target role,
# but no reference symbol is declared implemented without tested evidence.
.PHONY: power12-ownership
power12-ownership: power12-inventory
	@python3 scripts/rearchitecture_ownership.py

check: power12-audit-all
	@$(MAKE) test

.PHONY: all clean test debug run compile-test baremetal-amd64 baremetal-arm64 iso-amd64 run-iso sel4-microkit gen-embed \
        baremetal-riscv64 sel4-aarch64 sel4-riscv64 sel4-x86_64 \
        sel4-run-aarch64 sel4-run-riscv64 sel4-run-x86_64 \
        sel4-example-aarch64 sel4-example-riscv64 sel4-example-x86_64 \
        sel4-initializer \
        run-nexs-aarch64 run-nexs-riscv64 run-nexs-x86_64 run-nexs-amd64 \
        sel4-multikernel sel4-multikernel-aarch64 sel4-multikernel-riscv64 \
        sel4-multikernel-x86_64 sel4-multikernel-amd64

# ─────────────────────────────────────────────────────────────────────────────
# Default target: hosted interpreter
# ─────────────────────────────────────────────────────────────────────────────
all: $(TARGET)

$(OBJS): | gate

$(TARGET): $(OBJS) | gate
	$(CC) $(CFLAGS) -o $@ $(OBJS)
	@echo "Build OK -> ./$(TARGET)"

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ─────────────────────────────────────────────────────────────────────────────
# Native NexsOS1 userland via the existing NX-SDK object/capability ABI.
# Unlike the bare-metal targets these are ordinary user processes, not kernels.
# ─────────────────────────────────────────────────────────────────────────────
ifneq ($(filter os1-amd64 os1-arm64 os1-build os1-check os1-aot-amd64 os1-aot-arm64 os1-aot-build,$(MAKECMDGOALS)),)
OS1_SDK_ROOT ?= $(shell ./scripts/resolve-sdk.sh --path)
OS1_SDK_BUILD_ROOT ?= $(abspath build/sdk-os1)
OS1_ARCH ?= amd64
OS1_SDK_ARCH := $(if $(filter amd64,$(OS1_ARCH)),amd64,aarch64)
OS1_BUILD_DIR := build/os1-$(OS1_ARCH)
OS1_CC := $(if $(filter amd64,$(OS1_ARCH)),x86_64-elf-gcc,aarch64-none-elf-gcc)
OS1_CPU_FLAGS := $(if $(filter amd64,$(OS1_ARCH)),-DARCH_AMD64 -mno-red-zone -mcmodel=large,-DARCH_AARCH64 -mcpu=cortex-a57)
OS1_API_INCLUDES := -I$(OS1_SDK_ROOT)/include/api -I$(OS1_SDK_ROOT)/include/contract \
                    -I$(OS1_SDK_ROOT)/lib/extensions/USERKIT/nxntfy_srv/include
OS1_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
               -ffreestanding -fno-builtin -nostdlib -nostartfiles \
               -fno-pic -fno-pie -fno-omit-frame-pointer -fstack-protector-strong -Wa,--noexecstack \
               -ffunction-sections -fdata-sections -DPOOL_$(POOL_PROFILE) \
               -DNEXS_HOST_TOOL -DNEXS_OS1 $(OS1_CPU_FLAGS) \
               $(OS1_API_INCLUDES) $(INCS)
OS1_SOURCES := $(NEXS_OS1_SRCS)
OS1_OBJECTS := $(addprefix $(OS1_BUILD_DIR)/,$(OS1_SOURCES:.c=.o))
OS1_SDK_CRT := $(OS1_SDK_BUILD_ROOT)/$(OS1_SDK_ARCH)/system/arch/$(OS1_SDK_ARCH)/syscall.o
OS1_SDK_ARCHIVE := $(OS1_SDK_BUILD_ROOT)/$(OS1_SDK_ARCH)/libnxsdk-core.a
OS1_ELF := $(OS1_BUILD_DIR)/nxscript.elf

.PHONY: os1-amd64 os1-arm64 os1-build
os1-amd64:
	@$(MAKE) OS1_ARCH=amd64 os1-build

os1-arm64:
	@$(MAKE) OS1_ARCH=arm64 os1-build

os1-build: gate $(OS1_ELF)
	@echo "NexsOS1 $(OS1_ARCH) userland -> $(OS1_ELF)"

$(OS1_SDK_CRT) $(OS1_SDK_ARCHIVE):
	+$(MAKE) -C "$(OS1_SDK_ROOT)" ARCH="$(OS1_SDK_ARCH)" \
	  BUILD_ROOT="$(OS1_SDK_BUILD_ROOT)" core-archive

$(OS1_BUILD_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(OS1_CC) $(OS1_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(OS1_OBJECTS): | gate

$(OS1_ELF): $(OS1_OBJECTS) $(OS1_SDK_CRT) $(OS1_SDK_ARCHIVE) | gate
	$(OS1_CC) $(OS1_CFLAGS) -Wl,--gc-sections \
	  -Wl,-Ttext=0x80000000 -e _start -o "$@" $(OS1_OBJECTS) \
	  "$(OS1_SDK_CRT)" -Wl,--start-group "$(OS1_SDK_ARCHIVE)" -lgcc \
	  -Wl,--end-group

# Compile a .nx source directly into an OS1-native ELF using precisely the
# same catalogued runtime objects, SDK CRT and capability-only HAL as nxscript.
# This target is a build adapter, not a second compiler implementation.
OS1_AOT_ENTRY_NX ?= tests/fixtures/baremetal_smoke.nx
OS1_AOT_OUTPUT ?= $(OS1_BUILD_DIR)/nxscript-program.elf
OS1_AOT_NO_DEP ?= 0
OS1_AOT_EMBED_C ?=
OS1_AOT_PROGRAM_C = $(if $(OS1_AOT_EMBED_C),$(OS1_AOT_EMBED_C),$(OS1_BUILD_DIR)/nxscript-program.c)
OS1_AOT_PROGRAM_O = $(if $(OS1_AOT_EMBED_C),$(basename $(OS1_AOT_EMBED_C)).o,$(OS1_BUILD_DIR)/nxscript-program.o)
OS1_AOT_DEP_FLAG := $(if $(filter 1,$(OS1_AOT_NO_DEP)),--no-dep,)

.PHONY: os1-aot-amd64 os1-aot-arm64 os1-aot-build
os1-aot-amd64:
	@$(MAKE) OS1_ARCH=amd64 os1-aot-build

os1-aot-arm64:
	@$(MAKE) OS1_ARCH=arm64 os1-aot-build

os1-aot-build: gate $(TARGET) $(OS1_OBJECTS) $(OS1_SDK_CRT) $(OS1_SDK_ARCHIVE)
	@mkdir -p "$(OS1_BUILD_DIR)" "$(dir $(OS1_AOT_OUTPUT))"
	@if [ -z "$(OS1_AOT_EMBED_C)" ]; then \
	  test -f "$(OS1_AOT_ENTRY_NX)" || exit 2; \
	  ./$(TARGET) --codegen "$(OS1_AOT_ENTRY_NX)" $(OS1_AOT_DEP_FLAG) --hosted -o "$(OS1_AOT_PROGRAM_C)"; \
	else \
	  test -f "$(OS1_AOT_EMBED_C)" || exit 2; \
	fi
	@$(OS1_CC) $(OS1_CFLAGS) -c "$(OS1_AOT_PROGRAM_C)" -o "$(OS1_AOT_PROGRAM_O)"
	@$(OS1_CC) $(OS1_CFLAGS) -Wl,--gc-sections -Wl,-Ttext=0x80000000 -e _start 	  -o "$(OS1_AOT_OUTPUT)" $(OS1_OBJECTS) "$(OS1_AOT_PROGRAM_O)" 	  "$(OS1_SDK_CRT)" -Wl,--start-group "$(OS1_SDK_ARCHIVE)" -lgcc -Wl,--end-group
	@echo "NexsOS1 $(OS1_ARCH) AOT -> $(OS1_AOT_OUTPUT)"

.PHONY: os1-check
os1-check: gate $(TARGET)
	@python3 scripts/test_os1_compiler.py
	@python3 scripts/test_os1_parallel.py

-include $(wildcard build/os1-$(OS1_ARCH)/**/*.d)
endif # OS1 native target requested; non-OS1 hosts need no NX-SDK.


# Installable NEXS language sources and assets, using ONE reviewed catalog.
# This does not package a runtime ELF or modify a running OS1 image.
NXLIB_STAGE_ROOT ?= build/assurance/stage-nxlib
.PHONY: nxlib-stage
nxlib-stage:
	@python3 scripts/stage_nxlib.py --root "$(NXLIB_STAGE_ROOT)"

# ─────────────────────────────────────────────────────────────────────────────
# Debug build with AddressSanitizer + UBSan
# ─────────────────────────────────────────────────────────────────────────────
debug: gate $(SRCS) $(HDRS)
	$(CC) $(DBGFLAGS) -o $(TARGET)_dbg $(SRCS)
	@echo "Debug build -> ./$(TARGET)_dbg"

# Alias
nxscript_dbg: debug

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────
.PHONY: check-syntax
check-syntax: $(TARGET)
	@python3 scripts/test_nx_syntax.py

.PHONY: test-array-capacity-core test-array-capacity-asan

# Link the real production array/value/buddy implementation, no mocks.
# Portable errno -> typed NXOLMO error catalog, exercised before any
# fallback source selection and checked under strict compiler warnings.
# Real guest, real libc stub, exact-copy and sentinel boundary regression.
# Real installed-source/registry namespace and service-activation acceptance.
.PHONY: test-source-manifest-unified
test-source-manifest-unified: $(TARGET)
	python3 -m unittest discover -s tests -p 'test_source_manifest_unified.py'

.PHONY: test-freestanding-strncpy-guest
test-freestanding-strncpy-guest:
	python3 tests/test_freestanding_strncpy_guest.py

.PHONY: test-source-status-core
test-source-status-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -Werror -Wpedantic -Wconversion -Wsign-conversion \
	  tests/test_source_status_core.c -o build/assurance/test_source_status_core
	@./build/assurance/test_source_status_core

test-array-capacity-core: core/buddy.o core/dynarray.o core/value.o core/utils.o core/checked.o
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_array_capacity_core.c \
	  core/buddy.o core/dynarray.o core/value.o core/utils.o core/checked.o \
	  -o build/assurance/test_array_capacity_core
	@./build/assurance/test_array_capacity_core

test-array-capacity-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) tests/test_array_capacity_core.c \
	  core/buddy.c core/dynarray.c core/value.c core/utils.c core/checked.c \
	  -o build/assurance/test_array_capacity_sanitized
	@./build/assurance/test_array_capacity_sanitized

.PHONY: test-checked-core

# All nine checked/status ABI functions are owned and linked by Base-NEXS.
# Test both host and sanitizers without importing NXOLMO objects/libraries.
test-checked-core: core/checked.o core/status.o
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_checked_contract.c \
	  core/checked.o core/status.o -o build/assurance/test_checked_contract
	@./build/assurance/test_checked_contract

.PHONY: test-foundation-core

test-foundation-core: core/checked.o core/status.o core/types.o core/result.o
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_foundation_abi.c \
	  core/checked.o core/status.o core/types.o core/result.o \
	  -o build/assurance/test_foundation_abi
	@./build/assurance/test_foundation_abi

test: $(TARGET) test-sysio-fd-owner test-hal-module-authority-boundary test-halbc-budget-authority test-source-status-core test-language-fuzz test-system-syscall-fuzz test-full-9p-vfs-runtime test-allocator-observation test-p9-request-conformance test-nxolmo-resource-conformance test-nxolmo-block-conformance test-nxolmo-wal-conformance test-nxolmo-state-conformance test-nxolmo-regkey-integration test-array-capacity-core test-checked-core test-foundation-core test-registry-integrity-core test-registry-fallible-oom test-registry-init-recovery test-ipc-array-wire test-value-checked-core test-ast-iterative-owner test-runtime-admission-budget test-kernel-cap-cspace test-kernel-cap-cspace-oom test-kernel-ipc-owner test-pager-ownership-core test-recoverable-alloc-core test-buddy-iteration-core test-buddy-arena-isolation-core test-buddy-arena-threaded-core test-crypto-canonical-core test-operation-runtime-canonical test-operation-catalog-canonical test-clock-provider-hosted
	@python3 -m unittest discover -s tests -p 'test_*.py' -v
	@python3 scripts/test_nx_syntax.py
	@python3 scripts/test_runtime.py --binary $(TARGET)

.PHONY: check-examples
check-examples: $(TARGET)
	@python3 scripts/test_runtime.py --binary $(TARGET) --all

# Hosted AOT uses the same canonical runtime source graph as the interpreter.
# Inputs are private generated C files, never user-supplied shell variables.
# Cross toolchains must be present; unsupported targets are rejected explicitly.
AOT_HOST_TARGET ?= macos-amd64
AOT_EMBED_C ?=
AOT_OUTPUT ?=
ifeq ($(AOT_HOST_TARGET),macos-amd64)
  AOT_HOST_CC := clang
  AOT_HOST_FLAGS := -arch x86_64 -DNEXS_MACOS -DHOST_OS_MACOS
else ifeq ($(AOT_HOST_TARGET),macos-arm64)
  AOT_HOST_CC := clang
  AOT_HOST_FLAGS := -arch arm64 -DNEXS_MACOS -DHOST_OS_MACOS
else ifeq ($(AOT_HOST_TARGET),linux-amd64)
  AOT_HOST_CC := $(LINUX_AMD64_CC)
  AOT_HOST_FLAGS := -march=x86-64 -DNEXS_LINUX -DHOST_OS_LINUX
else ifeq ($(AOT_HOST_TARGET),linux-arm64)
  AOT_HOST_CC := aarch64-linux-gnu-gcc
  AOT_HOST_FLAGS := -march=armv8-a -DNEXS_LINUX -DHOST_OS_LINUX
else
  AOT_HOST_CC :=
endif

.PHONY: aot-hosted
aot-hosted: gate
	@test -n "$(AOT_HOST_CC)" || { echo "Unsupported hosted AOT target" >&2; exit 2; }
	@test -f "$(AOT_EMBED_C)" || { echo "Missing generated AOT source" >&2; exit 2; }
	@test -n "$(AOT_OUTPUT)" || { echo "Missing staged AOT output path" >&2; exit 2; }
	@mkdir -p "$(dir $(AOT_OUTPUT))"
	@$(AOT_HOST_CC) $(AOT_HOST_FLAGS) -DNEXS_HOST_TOOL -DPOOL_$(POOL_PROFILE) \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	  $(INCS) $(NEXS_AOT_HOSTED_SRCS) "$(AOT_EMBED_C)" -o "$(AOT_OUTPUT)"
	@echo "NEXS hosted AOT $(AOT_HOST_TARGET) -> $(AOT_OUTPUT)"

# ─────────────────────────────────────────────────────────────────────────────
# Compile a NEXS script to a Linux binary
# ─────────────────────────────────────────────────────────────────────────────
compile-test: $(TARGET)
	@mkdir -p build/linux-amd64
	@test -f example/test.nx || { echo "Missing compile-test fixture" >&2; exit 2; }
	@./$(TARGET) --compile example/test.nx --target linux-amd64 -o build/linux-amd64/test

# ─────────────────────────────────────────────────────────────────────────────
# Cross-compile targets
# ─────────────────────────────────────────────────────────────────────────────
# Hosted platform matrix. A missing compiler or sysroot MUST fail, never pass
# by printing "skipped" or executing the success echo after a failed cc.
linux-arm64: $(TARGET)
	@command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || { echo "Missing Linux arm64 cross-compiler" >&2; exit 2; }
	@mkdir -p build/linux-arm64
	@aarch64-linux-gnu-gcc -march=armv8-a -DNEXS_LINUX -DHOST_OS_LINUX -DPOOL_$(POOL_PROFILE) -DNEXS_HOST_TOOL \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	  $(INCS) $(SRCS) -o build/linux-arm64/nxscript
	@echo "Linux arm64 -> build/linux-arm64/nxscript"

linux-amd64: $(TARGET)
	@command -v $(LINUX_AMD64_CC) >/dev/null 2>&1 || { echo "Missing Linux amd64 cross-compiler: $(LINUX_AMD64_CC)" >&2; exit 2; }
	@mkdir -p build/linux-amd64
	@$(LINUX_AMD64_CC) -march=x86-64 -DNEXS_LINUX -DHOST_OS_LINUX -DPOOL_$(POOL_PROFILE) -DNEXS_HOST_TOOL \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	  $(INCS) $(SRCS) -o build/linux-amd64/nxscript
	@echo "Linux amd64 -> build/linux-amd64/nxscript"

macos-amd64: $(TARGET)
	@command -v clang >/dev/null 2>&1 || { echo "Missing Apple clang" >&2; exit 2; }
	@mkdir -p build/macos-amd64
	@clang -arch x86_64 -DNEXS_MACOS -DHOST_OS_MACOS -DPOOL_$(POOL_PROFILE) -DNEXS_HOST_TOOL \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	  $(INCS) $(SRCS) -o build/macos-amd64/nxscript
	@echo "macOS amd64 -> build/macos-amd64/nxscript"

macos-arm64: $(TARGET)
	@command -v clang >/dev/null 2>&1 || { echo "Missing Apple clang" >&2; exit 2; }
	@mkdir -p build/macos-arm64
	@clang -arch arm64 -DNEXS_MACOS -DHOST_OS_MACOS -DPOOL_$(POOL_PROFILE) -DNEXS_HOST_TOOL \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	  $(INCS) $(SRCS) -o build/macos-arm64/nxscript
	@echo "macOS arm64 -> build/macos-arm64/nxscript"

# Kernel sources






BAREMETAL_ENTRY_NX ?= tests/fixtures/repl.nx
BAREMETAL_OUTPUT ?=
# Optional test-only sources; never included in production images.
BAREMETAL_TEST_SRCS ?=
BAREMETAL_EMBED_C ?=
BAREMETAL_ARM64_PROGRAM_C = $(if $(BAREMETAL_EMBED_C),$(BAREMETAL_EMBED_C),build/baremetal-arm64/nxscript_embed.c)
BAREMETAL_AMD64_PROGRAM_C = $(if $(BAREMETAL_EMBED_C),$(BAREMETAL_EMBED_C),build/baremetal-amd64/nxscript_embed.c)
# Include reviewed nxlib resources in standalone baremetal images by default.
BAREMETAL_EMBED_DEPS ?= 1
BAREMETAL_CODEGEN_FLAGS := $(if $(filter 1,$(BAREMETAL_EMBED_DEPS)),,--no-dep)

baremetal-arm64: $(TARGET)
	@command -v aarch64-none-elf-gcc >/dev/null 2>&1 || { echo "Required toolchain aarch64-none-elf-gcc missing" >&2; exit 2; }
	@mkdir -p build/baremetal-arm64
	@if [ -z "$(BAREMETAL_EMBED_C)" ]; then \
	  ./$(TARGET) --codegen "$(BAREMETAL_ENTRY_NX)" $(BAREMETAL_CODEGEN_FLAGS) -o "$(BAREMETAL_ARM64_PROGRAM_C)"; \
	else test -f "$(BAREMETAL_EMBED_C)" || exit 2; fi
	@aarch64-none-elf-gcc -march=armv8-a -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-builtin \
	  -fno-stack-protector -nostdlib -nostartfiles -ffreestanding \
	  -T hal/arm64/nexs.ld $(INCS) -Ikernel/include \
	  $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(ARM64_HAL_SRCS) $(BAREMETAL_TEST_SRCS) \
	  "$(BAREMETAL_ARM64_PROGRAM_C)" -lgcc \
	  -o "$(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-arm64/nxscript.elf)"
	@echo "Baremetal arm64: $(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-arm64/nxscript.elf)"

baremetal-amd64: $(TARGET)
	@command -v x86_64-elf-gcc >/dev/null 2>&1 || { echo "Required toolchain x86_64-elf-gcc missing" >&2; exit 2; }
	@mkdir -p build/baremetal-amd64
	@if [ -z "$(BAREMETAL_EMBED_C)" ]; then \
	  ./$(TARGET) --codegen "$(BAREMETAL_ENTRY_NX)" $(BAREMETAL_CODEGEN_FLAGS) -o "$(BAREMETAL_AMD64_PROGRAM_C)"; \
	else test -f "$(BAREMETAL_EMBED_C)" || exit 2; fi
	@x86_64-elf-gcc -march=x86-64 -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-builtin \
	  -fno-stack-protector -fno-pic -mno-red-zone \
	  -nostdlib -nostartfiles -ffreestanding \
	  -Wl,--no-warn-rwx-segments -T hal/amd64/nexs.ld $(INCS) -Ikernel/include \
	  $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(AMD64_HAL_SRCS) $(BAREMETAL_TEST_SRCS) \
	  "$(BAREMETAL_AMD64_PROGRAM_C)" -lgcc \
	  -o "$(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-amd64/nxscript.elf)"
	@echo "Baremetal amd64: $(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-amd64/nxscript.elf)"

sel4-microkit: $(TARGET)
	@if [ -z "$(MICROKIT_SDK)" ] || [ -z "$(MICROKIT_BOARD)" ] || [ -z "$(MICROKIT_CONFIG)" ]; then \
		echo "MICROKIT_SDK, MICROKIT_BOARD, and MICROKIT_CONFIG must be specified to build for seL4"; \
		exit 1; \
	fi
	@BOARD_DIR="$(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)"; \
	ARCH=$$(grep 'CONFIG_SEL4_ARCH  ' $$BOARD_DIR/include/kernel/gen_config.h | cut -d' ' -f4); \
	if [ "$$ARCH" = "aarch64" ]; then \
		TARGET_TRIPLE="aarch64-none-elf"; \
		CFLAGS_ARCH="-mstrict-align"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		LD="ld.lld"; \
		CTX_SRC="hal/arm64/ctx_arm64.S"; \
		SEL4_UART_SRC="hal/sel4/arm64/uart.c"; \
	elif [ "$$ARCH" = "riscv64" ]; then \
		TARGET_TRIPLE="riscv64-unknown-elf"; \
		CFLAGS_ARCH="-march=rv64imafdc_zicsr_zifencei -mabi=lp64d"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		LD="ld.lld"; \
		CTX_SRC="hal/riscv64/ctx_riscv64.S"; \
		SEL4_UART_SRC="hal/sel4/riscv64/uart.c"; \
	elif [ "$$ARCH" = "x86_64" ]; then \
		TARGET_TRIPLE="x86_64-linux-gnu"; \
		CFLAGS_ARCH="-march=x86-64 -mtune=generic"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		LD="ld.lld"; \
		CTX_SRC="hal/amd64/ctx_amd64.S"; \
		SEL4_UART_SRC="hal/sel4/amd64/uart.c"; \
	else \
		echo "Unsupported ARCH: $$ARCH"; \
		exit 1; \
	fi; \
	mkdir -p build/sel4-microkit; \
	ENTRY_NX="$(SEL4_ENTRY_NX)"; \
	if [ "$(EMBED_EXAMPLES)" = "1" ]; then \
		ENTRY_NX="/tmp/nexs_sel4_entry.nx"; \
		printf 'exec("$(SEL4_ENTRY_NX)")\n' > $$ENTRY_NX; \
		find example -name "*.nx" | sort | while read f; do printf 'exec("%s")\n' "$$f"; done >> $$ENTRY_NX; \
		echo "[sel4-microkit] EMBED_EXAMPLES=1: entry = $$ENTRY_NX"; \
	fi; \
	echo "[sel4-microkit] Generating embed table from $$ENTRY_NX..."; \
	./$(TARGET) --codegen $$ENTRY_NX -o build/sel4-microkit/nexs_embed.c || exit 1; \
	echo "[sel4-microkit] Compiling nexs.elf for $$ARCH..."; \
	$$CC -nostdlib -ffreestanding -O3 -Wall -Wno-unused-function \
		-DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) -DNEXS_SEL4 \
		$(INCS) -Ikernel/include -Ihal/include -I$$BOARD_DIR/include $$CFLAGS_ARCH \
		$(BAREMETAL_SRCS) $(KERNEL_SRCS) $$CTX_SRC \
		kernel/msg.c kernel/syscall.c kernel/vfs.c kernel/blk.c kernel/journal.c \
		build/sel4-microkit/nexs_embed.c \
		hal/sel4/hal_sel4.c hal/sel4/timer.c $$SEL4_UART_SRC hal/sel4/sel4_main.c \
		-L$$BOARD_DIR/lib -lmicrokit -Tmicrokit.ld -o build/sel4-microkit/nxscript.elf && \
	echo "[sel4-microkit] Done -> build/sel4-microkit/nxscript.elf"

# Standalone gen-embed target: generate the seL4 embed C file without compiling
gen-embed: $(TARGET)
	@mkdir -p build/sel4-microkit
	@ENTRY_NX="nxlib/services/init.nx"; \
	if [ "$(EMBED_EXAMPLES)" = "1" ]; then \
		ENTRY_NX="/tmp/nexs_sel4_entry.nx"; \
		printf 'exec("nxlib/services/init.nx")\n' > $$ENTRY_NX; \
		find example -name "*.nx" | sort | while read f; do printf 'exec("%s")\n' "$$f"; done >> $$ENTRY_NX; \
	fi; \
	echo "[gen-embed] Generating build/sel4-microkit/nexs_embed.c from $$ENTRY_NX..."; \
	./$(TARGET) --codegen $$ENTRY_NX -o build/sel4-microkit/nexs_embed.c && \
	echo "[gen-embed] Done"


# ─────────────────────────────────────────────────────────────────────────────
# sel4-multikernel — 4-PD isolated build (NEXS_MULTI_PD)
# ─────────────────────────────────────────────────────────────────────────────
# Produces 4 ELF binaries in build/sel4-multikernel/:
#   nexs_root.elf  — root PD (UART owner + daemon + interpreter)
#   nexs_fs.elf    — filesystem service PD
#   nexs_pm.elf    — process manager service PD
#   nexs_tty.elf   — TTY service PD
#
# The existing sel4-microkit (single-PD) target is completely unaffected.
#
# Usage:
#   make sel4-multikernel MICROKIT_BOARD=qemu_virt_aarch64 \
#                         MICROKIT_CONFIG=debug \
#                         MICROKIT_SDK=/path/to/sdk
# ─────────────────────────────────────────────────────────────────────────────
sel4-multikernel: $(TARGET)
	@if [ -z "$(MICROKIT_SDK)" ] || [ -z "$(MICROKIT_BOARD)" ] || [ -z "$(MICROKIT_CONFIG)" ]; then \
		echo "MICROKIT_SDK, MICROKIT_BOARD, and MICROKIT_CONFIG must be specified"; \
		exit 1; \
	fi
	@BOARD_DIR="$(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)"; \
	ARCH=$$(grep 'CONFIG_SEL4_ARCH  ' $$BOARD_DIR/include/kernel/gen_config.h | cut -d' ' -f4); \
	if [ "$$ARCH" = "aarch64" ]; then \
		TARGET_TRIPLE="aarch64-none-elf"; \
		CFLAGS_ARCH="-mstrict-align"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		CTX_SRC="hal/arm64/ctx_arm64.S"; \
		SEL4_UART_SRC="hal/sel4/arm64/uart.c"; \
		SYSTEM_FILE="hal/sel4/nexs_aarch64.system"; \
	elif [ "$$ARCH" = "riscv64" ]; then \
		TARGET_TRIPLE="riscv64-unknown-elf"; \
		CFLAGS_ARCH="-march=rv64imafdc_zicsr_zifencei -mabi=lp64d"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		CTX_SRC="hal/riscv64/ctx_riscv64.S"; \
		SEL4_UART_SRC="hal/sel4/riscv64/uart.c"; \
		SYSTEM_FILE="hal/sel4/nexs_riscv64.system"; \
	elif [ "$$ARCH" = "x86_64" ]; then \
		TARGET_TRIPLE="x86_64-linux-gnu"; \
		CFLAGS_ARCH="-march=x86-64 -mtune=generic"; \
		CC="clang -target $$TARGET_TRIPLE"; \
		CTX_SRC="hal/amd64/ctx_amd64.S"; \
		SEL4_UART_SRC="hal/sel4/amd64/uart.c"; \
		SYSTEM_FILE="hal/sel4/nexs_x86_64.system"; \
	else \
		echo "Unsupported ARCH: $$ARCH"; exit 1; \
	fi; \
	OUT="build/sel4-multikernel"; \
	mkdir -p $$OUT; \
	VERIFY_FLAGS=""; \
	VERIFY_SRC=""; \
	if [ "$(VERIFY)" = "1" ]; then \
		VERIFY_FLAGS="-DNEXS_SEL4_VERIFY"; \
		VERIFY_SRC="hal/sel4/verify/verify.c"; \
	fi; \
	COMMON_FLAGS="-nostdlib -ffreestanding -O3 -Wall -Wno-unused-function \
		-DNEXS_BAREMETAL -DNEXS_SEL4 -DNEXS_MULTI_PD \
		-DPOOL_$(POOL_PROFILE) \
		$$VERIFY_FLAGS \
		$(INCS) -Ikernel/include -Ihal/include \
		-I$$BOARD_DIR/include $$CFLAGS_ARCH"; \
	COMMON_SRCS="$(BAREMETAL_SRCS) $(KERNEL_SRCS) $$CTX_SRC \
		kernel/msg.c kernel/syscall.c kernel/vfs.c kernel/blk.c kernel/journal.c \
		hal/sel4/sel4_ipc_bridge.c hal/sel4/timer.c"; \
	echo "[sel4-multikernel] Generating embed tables..."; \
	./$(TARGET) --codegen nxlib/services/init.nx    -o $$OUT/nexs_embed_root.c || exit 1; \
	./$(TARGET) --codegen nxlib/services/fs/init.nx -o $$OUT/nexs_embed_fs.c  2>/dev/null || \
		printf '/* no fs script */\n' > $$OUT/nexs_embed_fs.c; \
	./$(TARGET) --codegen nxlib/services/pm/init.nx -o $$OUT/nexs_embed_pm.c  2>/dev/null || \
		printf '/* no pm script */\n' > $$OUT/nexs_embed_pm.c; \
	./$(TARGET) --codegen nxlib/services/tty/init.nx -o $$OUT/nexs_embed_tty.c 2>/dev/null || \
		printf '/* no tty script */\n' > $$OUT/nexs_embed_tty.c; \
	echo "[sel4-multikernel] Building nexs_root.elf ($$ARCH)..."; \
	$$CC $$COMMON_FLAGS $$COMMON_SRCS $$SEL4_UART_SRC \
		hal/sel4/hal_sel4.c hal/sel4/sel4_main.c $$VERIFY_SRC \
		$$OUT/nexs_embed_root.c \
		-L$$BOARD_DIR/lib -lmicrokit -Tmicrokit.ld \
		-o $$OUT/nexs_root.elf && echo "[sel4-multikernel] nexs_root.elf OK"; \
	echo "[sel4-multikernel] Building nexs_fs.elf ($$ARCH)..."; \
	$$CC $$COMMON_FLAGS $$COMMON_SRCS \
		hal/sel4/hal_sel4.c hal/sel4/sel4_pd_fs.c $$OUT/nexs_embed_fs.c \
		-L$$BOARD_DIR/lib -lmicrokit -Tmicrokit.ld \
		-o $$OUT/nexs_fs.elf  && echo "[sel4-multikernel] nexs_fs.elf OK"; \
	echo "[sel4-multikernel] Building nexs_pm.elf ($$ARCH)..."; \
	$$CC $$COMMON_FLAGS $$COMMON_SRCS \
		hal/sel4/hal_sel4.c hal/sel4/sel4_pd_pm.c $$OUT/nexs_embed_pm.c \
		-L$$BOARD_DIR/lib -lmicrokit -Tmicrokit.ld \
		-o $$OUT/nexs_pm.elf  && echo "[sel4-multikernel] nexs_pm.elf OK"; \
	echo "[sel4-multikernel] Building nexs_tty.elf ($$ARCH)..."; \
	$$CC $$COMMON_FLAGS $$COMMON_SRCS \
		hal/sel4/hal_sel4.c hal/sel4/sel4_pd_tty.c $$OUT/nexs_embed_tty.c \
		-L$$BOARD_DIR/lib -lmicrokit -Tmicrokit.ld \
		-o $$OUT/nexs_tty.elf && echo "[sel4-multikernel] nexs_tty.elf OK"; \
	echo "[sel4-multikernel] Packaging with Microkit tool..."; \
	$(MICROKIT_SDK)/bin/microkit $$SYSTEM_FILE \
		--search-path $$OUT \
		--board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) \
		-o $$OUT/loader.img \
		-r $$OUT/report.txt && \
	echo "[sel4-multikernel] Done -> $$OUT/loader.img"

sel4-multikernel-aarch64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-multikernel MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_CONFIG=smp-debug MICROKIT_SDK=$(MICROKIT_SDK)

sel4-multikernel-riscv64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-multikernel MICROKIT_BOARD=qemu_virt_riscv64 MICROKIT_CONFIG=smp-debug MICROKIT_SDK=$(MICROKIT_SDK)

sel4-multikernel-x86_64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-multikernel MICROKIT_BOARD=x86_64_generic MICROKIT_CONFIG=smp-debug MICROKIT_SDK=$(MICROKIT_SDK)

sel4-multikernel-amd64: sel4-multikernel-x86_64

# ─────────────────────────────────────────────────────────────────────────────
# ISO & Image Generation

# ─────────────────────────────────────────────────────────────────────────────
iso-amd64: baremetal-amd64
	@echo "Generating Bootable ISO..."
	@chmod +x scripts/make-iso.sh
	@./scripts/make-iso.sh

# ─────────────────────────────────────────────────────────────────────────────
# Execution & Emulation
# ─────────────────────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

run-iso: iso-amd64
	@chmod +x scripts/qemu-amd64.sh
	@./scripts/qemu-amd64.sh --iso

# ─────────────────────────────────────────────────────────────────────────────
# Clean
# ─────────────────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(TARGET)_dbg $(OBJS)
	rm -rf build/linux-amd64 build/linux-arm64 \
	       build/macos-amd64 build/macos-arm64 \
	       build/os1-amd64 build/os1-arm64 build/sdk-os1 \
	       build/assurance build/nxadb-os1 \
	       build/baremetal-arm64 build/baremetal-amd64 build/baremetal-riscv64 \
	       build/sel4-microkit \
	       build/iso_root build/nexs-amd64.iso \
	       build_aarch64 build_riscv64 build_x86_64
	@echo "Clean done"

# ─────────────────────────────────────────────────────────────────────────────
# seL4 entry script override (default: nxlib/services/init.nx)
# ─────────────────────────────────────────────────────────────────────────────
SEL4_ENTRY_NX ?= nxlib/services/init.nx

# ─────────────────────────────────────────────────────────────────────────────
# Baremetal RISC-V 64-bit
# ─────────────────────────────────────────────────────────────────────────────
baremetal-riscv64: $(TARGET)
	@if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 || command -v riscv64-elf-gcc >/dev/null 2>&1; then \
		RISCV_CC=$$(command -v riscv64-unknown-elf-gcc 2>/dev/null || command -v riscv64-elf-gcc); \
		mkdir -p build/baremetal-riscv64 && \
		$$RISCV_CC -march=rv64imafdc -mabi=lp64d -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			-nostdlib -nostartfiles -ffreestanding \
			-T hal/riscv64/nexs.ld \
			$(INCS) -Ikernel/include -Ihal/include \
			$(BAREMETAL_SRCS) $(KERNEL_SRCS) \
			hal/riscv64/boot.S hal/riscv64/uart.c hal/riscv64/timer.c hal/riscv64/ctx_riscv64.S \
			-o build/baremetal-riscv64/nxscript.elf && \
		echo "Cross-compiled -> build/baremetal-riscv64/nxscript.elf"; \
	else \
		echo "riscv64-unknown-elf-gcc not found, skipping baremetal-riscv64"; \
	fi

# ─────────────────────────────────────────────────────────────────────────────
# seL4/Microkit convenience wrappers — single-arch with hardcoded QEMU boards
# ─────────────────────────────────────────────────────────────────────────────
sel4-aarch64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-microkit MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_CONFIG=debug MICROKIT_SDK=$(MICROKIT_SDK)

sel4-riscv64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-microkit MICROKIT_BOARD=qemu_virt_riscv64 MICROKIT_CONFIG=debug MICROKIT_SDK=$(MICROKIT_SDK)

sel4-x86_64:
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	$(MAKE) sel4-microkit MICROKIT_BOARD=x86_64_generic MICROKIT_CONFIG=debug MICROKIT_SDK=$(MICROKIT_SDK)

# ─────────────────────────────────────────────────────────────────────────────
# seL4 QEMU launch targets (compile + run)
# ─────────────────────────────────────────────────────────────────────────────
sel4-run-aarch64: sel4-aarch64
	qemu-system-aarch64 \
		-machine virt,virtualization=on \
		-cpu cortex-a53 \
		-nographic \
		-serial mon:stdio \
		-device loader,file=build/sel4-microkit/loader.img,addr=0x70000000,cpu-num=0 \
		-m size=2G

sel4-run-riscv64: sel4-riscv64
	qemu-system-riscv64 \
		-machine virt \
		-nographic \
		-serial mon:stdio \
		-kernel build/sel4-microkit/loader.img \
		-m size=2G

sel4-run-x86_64: sel4-x86_64
	qemu-system-x86_64 \
		-cpu qemu64,+fsgsbase,+pdpe1gb,+xsaveopt,+xsave \
		-m 1G \
		-display none \
		-serial mon:stdio \
		-kernel build/sel4-microkit/sel4_32.elf \
		-initrd build/sel4-microkit/loader.img

# ─────────────────────────────────────────────────────────────────────────────
# seL4 example targets — compile example/minios/boot.nx as entry script
# ─────────────────────────────────────────────────────────────────────────────
sel4-example-aarch64:
	$(MAKE) sel4-aarch64 SEL4_ENTRY_NX=example/minios/boot.nx

sel4-example-riscv64:
	$(MAKE) sel4-riscv64 SEL4_ENTRY_NX=example/minios/boot.nx

sel4-example-x86_64:
	$(MAKE) sel4-x86_64 SEL4_ENTRY_NX=example/minios/boot.nx

# ─────────────────────────────────────────────────────────────────────────────
# seL4 initializer — clone/update nexs-kernel for SDK builds
# ─────────────────────────────────────────────────────────────────────────────
SEL4_DEPS_DIR ?= dependencies/nexs-kernel

sel4-initializer:
	@echo "[sel4-init] Setting up seL4/Microkit build environment..."
	@mkdir -p dependencies
	@if [ ! -d "$(SEL4_DEPS_DIR)" ]; then \
		git clone https://github.com/olmox001/nexs-kernel $(SEL4_DEPS_DIR); \
	else \
		echo "[sel4-init] $(SEL4_DEPS_DIR) already exists, pulling latest..."; \
		git -C $(SEL4_DEPS_DIR) pull; \
	fi
	@echo "[sel4-init] Done. Set MICROKIT_SDK to the SDK path after building:"
	@echo "  cd $(SEL4_DEPS_DIR) && make build-sdk-aarch64"
	@echo "  export MICROKIT_SDK=$(SEL4_DEPS_DIR)/root/release/microkit-sdk-*/..."

# ─────────────────────────────────────────────────────────────────────────────
# run-nexs-* — build, package with microkit tool, and launch QEMU
# Requires: MICROKIT_SDK=<path-to-sdk>
# ─────────────────────────────────────────────────────────────────────────────
run-nexs-aarch64: sel4-multikernel-aarch64
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	@mkdir -p build_aarch64
	$(MICROKIT_SDK)/bin/microkit hal/sel4/nexs_aarch64.system \
		--search-path build/sel4-multikernel \
		--board qemu_virt_aarch64 \
		--config smp-debug \
		-o build_aarch64/loader.img \
		-r build_aarch64/report.txt
	qemu-system-aarch64 \
		-machine virt,virtualization=on \
		-cpu cortex-a53 \
		-nographic \
		-serial mon:stdio \
		-device loader,file=build_aarch64/loader.img,addr=0x70000000,cpu-num=0 \
		-m size=2G

run-nexs-riscv64: sel4-multikernel-riscv64
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	@mkdir -p build_riscv64
	$(MICROKIT_SDK)/bin/microkit hal/sel4/nexs_riscv64.system \
		--search-path build/sel4-multikernel \
		--board qemu_virt_riscv64 \
		--config smp-debug \
		-o build_riscv64/loader.img \
		-r build_riscv64/report.txt
	qemu-system-riscv64 \
		-machine virt \
		-nographic \
		-serial mon:stdio \
		-kernel build_riscv64/loader.img \
		-m size=2G

run-nexs-x86_64: sel4-multikernel-x86_64
	@if [ -z "$(MICROKIT_SDK)" ]; then echo "Set MICROKIT_SDK=..."; exit 1; fi
	@mkdir -p build_x86_64
	$(MICROKIT_SDK)/bin/microkit hal/sel4/nexs_x86_64.system \
		--search-path build/sel4-multikernel \
		--board x86_64_generic \
		--config smp-debug \
		-o build_x86_64/loader.img \
		-r build_x86_64/report.txt
	qemu-system-x86_64 \
		-cpu qemu64,+fsgsbase,+pdpe1gb,+xsaveopt,+xsave \
		-m 1G \
		-display none \
		-serial mon:stdio \
		-kernel build_x86_64/sel4_32.elf \
		-initrd build_x86_64/loader.img

run-nexs-amd64: run-nexs-x86_64

# ─────────────────────────────────────────────────────
# NEXS Verification Targets (VERIFY=1)
# ─────────────────────────────────────────────────────
verify-nexs-aarch64:
	$(MAKE) run-nexs-aarch64 VERIFY=1

verify-nexs-riscv64:
	$(MAKE) run-nexs-riscv64 VERIFY=1

verify-nexs-x86_64:
	$(MAKE) run-nexs-x86_64 VERIFY=1

verify-nexs-amd64: verify-nexs-x86_64
# Independent C regression binaries must link the same canonical NXOLMO
# state engine as the product. Avoid a test-only shadow registry.
NEXS_STATE_PRODUCT_LINK_SRCS := \
  core/status.c core/types.c core/result.c core/crypto/sha256.c \
  runtime/operation_budget.c runtime/operation_metrics.c \
  runtime/operation_catalog.c runtime/cycle_counter_fallback.c \
  runtime/clock_unavailable.c \
  registry/state_store.c registry/nxreg.c registry/snapshot_pool.c \
  registry/schema.c registry/migration.c
NEXS_STATE_PRODUCT_LINK_OBJS := $(NEXS_STATE_PRODUCT_LINK_SRCS:.c=.o)

.PHONY: test-nxolmo-resource-conformance
NEXS_RESOURCE_CONFORMANCE_TESTS := test_capability test_hardware_capability \
  test_buffer test_buffer_concurrent_views test_ring_mpmc test_p9_frame
# Instrument the actual FD owner, not the uninstrumented fd_owner.o.
# Link against the real existing product runtime and its provider.
.PHONY: test-sysio-fd-owner-sanitized
test-sysio-fd-owner-sanitized: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -O1 -g -Wall -Wextra -Werror \
	  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
	  tests/test_sysio_fd_owner.c sys/fd_owner.c \
	  $(filter-out runtime/main.o sys/fd_owner.o,$(OBJS)) \
	  -o build/assurance/test_sysio_fd_owner_sanitized
	@ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	  UBSAN_OPTIONS=halt_on_error=1 \
	  ./build/assurance/test_sysio_fd_owner_sanitized

# Real sysio session + registry + hosted backend, including dup/close,
# source priority and actual pipe: no mock resource or fake descriptor.
.PHONY: test-sysio-fd-owner
test-sysio-fd-owner: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -pthread tests/test_sysio_fd_owner.c \
	  $(filter-out runtime/main.o,$(OBJS)) \
	  -o build/assurance/test_sysio_fd_owner
	@./build/assurance/test_sysio_fd_owner

# Existing NXOLMO fuel budget enforces bounded HAL bytecode execution.
# Raw device opcodes must be denied until NXEC kernel resource admission.
.PHONY: test-halbc-budget-authority
test-halbc-budget-authority: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -Werror -Wconversion -Wsign-conversion \
	  tests/test_halbc_budget_authority.c \
	  $(filter-out runtime/main.o,$(OBJS)) \
	  -o build/assurance/test_halbc_budget_authority
	@./build/assurance/test_halbc_budget_authority

# Authenticated hardware grants are NOT inferred from HAL metadata.
# Test links real Base-NEXS runtime+registry+HAL modules without mocks.
.PHONY: test-hal-module-authority-boundary
test-hal-module-authority-boundary: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -Werror -Wconversion -Wsign-conversion -pthread \
	  tests/test_hal_module_authority_boundary.c \
	  $(filter-out runtime/main.o,$(OBJS)) \
	  -o build/assurance/test_hal_module_authority_boundary
	@./build/assurance/test_hal_module_authority_boundary

# Real NXOLMO async block request/completion rings, durability, fault
# injection and backwards compatible device adapter; original test imported
# into PRODUCT tests, no reference library linked into product.
.PHONY: test-nxolmo-block-conformance
# NXOLMO original WAL conformance + real crash/corruption mutation suite,
# against the Base-NEXS production port and already verified block backend.
.PHONY: test-nxolmo-wal-conformance
test-nxolmo-wal-conformance: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -Werror tests/nxolmo_canonical/test_wal.c \
	  core/checked.o core/crypto/crc64.o core/crypto/sha256.o \
	  block/device.o block/memory.o block/memory_device.o journal/wal.o \
	  -o build/assurance/test_wal
	@./build/assurance/test_wal
	@$(CC) $(CFLAGS) -Werror tests/nxolmo_canonical/fuzz_wal_standalone.c \
	  core/checked.o core/crypto/crc64.o core/crypto/sha256.o \
	  block/device.o block/memory.o block/memory_device.o journal/wal.o \
	  -o build/assurance/fuzz_wal_standalone
	@./build/assurance/fuzz_wal_standalone

test-nxolmo-block-conformance: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -Werror tests/nxolmo_canonical/test_block_memory.c \
	  core/checked.o block/device.o block/memory.o block/memory_device.o \
	  -o build/assurance/test_block_memory
	@./build/assurance/test_block_memory
	@$(CC) $(CFLAGS) -Werror tests/nxolmo_canonical/test_block_memory_adversarial.c \
	  core/checked.o block/device.o block/memory.o block/memory_device.o \
	  -o build/assurance/test_block_memory_adversarial
	@./build/assurance/test_block_memory_adversarial

test-nxolmo-resource-conformance: $(TARGET)
	@set -e; mkdir -p build/assurance; \
	for test in $(NEXS_RESOURCE_CONFORMANCE_TESTS); do \
	  $(CC) $(CFLAGS) -pthread tests/nxolmo_canonical/$$test.c \
	    core/checked.o core/crypto/crc64.o runtime/clock_hosted.o \
	    $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	    authority/capability.o hardware/capability.o buffer/buffer.o \
	    buffer/capability_adapter.o fs/9p_frame.o \
	    -o build/assurance/$$test; \
	  ./build/assurance/$$test; \
	done

# Native baremetal QEMU interactive UART qualification on BOTH Base-NEXS
# ISA targets; explicit opt-in, not an OS1 image build or stub.
.PHONY: test-baremetal-qemu-interactive
test-baremetal-qemu-interactive: baremetal-amd64 baremetal-arm64
	@python3 scripts/test_baremetal_qemu_interactive.py --arch amd64 --timeout 20
	@python3 scripts/test_baremetal_qemu_interactive.py --arch arm64 --timeout 20

# Report actual 21x3 syscall dispatch paths. Both CSV and run log are
# mandatory; the reporter rejects missing calls, failed assertions and
# mismatched preflight+randomized call totals.
NX_SYSCALL_PROFILE_CSV ?= build/assurance/system_syscall_fuzz_1m_3paths.csv
NX_SYSCALL_PROFILE_LOG ?= build/assurance/system_fuzz_1m_3paths.log
.PHONY: report-real-syscall-fuzz
report-real-syscall-fuzz:
	@python3 scripts/report_real_syscall_fuzz.py \
	  --csv $(NX_SYSCALL_PROFILE_CSV) \
	  --log $(NX_SYSCALL_PROFILE_LOG) \
	  --out docs/rearchitecture/P8_SYSCALL_PROFILE_2026_11_21.md

.PHONY: test-p9-request-conformance perf-billion-p9 perf-billion-buffer
test-p9-request-conformance: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_p9_request_validation.c \
	  core/checked.o fs/9p_frame.o -o build/assurance/test_p9_request_validation
	@./build/assurance/test_p9_request_validation

# Explicit long-running stress, never hidden inside the fast regression suite.
# A run counts exactly one actual decoder call per operation and validates
# success/rejection per iteration; normal profile = 200 million.
NX_P9_BENCH_OPERATIONS ?= 200000000
NX_P9_BENCH_WORKERS ?= 8
# Legacy target names maintained; standard = 200M, opt-in stress = 1B.
.PHONY: perf-standard-p9 perf-standard-buffer perf-stress-billion-p9 perf-stress-billion-buffer
perf-standard-p9:
	@$(MAKE) perf-billion-p9 NX_P9_BENCH_OPERATIONS=200000000
perf-standard-buffer:
	@$(MAKE) perf-billion-buffer NX_BUFFER_BENCH_OPERATIONS=200000000
perf-stress-billion-p9:
	@$(MAKE) perf-billion-p9 NX_P9_BENCH_OPERATIONS=1000000000
perf-stress-billion-buffer:
	@$(MAKE) perf-billion-buffer NX_BUFFER_BENCH_OPERATIONS=1000000000

perf-billion-p9:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -O3 -pthread tests/perf/billion_p9.c \
	  core/checked.o fs/9p_frame.o runtime/clock_hosted.o \
	  runtime/cycle_counter_fallback.o \
	  $(filter runtime/cycle_counter_hosted_amd64.o,$(OBJS)) \
	  -o build/assurance/billion_p9
	@./build/assurance/billion_p9 $(NX_P9_BENCH_OPERATIONS) $(NX_P9_BENCH_WORKERS)

# 200 million bit updates, with optional 1B stress, under a real signed
# NXOLMO capability, followed by a single validated buffer commit.
NX_BUFFER_BENCH_OPERATIONS ?= 200000000
NX_BUFFER_BENCH_BYTES ?= 1048576
perf-billion-buffer:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -O3 -pthread tests/perf/billion_masked_buffer.c \
	  core/checked.o core/crypto/crc64.o $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	  authority/capability.o hardware/capability.o buffer/buffer.o \
	  buffer/capability_adapter.o runtime/clock_hosted.o \
	  runtime/cycle_counter_fallback.o \
	  $(filter runtime/cycle_counter_hosted_amd64.o,$(OBJS)) \
	  -o build/assurance/billion_masked_buffer
	@./build/assurance/billion_masked_buffer \
	  $(NX_BUFFER_BENCH_OPERATIONS) $(NX_BUFFER_BENCH_BYTES)

# Whole Base-NEXS hosted runtime + original kernel dispatcher + original
# registry/VFS/9P. Uses the actual AMD64 context-switch assembly rewritten
# ONLY for Darwin assembler symbol/section syntax, never a fake scheduler.
NX_SYSCALL_FUZZ_CASES ?= 100000
NX_SYSCALL_FUZZ_SEED ?= 0xa11ce20261121
NX_SYSCALL_FUZZ_ALLOC_PROFILE ?= 1
NEXS_SYSTEM_FUZZ_LINK_OBJS = $(filter-out runtime/main.o compiler/codegen.o compiler/dep_scan.o compiler/driver.o,$(OBJS))
.PHONY: fuzz-system-syscalls
fuzz-system-syscalls: $(TARGET)
	@set -e; mkdir -p build/assurance; \
	if [ "$$(uname -s)" = "Darwin" ] && [ "$$(uname -m)" = "x86_64" ]; then \
	  sed -e 's/^\.section \.text$$/.text/' \
	      -e 's/^\.global ctx_switch$$/.global _ctx_switch/' \
	      -e 's/^ctx_switch:$$/_ctx_switch:/' \
	      hal/amd64/ctx_amd64.S > build/assurance/ctx_switch_hosted_amd64.S; \
	  ctx=build/assurance/ctx_switch_hosted_amd64.S; \
	elif [ "$$(uname -s)" = "Linux" ] && [ "$$(uname -m)" = "x86_64" ]; then \
	  ctx=hal/amd64/ctx_amd64.S; \
	else \
	  echo 'fuzz-system-syscalls: platform/context switch unqualified; FAIL'; exit 2; \
	fi; \
	$(CC) $(CFLAGS) -pthread \
	  tests/fuzz/system_syscall_fuzz.c $(NEXS_SYSTEM_FUZZ_LINK_OBJS) \
	  kernel/vfs.c kernel/syscall.c kernel/msg.c kernel/proc.c kernel/sched.c fs/9p.c \
	  $$ctx -o build/assurance/system_syscall_fuzz; \
	./build/assurance/system_syscall_fuzz \
	  $(NX_SYSCALL_FUZZ_CASES) $(NX_SYSCALL_FUZZ_SEED) \
	  build/assurance/system_syscall_fuzz.csv $(NX_SYSCALL_FUZZ_ALLOC_PROFILE)

.PHONY: test-allocator-observation
test-allocator-observation: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_allocator_observation_core.c \
	  $(NEXS_SYSTEM_FUZZ_LINK_OBJS) \
	  -o build/assurance/test_allocator_observation
	@./build/assurance/test_allocator_observation

# Actual committed VFS writes + readback, each with NXOLMO identity/revision
# checks. Unlike the codec/buffer microbenchmarks this changes live registry
# state on EACH iteration, and uses full 9P wire roundtrips.
NX_P9_FULL_COMMITS ?= 1000000
.PHONY: perf-p9-full-commits
perf-p9-full-commits:
	@NEXS_P9_FUZZ_ROUNDS=$(NX_P9_FULL_COMMITS) \
	  $(MAKE) -s test-full-9p-vfs-runtime

# Sanitized complete hosted runtime: recompile the actual product sources,
# not just the hostile test or a fake VFS, under ASan/UBSan instrumentation.
NEXS_FULL_SANITIZED_SRCS = $(filter-out runtime/main.c compiler/codegen.c compiler/dep_scan.c compiler/driver.c,$(SRCS))
.PHONY: test-9p-e2e-sanitizers
test-9p-e2e-sanitizers:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined \
	  -fno-omit-frame-pointer -pthread \
	  tests/test_full_9p_vfs_roundtrip.c $(NEXS_FULL_SANITIZED_SRCS) \
	  kernel/vfs.c fs/9p.c \
	  -o build/assurance/test_9p_full_sanitized
	@ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	  UBSAN_OPTIONS=halt_on_error=1 \
	  NEXS_P9_FUZZ_ROUNDS=1000 NEXS_P9_NEGATIVE_CASES=10000 \
	  ./build/assurance/test_9p_full_sanitized

.PHONY: test-full-9p-vfs-runtime
test-full-9p-vfs-runtime: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -pthread tests/test_full_9p_vfs_roundtrip.c \
	  $(NEXS_SYSTEM_FUZZ_LINK_OBJS) kernel/vfs.c fs/9p.c \
	  -o build/assurance/test_full_9p_vfs_runtime
	@./build/assurance/test_full_9p_vfs_runtime

# Every ordinary full test run dispatches real randomized calls through
# each of 21 syscall verbs, the real kernel message codec, real REG IPC
# and 9P; long campaigns live in fuzz-system-syscalls.
# Antagonistic NEXS interpreter and grammar execution. Tests are NOT
# production Power12 code; failed oracle, crash or timeout blocks the suite.
# Long-running campaigns are opt-in and keep seed/input repro artifacts.
NX_LANGUAGE_FUZZ_CASES ?= 256
NX_LANGUAGE_FUZZ_SEED ?= 0x20261121
.PHONY: test-language-fuzz fuzz-language
test-language-fuzz: $(TARGET)
	@python3 tests/fuzz/nx_language_runtime_fuzz.py \
	  --cases 32 --seed $(NX_LANGUAGE_FUZZ_SEED) \
	  --output build/assurance/nx_language_fuzz_test

fuzz-language: $(TARGET)
	@python3 tests/fuzz/nx_language_runtime_fuzz.py \
	  --cases $(NX_LANGUAGE_FUZZ_CASES) --seed $(NX_LANGUAGE_FUZZ_SEED) \
	  --output build/assurance/nx_language_fuzz_full

.PHONY: test-system-syscall-fuzz
test-system-syscall-fuzz:
	@$(MAKE) -s fuzz-system-syscalls NX_SYSCALL_FUZZ_CASES=4096

# Original NXOLMO schema, snapshot, NXREG and concurrency assertions,
# compiled against product-owned source files, not external reference objects.
NEXS_STATE_CONFORMANCE_TESTS := test_snapshot_pool test_schema test_migration \
  test_nxreg_api test_snapshot_concurrent \
  test_snapshot_pool_concurrent test_schema_concurrent
.PHONY: test-nxolmo-state-conformance
test-nxolmo-state-conformance: $(TARGET)
	@set -e; mkdir -p build/assurance; \
	for test in $(NEXS_STATE_CONFORMANCE_TESTS); do \
	  $(CC) $(CFLAGS) -pthread tests/nxolmo_canonical/$$test.c \
	    core/checked.o $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	    -o build/assurance/$$test; \
	  ./build/assurance/$$test; \
	done

# The names visible to the language are a projection of the one NXOLMO
# StateStore: prove actual ID, generation, parent, revision, growth, retirement.
.PHONY: test-nxolmo-regkey-integration
test-nxolmo-regkey-integration: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_nxolmo_regkey_state_core.c \
	  core/pager.o core/buddy.o core/dynarray.o core/value.o core/utils.o \
	  core/checked.o registry/registry.o registry/reg_ipc.o \
	  $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_nxolmo_regkey_integration
	@./build/assurance/test_nxolmo_regkey_integration

# NXOLMO StateStore migration precondition: protect the actual, single
# legacy RegKey backend from path truncation, incomplete free and move loss.
# Test uses production registry/dynarray/value/buddy sources (no mocks).
.PHONY: test-registry-integrity-core
test-registry-integrity-core: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_registry_integrity_core.c \
	  core/pager.o core/buddy.o core/dynarray.o core/value.o core/utils.o core/checked.o \
	  registry/registry.o registry/reg_ipc.o $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_integrity_core
	@./build/assurance/test_registry_integrity_core

# P1 exact page ownership: test the ACTUAL hosted mmap provider, registry
# bookkeeping and unified allocator against invalid release and OOM.
.PHONY: test-pager-ownership-core test-pager-ownership-asan
test-pager-ownership-core: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_pager_exact_ownership_core.c \
	  core/pager.o core/buddy.o core/dynarray.o core/value.o \
	  core/utils.o core/checked.o registry/registry.o registry/reg_ipc.o $(NEXS_STATE_PRODUCT_LINK_OBJS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_pager_ownership_core
	@./build/assurance/test_pager_ownership_core

test-pager-ownership-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) tests/test_pager_exact_ownership_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_pager_ownership_sanitized
	@./build/assurance/test_pager_ownership_sanitized

# The real allocator is linked in a 4 KiB test profile to force OOM without
# starving the developer machine. No fake allocator or scripted success.
.PHONY: test-recoverable-alloc-core test-recoverable-alloc-asan
test-recoverable-alloc-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_4KB tests/test_recoverable_alloc_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_recoverable_alloc_core
	@./build/assurance/test_recoverable_alloc_core

test-recoverable-alloc-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_4KB tests/test_recoverable_alloc_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_recoverable_alloc_sanitized
	@./build/assurance/test_recoverable_alloc_sanitized

# Full buddy-domain traversal, zero/oversized input, fragment rejection,
# exact free, no recursion and coalescence using the SAME production code.
.PHONY: test-buddy-iteration-core test-buddy-iteration-asan
test-buddy-iteration-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_4KB tests/test_buddy_iteration_core.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_iteration_core
	@./build/assurance/test_buddy_iteration_core

test-buddy-iteration-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_4KB tests/test_buddy_iteration_core.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_iteration_sanitized
	@./build/assurance/test_buddy_iteration_sanitized

# Two independently backed arenas use the SAME real Buddy algorithms:
# exact lifetime/overlap/foreign-pointer/OOM, not a new provider.
.PHONY: test-buddy-arena-isolation-core test-buddy-arena-isolation-asan
test-buddy-arena-isolation-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  tests/test_buddy_arena_isolation_core.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_arena_isolation_core
	@./build/assurance/test_buddy_arena_isolation_core

test-buddy-arena-isolation-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  tests/test_buddy_arena_isolation_core.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_arena_isolation_sanitized
	@./build/assurance/test_buddy_arena_isolation_sanitized

# Independent caller-supplied Buddy domains under real pthread execution.
# Not a claim of shared arena locking or VM task concurrency.
.PHONY: test-buddy-arena-threaded-core test-buddy-arena-threaded-asan
test-buddy-arena-threaded-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -pthread -UPOOL_64MB -DPOOL_4KB \
	  tests/test_buddy_arena_thread_isolation.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_arena_threaded_core
	@./build/assurance/test_buddy_arena_threaded_core

test-buddy-arena-threaded-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -pthread -UPOOL_64MB -DPOOL_4KB \
	  tests/test_buddy_arena_thread_isolation.c \
	  core/buddy.c core/utils.c core/checked.c \
	  -o build/assurance/test_buddy_arena_threaded_sanitized
	@./build/assurance/test_buddy_arena_threaded_sanitized

# The same production registry, Value, allocator and IPC implementation
# compiled together under AddressSanitizer and UndefinedBehaviorSanitizer.
.PHONY: test-registry-integrity-asan
test-registry-integrity-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) tests/test_registry_integrity_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c core/utils.c core/checked.c \
	  registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_integrity_sanitized
	@./build/assurance/test_registry_integrity_sanitized

# Actual partial-path rollback under allocator exhaustion. No injected mock.
.PHONY: test-registry-fallible-oom test-registry-fallible-oom-asan
test-registry-fallible-oom:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_32KB \
	  tests/test_registry_fallible_oom.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_fallible_oom
	@./build/assurance/test_registry_fallible_oom

test-registry-fallible-oom-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_32KB \
	  tests/test_registry_fallible_oom.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_fallible_oom_asan
	@./build/assurance/test_registry_fallible_oom_asan

# Real Value checked arithmetic and forced Buddy exhaustion; 32 KiB provider.
.PHONY: test-value-checked-core test-value-checked-asan
test-value-checked-core:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_32KB \
	  tests/test_value_checked_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_value_checked_core
	@./build/assurance/test_value_checked_core

test-value-checked-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_32KB \
	  tests/test_value_checked_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_value_checked_asan
	@./build/assurance/test_value_checked_asan

# Initialization must roll back the single canonical namespace on real OOM.
.PHONY: test-registry-init-recovery test-registry-init-recovery-asan
test-registry-init-recovery:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  tests/test_registry_init_recovery_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_init_recovery
	@./build/assurance/test_registry_init_recovery

test-registry-init-recovery-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  tests/test_registry_init_recovery_core.c \
	  core/pager.c core/buddy.c core/dynarray.c core/value.c \
	  core/utils.c core/checked.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_registry_init_recovery_asan
	@./build/assurance/test_registry_init_recovery_asan

# One actual parser AST: bounded iterative owner release, not recursion.
.PHONY: test-ast-iterative-owner test-ast-iterative-owner-asan
test-ast-iterative-owner:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_16MB \
	  tests/test_ast_iterative_owner_core.c \
	  lang/parser.c lang/lexer.c core/buddy.c core/pager.c core/dynarray.c \
	  core/value.c core/utils.c core/checked.c registry/registry.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  registry/reg_ipc.c hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_ast_iterative_owner
	@./build/assurance/test_ast_iterative_owner

test-ast-iterative-owner-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_16MB \
	  tests/test_ast_iterative_owner_core.c \
	  lang/parser.c lang/lexer.c core/buddy.c core/pager.c core/dynarray.c \
	  core/value.c core/utils.c core/checked.c registry/registry.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
	  registry/reg_ipc.c hal/common/pipe_transport_unavailable.c \
	  -o build/assurance/test_ast_iterative_owner_asan
	@./build/assurance/test_ast_iterative_owner_asan

# Actual linked runtime startup and budgeted VM AST dispatch. No mock.
.PHONY: test-runtime-admission-budget test-runtime-admission-budget-asan
test-runtime-admission-budget: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_runtime_admission_budget_core.c \
	  $(OBJS) -o build/assurance/test_runtime_admission_budget
	@./build/assurance/test_runtime_admission_budget

test-runtime-admission-budget-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) tests/test_runtime_admission_budget_core.c \
	  $(SRCS) -o build/assurance/test_runtime_admission_budget_asan
	@./build/assurance/test_runtime_admission_budget_asan

# Compiled, real kernel CSpace + current-process/RegKey/Buddy dependency closure.
# No stubbed proc_current or simulated capability checks. The unrelated
# process scheduler/cswitch functions are dead-stripped from this host test.
ifeq ($(UNAME_S),Darwin)
  KERNEL_TEST_LINK_GC := -Wl,-dead_strip
else
  KERNEL_TEST_LINK_GC := -Wl,--gc-sections
endif
KERNEL_CAP_TEST_SRCS := kernel/cap.c kernel/proc.c \
  core/buddy.c core/pager.c core/value.c core/dynarray.c \
  core/checked.c core/utils.c registry/registry.c registry/reg_ipc.c $(NEXS_STATE_PRODUCT_LINK_SRCS) \
  hal/common/pipe_transport_unavailable.c

.PHONY: test-kernel-cap-cspace test-kernel-cap-cspace-oom
.PHONY: test-kernel-cap-cspace-asan test-kernel-cap-cspace-oom-asan
test-kernel-cap-cspace:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -iquote kernel/include -ffunction-sections -fdata-sections \
	  kernel/test/test_cap_cspace_integrity.c $(KERNEL_CAP_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_cap_cspace
	@./build/assurance/test_kernel_cap_cspace

test-kernel-cap-cspace-oom:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -iquote kernel/include -UPOOL_64MB -DPOOL_4KB \
	  -ffunction-sections -fdata-sections \
	  kernel/test/test_cap_cspace_oom.c $(KERNEL_CAP_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_cap_cspace_oom
	@./build/assurance/test_kernel_cap_cspace_oom

test-kernel-cap-cspace-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -iquote kernel/include -ffunction-sections -fdata-sections \
	  kernel/test/test_cap_cspace_integrity.c $(KERNEL_CAP_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_cap_cspace_asan
	@./build/assurance/test_kernel_cap_cspace_asan

test-kernel-cap-cspace-oom-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -iquote kernel/include -UPOOL_64MB -DPOOL_4KB \
	  -ffunction-sections -fdata-sections \
	  kernel/test/test_cap_cspace_oom.c $(KERNEL_CAP_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_cap_cspace_oom_asan
	@./build/assurance/test_kernel_cap_cspace_oom_asan

# Direct compiled kernel endpoint/notification admission using the actual
# fallible production allocator. Unused scheduler paths are dead-stripped.
KERNEL_IPC_OWNER_TEST_SRCS := kernel/ipc.c core/buddy.c core/pager.c \
  core/utils.c core/checked.c
.PHONY: test-kernel-ipc-owner test-kernel-ipc-owner-asan
test-kernel-ipc-owner:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  -iquote kernel/include -ffunction-sections -fdata-sections \
	  kernel/test/test_ipc_alloc_core.c $(KERNEL_IPC_OWNER_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_ipc_owner
	@./build/assurance/test_kernel_ipc_owner

test-kernel-ipc-owner-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -UPOOL_64MB -DPOOL_4KB \
	  -iquote kernel/include -ffunction-sections -fdata-sections \
	  kernel/test/test_ipc_alloc_core.c $(KERNEL_IPC_OWNER_TEST_SRCS) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_kernel_ipc_owner_asan
	@./build/assurance/test_kernel_ipc_owner_asan

# Direct product registry/Value IPC linked against the actual POSIX hosted
# provider: nested arrays, malformed frames, valid TYPE_ERR(99), EOF.
# A successful unit test is NOT NXEC REG protocol interoperability evidence.
.PHONY: test-ipc-array-wire test-ipc-array-wire-asan
test-ipc-array-wire: $(TARGET)
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) tests/test_ipc_array_frame_core.c \
	  $(filter-out runtime/main.o,$(OBJS)) \
	  -o build/assurance/test_ipc_array_wire
	@./build/assurance/test_ipc_array_wire

test-ipc-array-wire-asan:
	@mkdir -p build/assurance
	@$(CC) $(DBGFLAGS) -ffunction-sections -fdata-sections \
	  tests/test_ipc_array_frame_core.c \
	  $(filter-out runtime/main.c,$(SRCS)) \
	  $(KERNEL_TEST_LINK_GC) -o build/assurance/test_ipc_array_wire_asan
	@./build/assurance/test_ipc_array_wire_asan


# Verified NXOLMO cryptographic algorithms ported to the canonical Base-NEXS
# core ABI. One implementation linked for ALL targets via config/sources.mk.
.PHONY: test-crypto-canonical-core test-crypto-canonical-asan
test-crypto-canonical-core:
	@mkdir -p build/assurance
	@clang -std=c11 -Wall -Wextra -Werror -Icore/include -O2 \
	  core/checked.c core/crypto/crc32c.c core/crypto/crc64.c \
	  core/crypto/sha256.c tests/test_crypto_canonical_core.c \
	  -o build/assurance/test-crypto-canonical-core
	@build/assurance/test-crypto-canonical-core

test-crypto-canonical-asan:
	@mkdir -p build/assurance
	@clang -std=c11 -Wall -Wextra -Werror -Icore/include -O1 \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  core/checked.c core/crypto/crc32c.c core/crypto/crc64.c \
	  core/crypto/sha256.c tests/test_crypto_canonical_core.c \
	  -o build/assurance/test-crypto-canonical-asan
	@build/assurance/test-crypto-canonical-asan

# NXOLMO reference suite exercised on the actual canonical product core.
# The provider selection includes real hosted CPU counters where available.
RUNTIME_OP_TEST_SRCS := core/checked.c core/status.c core/types.c core/result.c \
  runtime/operation_budget.c runtime/operation_metrics.c \
  runtime/operation_catalog.c runtime/cycle_counter_fallback.c \
  runtime/clock_unavailable.c runtime/clock_hosted.c \
  $(filter runtime/cycle_counter_hosted_amd64.c,$(NEXS_HOSTED_ADAPTER_SRCS))

.PHONY: test-operation-runtime-canonical test-operation-catalog-canonical test-clock-provider-hosted test-operation-runtime-asan
test-operation-runtime-canonical:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) $(RUNTIME_OP_TEST_SRCS) \
	  tests/test_operation_runtime_canonical.c -o build/assurance/test_operation_runtime
	@./build/assurance/test_operation_runtime

test-operation-catalog-canonical:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) $(RUNTIME_OP_TEST_SRCS) \
	  tests/test_operation_catalog_canonical.c -o build/assurance/test_operation_catalog
	@./build/assurance/test_operation_catalog

test-clock-provider-hosted:
	@mkdir -p build/assurance
	@$(CC) $(CFLAGS) $(RUNTIME_OP_TEST_SRCS) \
	  tests/test_clock_provider_hosted.c -o build/assurance/test_clock_provider_hosted
	@./build/assurance/test_clock_provider_hosted

test-operation-runtime-asan:
	@mkdir -p build/assurance
	@clang $(DBGFLAGS) $(RUNTIME_OP_TEST_SRCS) \
	  tests/test_operation_runtime_canonical.c -o build/assurance/test_operation_runtime_asan
	@./build/assurance/test_operation_runtime_asan
	@clang $(DBGFLAGS) $(RUNTIME_OP_TEST_SRCS) \
	  tests/test_operation_catalog_canonical.c -o build/assurance/test_operation_catalog_asan
	@./build/assurance/test_operation_catalog_asan
