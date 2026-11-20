/* ISA-specific user-mode memory ordering; no privileged MMIO/IRQ access. */
#include "../include/nexs_os1_platform.h"

void nexs_os1_arch_fence(void) {
  __asm__ volatile("mfence" ::: "memory");
}

const char *nexs_os1_arch_name(void) {
  return "amd64";
}
