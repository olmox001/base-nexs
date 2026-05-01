/*
 * hal/include/nexs_hal_module.h — HAL driver module registry
 *
 * Drivers call hal_module_register() at startup.
 * The kernel queries /hal/modules/<name>/ via the registry.
 */
#ifndef NEXS_HAL_MODULE_H
#define NEXS_HAL_MODULE_H
#pragma once

#include "../../core/include/nexs_common.h"
#include <stdint.h>

#define HAL_MODULE_NAME_MAX 32
#define HAL_MODULE_MAX      64

typedef enum {
    HAL_MOD_CHAR  = 1,   /* character device (UART, console) */
    HAL_MOD_BLK   = 2,   /* block device (disk, flash) */
    HAL_MOD_NET   = 3,   /* network interface */
    HAL_MOD_TIMER = 4,   /* timer / clock source */
    HAL_MOD_IRQ   = 5,   /* interrupt controller */
    HAL_MOD_BUS   = 6,   /* bus controller (PCI, I2C, SPI) */
} HalModuleType;

typedef struct {
    char          name[HAL_MODULE_NAME_MAX];
    HalModuleType type;
    int  (*probe)(void);       /* detect hardware; return 0=found, -1=absent */
    int  (*init)(void);        /* initialise; called if probe succeeds */
    void (*remove)(void);      /* tear down (optional, may be NULL) */
    uint32_t      flags;       /* reserved */
} HalModule;

/* Register a driver. Publishes to /hal/modules/<name>/ registry. */
NEXS_API void hal_module_register(const HalModule *mod);

/* Probe and init all registered drivers matching type (0 = all). */
NEXS_API void hal_module_probe_all(HalModuleType type);

/* Look up a registered module by name; returns NULL if not found. */
NEXS_API const HalModule *hal_module_find(const char *name);

/* Total registered module count. */
NEXS_API int hal_module_count(void);

/* Register builtin that exposes hal_module_list() to NEXS scripts. */
NEXS_API void hal_module_register_builtins(void);

#endif
