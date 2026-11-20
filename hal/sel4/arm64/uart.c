/*
 * hal/sel4/arm64/uart.c — seL4 + Microkit UART for aarch64
 * ==========================================================
 * PL011 UART at 0x09000000 (QEMU virt — mapped via nexs_aarch64.system).
 * On seL4, the UART memory region is already mapped by Microkit; no
 * hardware init is needed beyond QEMU's reset state.
 */

#include <microkit.h>
#include "../../include/nexs_hal.h"
#include <stdint.h>

#define UART_BASE    0x09000000UL
#define UART_DR      ((volatile uint32_t *)(UART_BASE + 0x000))
#define UART_FR      ((volatile uint32_t *)(UART_BASE + 0x018))
#define UART_FR_TXFF (1U << 5)
#define UART_FR_RXFE (1U << 4)

void nexs_hal_init(void) {
    microkit_dbg_puts("[NEXS] seL4+Microkit aarch64 ready\n");
}

void nexs_hal_putc(char c) {
    while (*UART_FR & UART_FR_TXFF) {}
    *UART_DR = (uint32_t)(unsigned char)c;
}

int nexs_hal_getc(void) {
    if (*UART_FR & UART_FR_RXFE) return -1;
    return (int)(*UART_DR & 0xFF);
}

void nexs_hal_memory_map(NexsMemMap *map) {
    if (!map) return;
    map->entry_point = 0;
    map->ram_base    = 0;
    map->ram_size    = 0;
    map->uart_base   = UART_BASE;
}
