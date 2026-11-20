/* ISA-specific user-mode memory ordering; no privileged MMIO/IRQ access. */
#include "../include/nexs_os1_platform.h"

void nexs_os1_arch_fence(void) {
  __asm__ volatile("dmb ish" ::: "memory");
}

const char *nexs_os1_arch_name(void) {
  return "arm64";
}
