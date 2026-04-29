# =============================================================================
# NEXS — Top-Level Makefile
# =============================================================================
# Modular layout: core/ registry/ lang/ sys/ runtime/ compiler/ hal/
# Legacy code: old/src/ (not compiled)
# =============================================================================

CC     = gcc
TARGET = nexs

# Include paths
INCS = \
  -Icore/include \
  -Iregistry/include \
  -Ilang/include \
  -Isys/include \
  -Iruntime/include \
  -Icompiler/include \
  -Ihal/include

# Production flags
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter $(INCS)

# Debug flags
DBGFLAGS = -O0 -g -std=c11 -Wall -Wextra -Wno-unused-parameter \
           -fsanitize=address,undefined $(INCS)

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
  runtime/main.c \
  compiler/codegen.c \
  compiler/driver.c \
  compiler/dep_scan.c \
  hal/bc/nexs_hal_bc.c \
  hal/hal_hosted.c

# Baremetal doesn't use the hosted HAL, but it keeps the AOT compiler (to be powered by tinycc)
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
  runtime/main.c \
  compiler/codegen.c \
  compiler/driver.c \
  compiler/dep_scan.c \
  hal/bc/nexs_hal_bc.c

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
  compiler/include/nexs_compiler.h \
  compiler/targets.h \
  hal/include/nexs_hal.h \
  hal/include/nexs_hal_bc.h \
  include/nexs.h

.PHONY: all clean test debug run compile-test

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
# Tests (same as original test suite)
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
	@echo "=== Test: strings ==="
	@printf 'out "hello" + " world"\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: registry ==="
	@printf 'reg /env/ver = 1\nout reg /env/ver\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: ls ==="
	@printf 'ls /sys\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: IPC ==="
	@printf 'sendmessage /test/q 42\nout msgpending /test/q\nout receivemessage /test/q\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: pointer ==="
	@printf 'reg /env/x = 99\nptr /env/px = /env/x\nout deref /env/px\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test: script file ==="
	@if [ -f test.nx ]; then ./$(TARGET) test.nx; fi
	@echo ""
	@echo "All tests completed"

# ─────────────────────────────────────────────────────────────────────────────
# Compile a NEXS script to a Linux binary (test the compiler pipeline)
# ─────────────────────────────────────────────────────────────────────────────
compile-test: $(TARGET)
	@mkdir -p build/linux-amd64
	@if [ -f example/test.nx ]; then \
	  ./$(TARGET) --compile example/test.nx --target linux-amd64 -o build/linux-amd64/test; \
	else \
	  echo "(skipped: example/test.nx not found)"; \
	fi

# ─────────────────────────────────────────────────────────────────────────────
# Cross-compile targets (guarded by cross-compiler availability)
# ─────────────────────────────────────────────────────────────────────────────
linux-arm64: $(TARGET)
	@if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then \
	  mkdir -p build/linux-arm64 && \
	  aarch64-linux-gnu-gcc -march=armv8-a -DNEXS_LINUX \
	    -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	    $(INCS) $(SRCS) -o build/linux-arm64/nexs; \
	  echo "Cross-compiled -> build/linux-arm64/nexs"; \
	else \
	  echo "aarch64-linux-gnu-gcc not found, skipping linux-arm64"; \
	fi

plan9-amd64: $(TARGET)
	@echo "Plan 9 target: requires Plan 9 toolchain (9c/9l)"
	@echo "Skipping (not implemented for POSIX cross-compilation)"

# Kernel sources (shared between amd64 and arm64 baremetal)
KERNEL_SRCS = \
  kernel/libc_stub.c \
  kernel/proc.c \
  kernel/sched.c

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

AMD64_INCS = $(INCS) -Ikernel/include -Ihal/include

baremetal-arm64: $(TARGET)
	@if command -v aarch64-none-elf-gcc >/dev/null 2>&1; then \
	  mkdir -p build/baremetal-arm64 && \
	  aarch64-none-elf-gcc -march=armv8-a -DNEXS_BAREMETAL \
	    -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	    -nostdlib -nostartfiles -ffreestanding \
	    -T hal/arm64/nexs.ld \
	    $(INCS) -Ikernel/include -Ihal/include \
	    $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(ARM64_HAL_SRCS) \
	    -o build/baremetal-arm64/nexs.elf; \
	  echo "Cross-compiled -> build/baremetal-arm64/nexs.elf"; \
	else \
	  echo "aarch64-none-elf-gcc not found, skipping baremetal-arm64"; \
	fi

baremetal-amd64: $(TARGET)
	@if command -v x86_64-elf-gcc >/dev/null 2>&1; then \
	  mkdir -p build/baremetal-amd64 && \
	  x86_64-elf-gcc -march=x86-64 -mno-red-zone -DNEXS_BAREMETAL \
	    -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
	    -nostdlib -nostartfiles -ffreestanding \
	    -T hal/amd64/nexs.ld \
	    $(INCS) -Ikernel/include -Ihal/include \
	    $(BAREMETAL_SRCS) $(KERNEL_SRCS) $(AMD64_HAL_SRCS) \
	    -o build/baremetal-amd64/nexs.elf; \
	  echo "Cross-compiled -> build/baremetal-amd64/nexs.elf"; \
	else \
	  echo "x86_64-elf-gcc not found, skipping baremetal-amd64"; \
	fi



# ─────────────────────────────────────────────────────────────────────────────
# Run the REPL
# ─────────────────────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

# ─────────────────────────────────────────────────────────────────────────────
# Clean
# ─────────────────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(TARGET)_dbg $(OBJS)
	rm -rf build/linux-amd64 build/linux-arm64 \
	       build/baremetal-arm64 build/baremetal-amd64
	@echo "Clean done"
