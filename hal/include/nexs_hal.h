/*
 * nexs_hal.h — NEXS Hardware Abstraction Layer
 * ===============================================
 * Platform-independent interface for bare-metal targets.
 * On hosted platforms these are no-ops or redirected to libc.
 */

#ifndef NEXS_HAL_H
#define NEXS_HAL_H
#pragma once

#include "../../core/include/nexs_common.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   MEMORY MAP DESCRIPTOR
   ========================================================= */

typedef struct {
  uintptr_t entry_point; /* physical/virtual address of _start */
  uintptr_t ram_base;    /* start of usable RAM */
  size_t    ram_size;    /* total usable RAM in bytes */
  uintptr_t uart_base;   /* MMIO base address of UART */
} NexsMemMap;

/* =========================================================
   HAL API
   ========================================================= */

/* Initialise the HAL (UART, clocks, etc.) — called from boot.S */
NEXS_API void nexs_hal_init(void);

/* Transmit a single character via UART */
NEXS_API void nexs_hal_putc(char c);

/* Receive a single character from UART; returns -1 if none available */
NEXS_API int  nexs_hal_getc(void);

/* Transmit a NUL-terminated string via UART */
NEXS_API void nexs_hal_print(const char *s);

/* Fill in the platform memory map */
NEXS_API void nexs_hal_memory_map(NexsMemMap *map);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_HAL_H */
