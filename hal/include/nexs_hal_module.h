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
#include "nexs_checked.h"
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* Stable device-name field width, not a hardware-device count limit.
 * The actual catalog is provider-backed and grows by admitted entries. */
#define HAL_MODULE_NAME_MAX 32

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

/* Registration node belongs to an admitted owner and MUST remain alive
 * until quiescent retirement. No implicit allocation, fixed device catalog
 * or hidden ownership transfer. Its next pointer is immutable once linked. */
typedef struct HalModuleEntry {
    HalModule module;
    _Atomic uint32_t state;
    struct HalModuleEntry *next;
} HalModuleEntry;

/* Trusted native registration only. Code and callbacks are owned by the
 * registering platform; strings cannot inject registry paths. Storage is
 * caller's externally admitted storage, publication is atomic and rejects
 * duplicate names, duplicate node storage and overlapping descriptors.
 * No registry diagnostic grants authority. */
NEXS_API nx_status hal_module_register(HalModuleEntry *storage,
                                        const HalModule *mod);

/* Trusted platform bootstrap, never an untrusted language builtin.
 * Modules have independent atomic admission states; BUSY is nonwaiting.
 * NXEC signed-authority and unload/revocation are still an OPEN contract. */
NEXS_API nx_status hal_module_probe_all(HalModuleType type);

/* Look up a registered module by name; returns NULL if not found. */
NEXS_API const HalModule *hal_module_find(const char *name);

/* Total admitted modules, without a fixed device-count ceiling. */
NEXS_API size_t hal_module_count(void);

/* Registers READ-ONLY module metadata access. Scripts cannot probe, bind
 * IRQs, initialize drivers or acquire capabilities from this catalog.
 * Error is explicit and propagated to runtime initialization. */
NEXS_API nx_status hal_module_register_builtins(void);

#endif
