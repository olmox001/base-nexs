# =============================================================================
# NEXS — Top-Level Makefile
# =============================================================================
# Modular layout: core/ registry/ lang/ sys/ runtime/ compiler/ hal/
# Legacy code: old/src/ (not compiled)
# =============================================================================

CC     = gcc
TARGET = nexs

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
            -fsanitize=address,undefined -DPOOL_$(POOL_PROFILE) $(INCS)

# All runtime source files (no src/)
SRCS = \
  core/buddy.c \
  core/pager.c \
  core/value.c \
  core/dynarray.c \
  core/utils.c \
  registry/registry.c \
  registry/reg_ipc.c \
  lang/fn_table.c \
  lang/lexer.c \
  lang/parser.c \
  lang/eval.c \
  lang/builtins.c \
  sys/sysio.c \
  sys/sysproc.c \
  runtime/runtime.c \
  runtime/nexs_line.c \
  runtime/main.c \
  compiler/codegen.c \
  compiler/driver.c \
  compiler/dep_scan.c \
  hal/bc/nexs_hal_bc.c \
  hal/module/nexs_hal_module.c \
  hal/hal_hosted.c \
  hal/common/console.c \
  hal/common/timer.c

# Baremetal doesn't use the hosted HAL, but it keeps the AOT compiler
BAREMETAL_SRCS = \
  core/buddy.c \
  core/pager.c \
  core/value.c \
  core/dynarray.c \
  core/utils.c \
  registry/registry.c \
  registry/reg_ipc.c \
  lang/fn_table.c \
  lang/lexer.c \
  lang/parser.c \
  lang/eval.c \
  lang/builtins.c \
  sys/sysio.c \
  sys/sysproc.c \
  runtime/runtime.c \
  runtime/nexs_line.c \
  runtime/main.c \
  compiler/codegen.c \
  compiler/driver.c \
  compiler/dep_scan.c \
  hal/bc/nexs_hal_bc.c \
  hal/module/nexs_hal_module.c

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

.PHONY: all clean test debug run compile-test baremetal-amd64 baremetal-arm64 iso-amd64 run-iso

# ─────────────────────────────────────────────────────────────────────────────
# Default target: hosted interpreter
# ─────────────────────────────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)
	@echo "Build OK -> ./$(TARGET)"

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ─────────────────────────────────────────────────────────────────────────────
# Debug build with AddressSanitizer + UBSan
# ─────────────────────────────────────────────────────────────────────────────
debug: $(SRCS) $(HDRS)
	$(CC) $(DBGFLAGS) -o $(TARGET)_dbg $(SRCS)
	@echo "Debug build -> ./$(TARGET)_dbg"

# Alias
nexs_dbg: debug

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────
test: $(TARGET)
	@echo "=== Test: basic ==="
	@printf 'x = 42\nout x\n' | ./$(TARGET)
	@printf 'out 1 + 2 * 3\n' | ./$(TARGET)
	@printf 'fn sq(n) { ret n * n }\nout sq(7)\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: array ==="
	@printf 'arr[0] = 10\narr[1] = 20\narr[2] = arr[0] + arr[1]\nout arr[2]\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: ls ==="
	@printf 'ls /sys\n' | ./$(TARGET)
	@echo ""
	@echo "All tests completed"

# ─────────────────────────────────────────────────────────────────────────────
# Compile a NEXS script to a Linux binary
# ─────────────────────────────────────────────────────────────────────────────
compile-test: $(TARGET)
	@mkdir -p build/linux-amd64
	@if [ -f example/test.nx ]; then \
		./$(TARGET) --compile example/test.nx --target linux-amd64 -o build/linux-amd64/test; \
	else \
		echo "(skipped: example/test.nx not found)"; \
	fi

# ─────────────────────────────────────────────────────────────────────────────
# Cross-compile targets
# ─────────────────────────────────────────────────────────────────────────────
linux-arm64: $(TARGET)
	@if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then \
		mkdir -p build/linux-arm64 && \
		aarch64-linux-gnu-gcc -march=armv8-a -DNEXS_LINUX -DPOOL_$(POOL_PROFILE) -DNEXS_HOST_TOOL \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			$(INCS) $(SRCS) -o build/linux-arm64/nexs; \
		echo "Cross-compiled -> build/linux-arm64/nexs"; \
	else \
		echo "aarch64-linux-gnu-gcc not found, skipping linux-arm64"; \
	fi

