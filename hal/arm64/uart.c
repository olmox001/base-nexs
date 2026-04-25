/*
 * hal/arm64/uart.c — PL011 UART Driver
 * ========================================
 * Targets QEMU 'virt' machine (PL011 UART at 0x09000000).
 * For real hardware, adjust UART0_BASE to match your board.
 */

#include "../../hal/include/nexs_hal.h"

#include <stdint.h>
#include <stddef.h>

/* PL011 register offsets */
#define UART0_BASE  0x09000000UL
#define UART_DR     ((volatile uint32_t *)(UART0_BASE + 0x000))
#define UART_FR     ((volatile uint32_t *)(UART0_BASE + 0x018))
#define UART_IBRD   ((volatile uint32_t *)(UART0_BASE + 0x024))
#define UART_FBRD   ((volatile uint32_t *)(UART0_BASE + 0x028))
#define UART_LCRH   ((volatile uint32_t *)(UART0_BASE + 0x02C))
#define UART_CR     ((volatile uint32_t *)(UART0_BASE + 0x030))

/* FR register bits */
#define UART_FR_TXFF  (1U << 5)   /* Transmit FIFO full */
#define UART_FR_RXFE  (1U << 4)   /* Receive FIFO empty */

void nexs_hal_init(void) {
  /* On QEMU virt, the PL011 UART is already usable after reset.
   * Configure for 115200 baud, 8N1 (assuming 24 MHz UART clock). */

  /* Disable UART */
  *UART_CR = 0;

  /* Set baud rate: IBRD = 13, FBRD = 1 for 115200 @ 24 MHz */
  *UART_IBRD = 13;
  *UART_FBRD = 1;

  /* 8 data bits, enable FIFOs */
  *UART_LCRH = (3U << 5) | (1U << 4);

  /* Enable UART, TX, RX */
  *UART_CR = (1U << 0) | (1U << 8) | (1U << 9);
}

void nexs_hal_putc(char c) {
  /* Wait while TX FIFO is full */
  while (*UART_FR & UART_FR_TXFF) {}
  *UART_DR = (uint32_t)(unsigned char)c;
}

int nexs_hal_getc(void) {
  /* Return -1 if RX FIFO is empty */
  if (*UART_FR & UART_FR_RXFE) return -1;
  return (int)(*UART_DR & 0xFF);
}

void nexs_hal_print(const char *s) {
  if (!s) return;
  while (*s) {
    if (*s == '\n') nexs_hal_putc('\r');
    nexs_hal_putc(*s++);
  }
}

void nexs_hal_memory_map(NexsMemMap *map) {
  if (!map) return;
  map->entry_point = (uintptr_t)0x40000000UL;  /* as per nexs.ld */
  map->ram_base    = (uintptr_t)0x40000000UL;
  map->ram_size    = 128 * 1024 * 1024;          /* 128 MB (QEMU default) */
  map->uart_base   = (uintptr_t)UART0_BASE;
}
