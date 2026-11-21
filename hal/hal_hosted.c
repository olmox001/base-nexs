/*
 * hal/hal_hosted.c — HAL stubs for hosted (non-baremetal) builds
 * ================================================================
 * Provides thin wrappers over libc stdio so that hal/bc/nexs_hal_bc.c
 * can use nexs_hal_putc / nexs_hal_getc / nexs_hal_print in hosted builds.
 * On bare-metal these symbols come from hal/arm64/uart.c or hal/amd64/uart.c.
 */

#ifndef NEXS_BAREMETAL

#include "include/nexs_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "include/hal_internal.h"
#include "include/nexs_mmu.h"
#include "../../registry/include/nexs_registry.h"
#include "../../registry/include/nexs_ipc_transport.h"
#include "../../core/include/nexs_value.h"
#include "../../core/include/nexs_alloc.h"
#include <stdio.h>
#include <stdlib.h>

void nexs_ipc_pipe_abort(int ends[2]) {
  if (ends == NULL)
    return;
  for (size_t i = 0U; i < 2U; ++i) {
    if (ends[i] >= 0)
      close(ends[i]);
    ends[i] = -1;
  }
}
/* Actual hosted POSIX pipe provider, never a kernel/VM substitute. */
int nexs_ipc_pipe_open(int ends[2]) {
  if (ends == NULL)
    return -1;
  ends[0] = -1;
  ends[1] = -1;
  if (pipe(ends) != 0)
    return -1;
  int flags = fcntl(ends[1], F_GETFL, 0);
  if (flags < 0 || fcntl(ends[1], F_SETFL, flags | O_NONBLOCK) != 0) {
    nexs_ipc_pipe_abort(ends);
    return -1;
  }
  return 0;
}



static void hosted_hal_init(void) { /* no-op in hosted mode */ }

static void hosted_hal_putc(char c) {
  fputc((unsigned char)c, stdout);
}

static int hosted_hal_getc(void) {
  int c = fgetc(stdin);
  return (c == EOF) ? -1 : c;
}

static void hosted_hal_memory_map(NexsMemMap *map) {
  if (map) {
    map->entry_point = 0;
    map->ram_base    = 0;
    map->ram_size    = 0;
    map->uart_base   = 0;
  }
}

static void hosted_hal_irq_disable(void) { /* no-op on hosted */ }
static void hosted_hal_irq_enable(void)  { /* no-op on hosted */ }

static void hosted_hal_halt(void) __attribute__((noreturn));
static void hosted_hal_halt(void) {
  _Exit(0);
}

void mmu_worker_sync(void) {
    /* No-op on hosted mode: registry updates are synchronous here */
}

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
    void *p = page_alloc(1);
    if (!p) return -1;
    /* page_alloc (in pager.c) already tracks in registry for Hosted */
    return 0;
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
    /* In hosted, virt is the pointer itself */
    page_free((void *)virt, 1);
    return 0;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t pages, uint32_t flags) {
    for (uint32_t i = 0; i < pages; i++) {
        if (mm_alloc_page(pid, virt + i * 4096, flags) != 0) return -1;
    }
    return 0;
}

paddr_t mmu_virt_to_phys(vaddr_t virt) {
    return (paddr_t)virt;
}

void mmu_init(void) {}
int  mmu_switch_address_space(uint32_t pid) { return 0; }
void mmu_destroy_address_space(uint32_t pid) {}
paddr_t mmu_create_address_space(uint32_t pid) { return 0; }
void mmu_flush_tlb(vaddr_t virt) {}
void mmu_page_fault(vaddr_t fault_addr, uint64_t err) {}

/* =========================================================
   DRIVER REGISTRATION
   ========================================================= */

static HalDriver s_hosted_driver = {
    .name = "hosted-stdio",
    .init = hosted_hal_init,
    .write_char = hosted_hal_putc,
    .read_char = hosted_hal_getc,
    .halt = hosted_hal_halt,
    .irq_disable = hosted_hal_irq_disable,
    .irq_enable = hosted_hal_irq_enable,
    .memory_map = hosted_hal_memory_map
};

__attribute__((constructor))
static void hosted_register_hal(void) {
    g_hal_driver = &s_hosted_driver;
}

#endif /* !NEXS_BAREMETAL */
