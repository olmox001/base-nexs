/*
 * hal/sel4/amd64/uart.c — seL4 + Microkit UART for x86_64
 * =========================================================
 * COM1 (0x3F8) via seL4 I/O Port Capability.
 * Native inb/outb instructions trigger GPF in Ring 3.
 * I/O must be routed through microkit_x86_ioport_* system calls.
 */

#include "../../include/nexs_hal.h"
#include <microkit.h>
#include <stdint.h>

#define COM1_PORT 0x3F8
#define UART_IOPORT_ID                                                         \
  0 /* Questo ID corrisponde a <ioport id="0"> nel file XML */

static inline uint8_t inb(uint16_t port) {
  /* Delega la lettura all'interfaccia di sistema Microkit/seL4 */
  return (uint8_t)microkit_x86_ioport_read_8(UART_IOPORT_ID, (seL4_Word)port);
}

static inline void outb(uint16_t port, uint8_t val) {
  /* Delega la scrittura all'interfaccia di sistema Microkit/seL4 */
  microkit_x86_ioport_write_8(UART_IOPORT_ID, (seL4_Word)port, (seL4_Word)val);
}

void nexs_hal_init(void) {
  outb(COM1_PORT + 1, 0x00); /* disable interrupts */
  outb(COM1_PORT + 3, 0x80); /* DLAB on */
  outb(COM1_PORT + 0, 0x03); /* 38400 baud lo */
  outb(COM1_PORT + 1, 0x00); /* baud hi */
  outb(COM1_PORT + 3, 0x03); /* 8N1 */
  outb(COM1_PORT + 2, 0xC7); /* FIFO enable */
  outb(COM1_PORT + 4, 0x0B); /* RTS/DSR */
  microkit_dbg_puts("[NEXS] seL4+Microkit x86_64 ready\n");
}

void nexs_hal_putc(char c) {
  while (!(inb(COM1_PORT + 5) & 0x20)) {
  }
  outb(COM1_PORT, (uint8_t)c);
}

int nexs_hal_getc(void) {
  if (!(inb(COM1_PORT + 5) & 0x01))
    return -1;
  return (int)inb(COM1_PORT);
}

void nexs_hal_memory_map(NexsMemMap *map) {
  if (!map)
    return;
  map->entry_point = 0;
  map->ram_base = 0;
  map->ram_size = 0;
  map->uart_base = COM1_PORT;
}