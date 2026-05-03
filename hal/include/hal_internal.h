/*
 * hal/include/hal_internal.h — Internal HAL Driver Interface
 * ============================================================
 */

#ifndef HAL_INTERNAL_H
#define HAL_INTERNAL_H

#include "nexs_hal.h"

typedef struct {
    const char *name;
    void (*init)(void);
    void (*putc)(char c);
    int  (*getc)(void);
    void (*halt)(void) __attribute__((noreturn));
    void (*irq_disable)(void);
    void (*irq_enable)(void);
    void (*memory_map)(NexsMemMap *map);
} HalDriver;

/* To be implemented by each architecture and registered at boot */
extern HalDriver *g_hal_driver;

#endif
