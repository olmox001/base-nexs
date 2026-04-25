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

void nexs_hal_init(void) { /* no-op in hosted mode */ }

void nexs_hal_putc(char c) {
  fputc((unsigned char)c, stdout);
}

int nexs_hal_getc(void) {
  int c = fgetc(stdin);
  return (c == EOF) ? -1 : c;
}

void nexs_hal_print(const char *s) {
  if (s) fputs(s, stdout);
}

void nexs_hal_memory_map(NexsMemMap *map) {
  if (map) {
    map->entry_point = 0;
    map->ram_base    = 0;
    map->ram_size    = 0;
    map->uart_base   = 0;
  }
}

#endif /* !NEXS_BAREMETAL */
