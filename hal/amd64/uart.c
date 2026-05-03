/*
 * hal/amd64/uart.c — 16550 UART via x86 Port I/O
 * ==================================================
 * Targets COM1 (0x3F8) — standard PC serial port.
 */

#include "../../hal/include/nexs_hal.h"

#include <stddef.h>
#include <stdint.h>

#define COM1_PORT 0x3F8

/* =========================================================
   PORT I/O PRIMITIVES
   ========================================================= */

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t val;
  __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

/* =========================================================
   16550 INITIALISATION
   ========================================================= */

#include "../include/hal_internal.h"

/* =========================================================
   16550 INITIALISATION
   ========================================================= */

static void amd64_hal_init(void) {
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

static void amd64_hal_putc(char c) {
  /* Wait until transmitter empty */
  while (!(inb(COM1_PORT + 5) & 0x20)) {
  }
  outb(COM1_PORT, (uint8_t)c);
}

static int amd64_hal_getc(void) {
  if (!(inb(COM1_PORT + 5) & 0x01))
    return -1;
  return (int)inb(COM1_PORT);
}

static void amd64_hal_memory_map(NexsMemMap *map) {
  if (!map)
    return;
  map->entry_point = (uintptr_t)0x100000UL; /* 1 MB (as per nexs.ld) */
  map->ram_base = (uintptr_t)0x100000UL;
  map->ram_size = 256 * 1024 * 1024; /* conservative 4 MB */
  map->uart_base = (uintptr_t)COM1_PORT;
}

static void amd64_hal_irq_disable(void) {
  __asm__ volatile("cli" : : : "memory");
}

static void amd64_hal_irq_enable(void) {
  __asm__ volatile("sti" : : : "memory");
}

static void amd64_hal_halt(void) __attribute__((noreturn));
static void amd64_hal_halt(void) {
  __asm__ volatile("cli");
  /* QEMU ACPI shutdown: PIIX4 PM port 0x604, value 0x2000 */
  outw(0x604, 0x2000);
  /* Bochs / older QEMU fallback */
  outw(0xB004, 0x2000);
  while (1) {
    __asm__ volatile("hlt");
  }
}

/* =========================================================
   DRIVER REGISTRATION
   ========================================================= */

static HalDriver s_amd64_driver = {.name = "amd64-uart",
                                   .init = amd64_hal_init,
                                   .putc = amd64_hal_putc,
                                   .getc = amd64_hal_getc,
                                   .halt = amd64_hal_halt,
                                   .irq_disable = amd64_hal_irq_disable,
                                   .irq_enable = amd64_hal_irq_enable,
                                   .memory_map = amd64_hal_memory_map};

/* Use a constructor to register the driver at startup */
__attribute__((constructor)) static void amd64_register_hal(void) {
  g_hal_driver = &s_amd64_driver;
}
