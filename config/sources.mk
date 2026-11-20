# Canonical source module catalog — Base-NEXS only.
# Shared language, compiler, registry and kernel infrastructure are compiled
# from one list. Platform adapters are deliberately selected, not duplicated.

NEXS_SHARED_SRCS := \
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

NEXS_HOSTED_ADAPTER_SRCS := \
  hal/hal_hosted.c \
  hal/common/console.c \
  hal/common/timer.c

NEXS_OS1_ADAPTER_SRCS := \
  hal/common/console.c \
  hal/common/timer.c \
  hal/os1/os1_driver.c \
  hal/os1/tty.c

SRCS := $(NEXS_SHARED_SRCS) $(NEXS_HOSTED_ADAPTER_SRCS)
BAREMETAL_SRCS := $(NEXS_SHARED_SRCS)
KERNEL_SRCS = $(NEXS_KERNEL_SRCS)
ARM64_HAL_SRCS = $(NEXS_BAREMETAL_ARM64_SRCS)
AMD64_HAL_SRCS = $(NEXS_BAREMETAL_AMD64_SRCS)

NEXS_KERNEL_SRCS := \
  kernel/libc_stub.c \
  kernel/proc.c \
  kernel/sched.c \
  kernel/ipc.c \
  kernel/cap.c \
  kernel/sys_brk.c

NEXS_BAREMETAL_ARM64_SRCS := \
  hal/arm64/boot.S \
  hal/arm64/exc_vectors.S \
  hal/arm64/exc_handler.c \
  hal/arm64/uart.c \
  hal/arm64/mmu.c \
  hal/arm64/gic.c \
  hal/arm64/timer.c \
  hal/arm64/fdt.c \
  hal/arm64/ctx_arm64.S

NEXS_BAREMETAL_AMD64_SRCS := \
  hal/amd64/boot.S \
  hal/amd64/isr_stubs.S \
  hal/amd64/uart.c \
  hal/amd64/gdt.c \
  hal/amd64/idt.c \
  hal/amd64/apic.c \
  hal/amd64/mmu.c \
  hal/amd64/acpi.c \
  hal/amd64/ctx_amd64.S

NEXS_BAREMETAL_RISCV64_SRCS := \
  hal/riscv64/boot.S \
  hal/riscv64/uart.c \
  hal/riscv64/timer.c \
  hal/riscv64/ctx_riscv64.S

# The OS1 architecture adapter is selected by OS1_ARCH (amd64 or arm64).
NEXS_OS1_SRCS = $(NEXS_SHARED_SRCS) $(NEXS_OS1_ADAPTER_SRCS) \
                 hal/os1/$(OS1_ARCH)/arch.c
