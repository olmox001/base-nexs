# =============================================================================
# NEXS — Top-Level Makefile
# =============================================================================
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
  core/include/nexs_alloc.h \
  core/include/nexs_value.h \
  core/include/nexs_utils.h \
  registry/include/nexs_registry.h \
  lang/include/nexs_lex.h \
  lang/include/nexs_ast.h \
  lang/include/nexs_fn.h \
  lang/include/nexs_parse.h \
  lang/include/nexs_eval.h \
  sys/include/nexs_sys.h \
  runtime/include/nexs_runtime.h \
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

quality-check: gate

check: gate test

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
OS1_AOT_DEP_FLAG := $(if $(filter 1,$(OS1_AOT_NO_DEP)),--no-dep,)

.PHONY: os1-aot-amd64 os1-aot-arm64 os1-aot-build
os1-aot-amd64:
	@$(MAKE) OS1_ARCH=amd64 os1-aot-build

os1-aot-arm64:
	@$(MAKE) OS1_ARCH=arm64 os1-aot-build

os1-aot-build: gate $(TARGET) $(OS1_OBJECTS) $(OS1_SDK_CRT) $(OS1_SDK_ARCHIVE)
	@test -f "$(OS1_AOT_ENTRY_NX)" || { echo "Missing .nx source: $(OS1_AOT_ENTRY_NX)" >&2; exit 2; }
	@mkdir -p "$(OS1_BUILD_DIR)" "$(dir $(OS1_AOT_OUTPUT))"
	@./$(TARGET) --codegen "$(OS1_AOT_ENTRY_NX)" $(OS1_AOT_DEP_FLAG) --hosted -o "$(OS1_BUILD_DIR)/nxscript-program.c"
	@$(OS1_CC) $(OS1_CFLAGS) -c "$(OS1_BUILD_DIR)/nxscript-program.c" -o "$(OS1_BUILD_DIR)/nxscript-program.o"
	@$(OS1_CC) $(OS1_CFLAGS) -Wl,--gc-sections -Wl,-Ttext=0x80000000 -e _start 	  -o "$(OS1_AOT_OUTPUT)" $(OS1_OBJECTS) "$(OS1_BUILD_DIR)/nxscript-program.o" 	  "$(OS1_SDK_CRT)" -Wl,--start-group "$(OS1_SDK_ARCHIVE)" -lgcc -Wl,--end-group
	@echo "NexsOS1 $(OS1_ARCH) AOT -> $(OS1_AOT_OUTPUT)"

.PHONY: os1-check
os1-check: gate $(TARGET)
	@python3 scripts/test_os1_compiler.py

-include $(wildcard build/os1-$(OS1_ARCH)/**/*.d)
endif # OS1 native target requested; non-OS1 hosts need no NX-SDK.


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

test: $(TARGET)
	@python3 -m unittest discover -s tests -p 'test_*.py' -v
	@python3 scripts/test_nx_syntax.py
	@python3 scripts/test_runtime.py --binary $(TARGET)

.PHONY: check-examples
check-examples: $(TARGET)
	@python3 scripts/test_runtime.py --binary $(TARGET) --all

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
BAREMETAL_EMBED_DEPS ?= 0
BAREMETAL_CODEGEN_FLAGS := $(if $(filter 1,$(BAREMETAL_EMBED_DEPS)),,--no-dep)

baremetal-arm64: $(TARGET)
	@command -v aarch64-none-elf-gcc >/dev/null 2>&1 || { echo "Required toolchain aarch64-none-elf-gcc missing" >&2; exit 2; }
	@mkdir -p build/baremetal-arm64
	@./$(TARGET) --codegen "$(BAREMETAL_ENTRY_NX)" $(BAREMETAL_CODEGEN_FLAGS) -o build/baremetal-arm64/nxscript_embed.c
	@aarch64-none-elf-gcc -march=armv8-a -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-builtin \
	  -fno-stack-protector -nostdlib -nostartfiles -ffreestanding \
	  -T hal/arm64/nexs.ld $(INCS) -Ikernel/include \
	  $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(ARM64_HAL_SRCS) \
	  build/baremetal-arm64/nxscript_embed.c -lgcc \
	  -o "$(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-arm64/nxscript.elf)"
	@echo "Baremetal arm64: $(if $(BAREMETAL_OUTPUT),$(BAREMETAL_OUTPUT),build/baremetal-arm64/nxscript.elf)"

baremetal-amd64: $(TARGET)
	@command -v x86_64-elf-gcc >/dev/null 2>&1 || { echo "Required toolchain x86_64-elf-gcc missing" >&2; exit 2; }
	@mkdir -p build/baremetal-amd64
	@./$(TARGET) --codegen "$(BAREMETAL_ENTRY_NX)" $(BAREMETAL_CODEGEN_FLAGS) -o build/baremetal-amd64/nxscript_embed.c
	@x86_64-elf-gcc -march=x86-64 -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
	  -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -fno-builtin \
	  -fno-stack-protector -fno-pic -mno-red-zone \
	  -nostdlib -nostartfiles -ffreestanding \
	  -Wl,--no-warn-rwx-segments -T hal/amd64/nexs.ld $(INCS) -Ikernel/include \
	  $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(AMD64_HAL_SRCS) \
	  build/baremetal-amd64/nxscript_embed.c -lgcc \
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
	@ENTRY_NX="services/init.nx"; \
	if [ "$(EMBED_EXAMPLES)" = "1" ]; then \
		ENTRY_NX="/tmp/nexs_sel4_entry.nx"; \
		printf 'exec("services/init.nx")\n' > $$ENTRY_NX; \
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
	./$(TARGET) --codegen services/init.nx    -o $$OUT/nexs_embed_root.c || exit 1; \
	./$(TARGET) --codegen services/fs/init.nx -o $$OUT/nexs_embed_fs.c  2>/dev/null || \
		printf '/* no fs script */\n' > $$OUT/nexs_embed_fs.c; \
	./$(TARGET) --codegen services/pm/init.nx -o $$OUT/nexs_embed_pm.c  2>/dev/null || \
		printf '/* no pm script */\n' > $$OUT/nexs_embed_pm.c; \
	./$(TARGET) --codegen services/tty/init.nx -o $$OUT/nexs_embed_tty.c 2>/dev/null || \
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
# seL4 entry script override (default: services/init.nx)
# ─────────────────────────────────────────────────────────────────────────────
SEL4_ENTRY_NX ?= services/init.nx

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