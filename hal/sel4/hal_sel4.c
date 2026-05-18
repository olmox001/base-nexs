/*
 * hal/sel4/hal_sel4.c — seL4 + Microkit HAL Core
 * ==============================================================
 * Implements IRQ no-ops, halt, print, MMU stubs, and timer stubs.
 * Boot initialization, memory map, and UART I/O are implemented
 * per-arch in:
 *   hal/sel4/arm64/uart.c   — PL011 MMIO (aarch64)
 *   hal/sel4/amd64/uart.c   — COM1 port I/O (x86_64)
 *   hal/sel4/riscv64/uart.c — NS16550 MMIO (riscv64)
 */

#include "../../core/include/nexs_alloc.h"
#include "../include/hal_internal.h"
#include "../include/nexs_hal.h"
#include "../include/nexs_mmu.h"
#include "../include/nexs_timer.h"
#include <microkit.h>

/* Allocazione globale del driver (risolve l'errore del linker) */
HalDriver *g_hal_driver = NULL;

/* Dichiarazione delle funzioni architetturali implementate nei file uart.c */
extern void nexs_hal_init(void);
extern void nexs_hal_putc(char c);
extern int nexs_hal_getc(void);
extern void nexs_hal_memory_map(NexsMemMap *map);

/* =========================================================
   IRQ — no-op under Microkit protection domain
   ========================================================= */

void nexs_hal_irq_disable(void) {}
void nexs_hal_irq_enable(void) {}

/* =========================================================
   Halt — spin (seL4 has no PSCI/ACPI from PD context)
   ========================================================= */

void nexs_hal_halt(void) {
  while (1) {
  }
}

/* =========================================================
   Print — built on nexs_hal_putc from arch uart.c
   ========================================================= */

void nexs_hal_print(const char *s) {
  if (!s)
    return;
  while (*s) {
    if (*s == '\n')
      nexs_hal_putc('\r');
    nexs_hal_putc(*s++);
  }
}

/* =========================================================
   Timer stubs
   ========================================================= */

void hal_timer_init(TimerCallback cb) { g_hal_tick_cb = cb; }

void hal_timer_set_hz(uint32_t hz) { (void)hz; }

/* =========================================================
   MMU / MM stubs — address spaces managed by seL4
   ========================================================= */

void mmu_worker_sync(void) {}
void mmu_init(void) {}
int mmu_map_page(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t flags) {
  (void)pid;
  (void)virt;
  (void)phys;
  (void)flags;
  return 0;
}
int mmu_unmap_page(uint32_t pid, vaddr_t virt) {
  (void)pid;
  (void)virt;
  return 0;
}
int mmu_switch_address_space(uint32_t pid) {
  (void)pid;
  return 0;
}
void mmu_destroy_address_space(uint32_t pid) { (void)pid; }
paddr_t mmu_create_address_space(uint32_t pid) {
  (void)pid;
  return 0;
}
void mmu_flush_tlb(vaddr_t virt) { (void)virt; }
void mmu_page_fault(vaddr_t fault_addr, uint64_t err) {
  (void)fault_addr;
  (void)err;
}
paddr_t mmu_virt_to_phys(vaddr_t virt) { return (paddr_t)virt; }

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
  (void)pid;
  (void)virt;
  (void)flags;
  return page_alloc(1) ? 0 : -1;
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
  (void)pid;
  page_free((void *)virt, 1);
  return 0;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t pages,
                 uint32_t flags) {
  (void)phys;
  for (uint32_t i = 0; i < pages; i++) {
    if (mm_alloc_page(pid, virt + i * 4096, flags) != 0)
      return -1;
  }
  return 0;
}

/* =========================================================
   DRIVER REGISTRATION
   ========================================================= */

static HalDriver s_sel4_driver = {
    .name = "sel4-microkit-hal",
    .init =
        nexs_hal_init, /* Routing diretto all'implementazione architetturale */
    .putc =
        nexs_hal_putc, /* Routing diretto all'implementazione architetturale */
    .getc =
        nexs_hal_getc, /* Routing diretto all'implementazione architetturale */
    .halt = nexs_hal_halt,
    .irq_disable = nexs_hal_irq_disable,
    .irq_enable = nexs_hal_irq_enable,
    .memory_map = nexs_hal_memory_map /* Routing diretto all'implementazione
                                         architetturale */
};

__attribute__((constructor)) static void sel4_register_hal(void) {
  g_hal_driver = &s_sel4_driver;
}