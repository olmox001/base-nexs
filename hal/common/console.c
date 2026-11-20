/*
 * hal/common/console.c — Centralized HAL Console Logic
 * =====================================================
 */

#include "../include/nexs_hal.h"
#include "../include/hal_internal.h"
#include <stddef.h>

/* Global driver pointer, set by arch initialization */
HalDriver *g_hal_driver = NULL;

void nexs_hal_init(void) {
    if (g_hal_driver && g_hal_driver->init) {
        g_hal_driver->init();
    }
}

void nexs_hal_putc(char c) {
    if (g_hal_driver && g_hal_driver->write_char) {
        g_hal_driver->write_char(c);
    }
}

int nexs_hal_getc(void) {
    if (g_hal_driver && g_hal_driver->read_char) {
        return g_hal_driver->read_char();
    }
    return -1;
}

void nexs_hal_print(const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') nexs_hal_putc('\r');
        nexs_hal_putc(*s++);
    }
}

void nexs_hal_memory_map(NexsMemMap *map) {
    if (g_hal_driver && g_hal_driver->memory_map) {
        g_hal_driver->memory_map(map);
    }
}

void nexs_hal_irq_disable(void) {
    if (g_hal_driver && g_hal_driver->irq_disable) {
        g_hal_driver->irq_disable();
    }
}

void nexs_hal_irq_enable(void) {
    if (g_hal_driver && g_hal_driver->irq_enable) {
        g_hal_driver->irq_enable();
    }
}

void nexs_hal_halt(void) {
    if (g_hal_driver && g_hal_driver->halt) {
        g_hal_driver->halt();
    }
    /* No driver must fail closed, including in unprivileged OS1 userland. */
    __builtin_trap();
}