linux-amd64: $(TARGET)
	@if command -v $(LINUX_AMD64_CC) >/dev/null 2>&1; then \
		mkdir -p build/linux-amd64 && \
		$(LINUX_AMD64_CC) -march=x86_64 -DNEXS_LINUX -DPOOL_$(POOL_PROFILE) -DHOST_OS_$(HOST_PLATFORM) -DNEXS_HOST_TOOL \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			$(INCS) $(SRCS) -o build/linux-amd64/nexs; \
		echo "Cross-compiled -> build/linux-amd64/nexs"; \
	else \
		echo "$(LINUX_AMD64_CC) not found, skipping linux-amd64"; \
	fi

macos-amd64: $(TARGET)
	@if command -v $(MACOS_AMD64_CC) >/dev/null 2>&1; then \
		mkdir -p build/macos-amd64 && \
		$(MACOS_AMD64_CC) -DNEXS_MACOS -DPOOL_$(POOL_PROFILE) -DHOST_OS_$(HOST_PLATFORM) -DNEXS_HOST_TOOL \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			$(INCS) $(SRCS) -o build/macos-amd64/nexs; \
		echo "Compiled -> build/macos-amd64/nexs"; \
	else \
		echo "$(MACOS_AMD64_CC) not found, skipping macos-amd64"; \
	fi

# Kernel sources
KERNEL_SRCS = \
  kernel/libc_stub.c \
  kernel/proc.c \
  kernel/sched.c \
  kernel/ipc.c \
  kernel/cap.c \
  kernel/sys_brk.c

ARM64_HAL_SRCS = \
  hal/arm64/boot.S \
  hal/arm64/exc_vectors.S \
  hal/arm64/exc_handler.c \
  hal/arm64/uart.c \
  hal/arm64/mmu.c \
  hal/arm64/gic.c \
  hal/arm64/timer.c \
  hal/arm64/fdt.c \
  kernel/ctx_arm64.S

AMD64_HAL_SRCS = \
  hal/amd64/boot.S \
  hal/amd64/isr_stubs.S \
  hal/amd64/uart.c \
  hal/amd64/gdt.c \
  hal/amd64/idt.c \
  hal/amd64/apic.c \
  hal/amd64/mmu.c \
  hal/amd64/acpi.c \
  kernel/ctx_amd64.S

baremetal-arm64: $(TARGET)
	@if command -v aarch64-none-elf-gcc >/dev/null 2>&1; then \
		mkdir -p build/baremetal-arm64 && \
		aarch64-none-elf-gcc -march=armv8-a -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			-nostdlib -nostartfiles -ffreestanding \
			-T hal/arm64/nexs.ld \
			$(INCS) -Ikernel/include -Ihal/include \
			$(BAREMETAL_SRCS) $(KERNEL_SRCS) $(ARM64_HAL_SRCS) \
			-o build/baremetal-arm64/nexs.elf && \
		echo "Cross-compiled -> build/baremetal-arm64/nexs.elf"; \
	else \
		echo "aarch64-none-elf-gcc not found, skipping baremetal-arm64"; \
	fi

baremetal-amd64: $(TARGET)
	@if command -v x86_64-elf-gcc >/dev/null 2>&1; then \
		mkdir -p build/baremetal-amd64 && \
		x86_64-elf-gcc -march=x86-64 -DNEXS_BAREMETAL -DPOOL_$(POOL_PROFILE) \
			-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
			-fno-stack-protector -fno-pic -mno-red-zone \
			-nostdlib -nostartfiles -ffreestanding \
			-Wl,--no-warn-rwx-segments \
			-T hal/amd64/nexs.ld \
			$(INCS) -Ikernel/include -Ihal/include \
			$(BAREMETAL_SRCS) $(KERNEL_SRCS) $(AMD64_HAL_SRCS) \
			-o build/baremetal-amd64/nexs.elf && \
		echo "[+] Baremetal ELF creato con strap header."; \
	else \
		echo "x86_64-elf-gcc not found, skipping baremetal-amd64"; \
	fi

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
	       build/baremetal-arm64 build/baremetal-amd64 \
	       build/iso_root build/nexs-amd64.iso
	@echo "Clean done"