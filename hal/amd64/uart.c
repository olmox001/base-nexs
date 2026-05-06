/*
 * hal/amd64/uart.c — 16550 UART via x86 Port I/O
 * ==================================================
 * Targets COM1 (0x3F8) — standard PC serial port.
 */

#include "../../hal/include/nexs_hal.h"

#include <stdint.h>
#include <stddef.h>
#include "../../core/include/nexs_common.h"

#define COM1_PORT 0x3F8

/* =========================================================
   PORT I/O PRIMITIVES
   ========================================================= */

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t val;
  __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

/* =========================================================
   16550 INITIALISATION
   ========================================================= */

void nexs_hal_init(void) {
  outb(COM1_PORT + 1, 0x00); /* Disable interrupts */
  outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
  outb(COM1_PORT + 0, 0x03); /* Divisor lo: 3 → 38400 baud */
  outb(COM1_PORT + 1, 0x00); /* Divisor hi */
  outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
  outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
  outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

/* =========================================================
   I/O
   ========================================================= */

void nexs_hal_putc(char c) {
  /* Wait until transmitter empty */
  while (!(inb(COM1_PORT + 5) & 0x20)) {}
  outb(COM1_PORT, (uint8_t)c);
}

int nexs_hal_getc(void) {
  /* Wait until character is available */
  while (!(inb(COM1_PORT + 5) & 0x01)) {
    __asm__ volatile("pause");
  }
  return (int)inb(COM1_PORT);
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
  map->entry_point = (uintptr_t)0x100000UL;  /* 1 MB (as per nexs.ld) */
  map->ram_base    = (uintptr_t)0x100000UL;
  /* Use at least POOL_SIZE + 16MB for kernel code/data overhead */
  map->ram_size    = POOL_SIZE + (16 * 1024 * 1024);
  map->uart_base   = (uintptr_t)COM1_PORT;
}

void nexs_hal_irq_disable(void) {
  __asm__ volatile("cli" : : : "memory");
}

void nexs_hal_irq_enable(void) {
  __asm__ volatile("sti" : : : "memory");
}

void nexs_hal_halt(void) {
  __asm__ volatile("cli");
  /* QEMU ACPI shutdown: PIIX4 PM port 0x604, value 0x2000 */
  outw(0x604, 0x2000);
  /* Bochs / older QEMU fallback */
  outw(0xB004, 0x2000);
  while (1) { __asm__ volatile("hlt"); }
}
