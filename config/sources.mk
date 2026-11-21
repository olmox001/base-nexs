# Canonical source module catalog — Base-NEXS only.
# Shared language, compiler, registry and kernel infrastructure are compiled
# from one list. Platform adapters are deliberately selected, not duplicated.

NEXS_SHARED_SRCS := \
  core/checked.c \
  core/status.c \
  core/types.c \
  core/result.c \
  core/crypto/crc32c.c \
  core/crypto/crc64.c \
  core/crypto/sha256.c \
  core/buddy.c \
  core/pager.c \
  core/value.c \
  core/dynarray.c \
  core/utils.c \
  state/authority/capability.c \
  state/authority/hardware/capability.c \
  state/memory/buffer/buffer.c \
  state/memory/buffer/capability_adapter.c \
  state/storage/block/device.c \
  state/storage/block/memory.c \
  state/storage/block/memory_device.c \
  state/storage/journal/wal.c \
  state/storage/9p/9p_frame.c \
  state/registry/state_store.c \
  state/registry/nxreg.c \
  state/registry/capability_adapter.c \
  state/registry/snapshot_pool.c \
  state/registry/schema.c \
  state/registry/migration.c \
  registry/registry.c \
  registry/view.c \
  registry/reg_ipc.c \
  lang/fn_table.c \
  lang/lexer.c \
  lang/parser.c \
  lang/eval.c \
  lang/builtins.c \
  extensions/textbuf/textbuf.c \
  sys/fd_owner.c \
  sys/sysio.c \
  sys/sysproc.c \
  runtime/runtime.c \
  runtime/operation_budget.c \
  runtime/operation_metrics.c \
  runtime/operation_catalog.c \
  runtime/cycle_counter_fallback.c \
  runtime/clock_unavailable.c \
  runtime/nexs_line.c \
  runtime/repl.c \
  runtime/main.c \
  compiler/codegen.c \
  compiler/dep_scan.c \
  hal/bc/nexs_hal_bc.c \
  hal/module/nexs_hal_module.c

NEXS_HOSTED_ADAPTER_SRCS := \
  runtime/clock_hosted.c \
  compiler/driver.c \
  runtime/source_hosted.c \
  hal/hal_hosted.c \
  hal/common/console.c \
  hal/common/timer.c

NEXS_OS1_ADAPTER_SRCS := \
  compiler/compile_unavailable.c \
  hal/common/pipe_transport_unavailable.c \
  hal/common/console.c \
  hal/common/timer.c \
  hal/os1/os1_driver.c \
  hal/os1/tty.c

BAREMETAL_SRCS := $(NEXS_SHARED_SRCS) compiler/compile_unavailable.c \
  hal/common/pipe_transport_unavailable.c

# Architecture provider selection; runtime code contains no ISA #ifdef.
ifeq ($(shell uname -m),x86_64)
  NEXS_HOSTED_ADAPTER_SRCS += runtime/cycle_counter_hosted_amd64.c
endif

# Complete ISA provider selection BEFORE capturing the hosted source list.
SRCS := $(NEXS_SHARED_SRCS) $(NEXS_HOSTED_ADAPTER_SRCS)

# Hosted AOT links the canonical runtime source list, excluding the CLI,
# build-only compiler and generated code (which defines its own entrypoint).
# AOT uses the DESTINATION ISA, not the build machine ISA. Keep the
# universal fallback and select the strong AMD64 counter only for AMD64
# output; ARM64 output never contains an x86 RDTSCP translation unit.
# Recursive expansion reads AOT_HOST_TARGET from the selected build goal.
NEXS_AOT_HOSTED_SRCS = $(filter-out runtime/main.c compiler/codegen.c compiler/dep_scan.c,$(NEXS_SHARED_SRCS)) \
  $(filter-out compiler/driver.c runtime/cycle_counter_hosted_amd64.c,$(NEXS_HOSTED_ADAPTER_SRCS)) \
  $(if $(filter macos-amd64 linux-amd64,$(AOT_HOST_TARGET)),runtime/cycle_counter_hosted_amd64.c,)
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

# One seL4 privileged kernel source closure, independent of PD count.
# Neither target may redefine it or silently omit provider implementations.
NEXS_SEL4_PRIVILEGED_SRCS := \
  kernel/msg.c \
  kernel/syscall.c \
  kernel/vfs.c \
  kernel/blk.c \
  kernel/journal.c

# The OS1 architecture adapter is selected by OS1_ARCH (amd64 or arm64).
NEXS_OS1_SRCS = $(NEXS_SHARED_SRCS) $(NEXS_OS1_ADAPTER_SRCS) \
                 hal/os1/$(OS1_ARCH)/arch.c
