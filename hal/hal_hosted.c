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

#include "include/hal_internal.h"
#include <stdio.h>
#include <stdlib.h>

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

/* =========================================================
   DRIVER REGISTRATION
   ========================================================= */

static HalDriver s_hosted_driver = {
    .name = "hosted-stdio",
    .init = hosted_hal_init,
    .putc = hosted_hal_putc,
    .getc = hosted_hal_getc,
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
