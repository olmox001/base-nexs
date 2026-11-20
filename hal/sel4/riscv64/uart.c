/*
 * hal/sel4/riscv64/uart.c — seL4 + Microkit UART for riscv64
 * ============================================================
 * NS16550 at 0x10000000 (QEMU virt — mapped via nexs_riscv64.system).
 * QEMU pre-initializes the NS16550; no register setup needed.
 */

#include <microkit.h>
#include "../../include/nexs_hal.h"
#include <stdint.h>

#define NS16550_BASE  0x10000000UL
#define UART_THR      ((volatile uint8_t *)(NS16550_BASE + 0))
#define UART_RBR      ((volatile uint8_t *)(NS16550_BASE + 0))
#define UART_LSR      ((volatile uint8_t *)(NS16550_BASE + 5))
#define UART_LSR_THRE (1U << 5)
#define UART_LSR_DR   (1U << 0)

void nexs_hal_init(void) {
    microkit_dbg_puts("[NEXS] seL4+Microkit riscv64 ready\n");
}

void nexs_hal_putc(char c) {
    while (!(*UART_LSR & UART_LSR_THRE)) {}
    *UART_THR = (uint8_t)c;
}

int nexs_hal_getc(void) {
    if (!(*UART_LSR & UART_LSR_DR)) return -1;
    return (int)*UART_RBR;
}

void nexs_hal_memory_map(NexsMemMap *map) {
    if (!map) return;
    map->entry_point = 0;
    map->ram_base    = 0;
    map->ram_size    = 0;
    map->uart_base   = NS16550_BASE;
}
