/* Base-NEXS trusted native module catalog.
 * NXOLMO hardware grants and NXEC resource authority are separate owners:
 * this metadata catalog MUST NOT issue, infer or mint device capabilities.
 * Writers publish immutable descriptors; per-device states serialize probe.
 */
#include "../include/nexs_hal_module.h"
#include "../../core/include/nexs_alloc.h"
#include "../../core/include/nexs_value.h"
#include "../../lang/include/nexs_fn.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    HAL_MODULE_REGISTERED = 1u,
    HAL_MODULE_PROBING = 2u,
    HAL_MODULE_ACTIVE = 3u,
    HAL_MODULE_ABSENT = 4u,
    HAL_MODULE_ERROR = 5u
};

/* Transitional singleton: RuntimeInstance must own this catalog at P2.
 * All entries are supplied by an external admitted owner (no allocations in
 * registration). Append-only and immutable while any reader is active. */
static _Atomic(HalModuleEntry *) s_modules;
static _Atomic uint32_t s_publish_lock;
static _Atomic size_t s_module_count;

static int hal_module_type_valid(HalModuleType type) {
    return type >= HAL_MOD_CHAR && type <= HAL_MOD_BUS;
}

static int hal_module_name_valid(const char name[HAL_MODULE_NAME_MAX]) {
    if (name == NULL)
        return 0;
    for (size_t i = 0u; i < HAL_MODULE_NAME_MAX; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == 0u)
            return i != 0u;
        if (!((c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
              (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
              (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
              c == (unsigned char)'_' || c == (unsigned char)'-'))
            return 0;
    }
    return 0;
}

static const char *hal_module_type_name(HalModuleType type) {
    switch (type) {
    case HAL_MOD_CHAR: return "char";
    case HAL_MOD_BLK: return "blk";
    case HAL_MOD_NET: return "net";
    case HAL_MOD_TIMER: return "timer";
    case HAL_MOD_IRQ: return "irq";
    case HAL_MOD_BUS: return "bus";
    default: return "invalid";
    }
}

static const char *hal_module_state_name(uint32_t state) {
    switch (state) {
    case HAL_MODULE_REGISTERED: return "registered";
    case HAL_MODULE_PROBING: return "probing";
    case HAL_MODULE_ACTIVE: return "active";
    case HAL_MODULE_ABSENT: return "absent";
    case HAL_MODULE_ERROR: return "error";
    default: return "corrupt";
    }
}

static HalModuleEntry *hal_module_lookup(const char *name) {
    size_t admitted = atomic_load_explicit(
        &s_module_count, memory_order_acquire);
    HalModuleEntry *entry = atomic_load_explicit(
        &s_modules, memory_order_acquire);
    for (size_t i = 0u; i < admitted && entry != NULL; ++i) {
        if (strcmp(entry->module.name, name) == 0)
            return entry;
        entry = entry->next;
    }
    /* Concurrent publication can shorten a reader's snapshot. Missing
     * entries deny, never authorize, until a later lookup observes them. */
    return NULL;
}

nx_status hal_module_register(HalModuleEntry *storage,
                                   const HalModule *module) {
    uint32_t expected = 0u;
    if (storage == NULL || module == NULL ||
        !hal_module_name_valid(module->name) ||
        !hal_module_type_valid(module->type) || module->flags != 0u ||
        ((uintptr_t)storage %
         (uintptr_t)_Alignof(HalModuleEntry)) != 0u ||
        nx_pointer_ranges_overlap(storage, sizeof(*storage), module,
                                  sizeof(*module)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;

    /* All storage admission is caller-owned and performed BEFORE the
     * critical section. No default/global Buddy call occurs here. */
    if (!atomic_compare_exchange_strong_explicit(
            &s_publish_lock, &expected, 1u,
            memory_order_acquire, memory_order_relaxed))
        return NX_STATUS_BUSY;

    size_t admitted = atomic_load_explicit(
        &s_module_count, memory_order_relaxed);
    HalModuleEntry *found = atomic_load_explicit(
        &s_modules, memory_order_relaxed);
    for (size_t i = 0u; i < admitted && found != NULL; ++i) {
        if (found == storage ||
            nx_pointer_ranges_overlap(storage, sizeof(*storage),
                                      found, sizeof(*found)) != 0 ||
            strcmp(found->module.name, module->name) == 0) {
            atomic_store_explicit(&s_publish_lock, 0u, memory_order_release);
            return NX_STATUS_CONFLICT;
        }
        found = found->next;
    }
    if (found != NULL) {
        atomic_store_explicit(&s_publish_lock, 0u, memory_order_release);
        return NX_STATUS_CORRUPT; /* Catalogue/cardinality divergence. */
    }
    if (admitted == SIZE_MAX) {
        atomic_store_explicit(&s_publish_lock, 0u, memory_order_release);
        return NX_STATUS_RESOURCE_EXHAUSTED;
    }
    storage->module = *module;
    atomic_init(&storage->state, HAL_MODULE_REGISTERED);
    storage->next = atomic_load_explicit(&s_modules, memory_order_relaxed);
    atomic_store_explicit(&s_modules, storage, memory_order_release);
    (void)atomic_fetch_add_explicit(
        &s_module_count, (size_t)1u, memory_order_release);
    atomic_store_explicit(&s_publish_lock, 0u, memory_order_release);
    return NX_STATUS_OK;
}

static nx_status hal_module_probe_entry(HalModuleEntry *entry) {
    uint32_t previous = atomic_load_explicit(
        &entry->state, memory_order_acquire);
    if (previous == HAL_MODULE_ACTIVE)
        return NX_STATUS_OK; /* No duplicate hardware init. */
    if (previous == HAL_MODULE_PROBING)
        return NX_STATUS_BUSY;
    if (!atomic_compare_exchange_strong_explicit(
            &entry->state, &previous, HAL_MODULE_PROBING,
            memory_order_acq_rel, memory_order_relaxed))
        return NX_STATUS_BUSY;

    /* Never hold the catalog lock while executing unbounded platform code.
     * Only this device is inaccessible for a concurrent probe. */
    int found = entry->module.probe == NULL ||
                entry->module.probe() == 0;
    if (!found) {
        atomic_store_explicit(&entry->state, HAL_MODULE_ABSENT,
                              memory_order_release);
        return NX_STATUS_NOT_FOUND;
    }
    int initialized = entry->module.init == NULL ||
                      entry->module.init() == 0;
    atomic_store_explicit(&entry->state,
                          initialized ? HAL_MODULE_ACTIVE : HAL_MODULE_ERROR,
                          memory_order_release);
    return initialized ? NX_STATUS_OK : NX_STATUS_IO_ERROR;
}

nx_status hal_module_probe_all(HalModuleType type) {
    if (type != 0 && !hal_module_type_valid(type))
        return NX_STATUS_INVALID_ARGUMENT;
    nx_status first_error = NX_STATUS_OK;
    size_t admitted = atomic_load_explicit(
        &s_module_count, memory_order_acquire);
    HalModuleEntry *entry = atomic_load_explicit(
        &s_modules, memory_order_acquire);
    for (size_t i = 0u; i < admitted && entry != NULL; ++i) {
        if (type == 0 || entry->module.type == type) {
            nx_status status = hal_module_probe_entry(entry);
            if (status != NX_STATUS_OK && first_error == NX_STATUS_OK)
                first_error = status;
        }
        entry = entry->next;
    }
    return first_error;
}

const HalModule *hal_module_find(const char *name) {
    if (name == NULL || !hal_module_name_valid(name))
        return NULL;
    HalModuleEntry *entry = hal_module_lookup(name);
    return entry == NULL ? NULL : &entry->module;
}

size_t hal_module_count(void) {
    return atomic_load_explicit(&s_module_count, memory_order_acquire);
}

static Value hal_module_list_builtin(Value *args, int n_args) {
    (void)args;
    if (n_args != 0)
        return val_err((int)NX_STATUS_INVALID_ARGUMENT,
                       "hal_module_list expects no arguments");
    size_t bytes = 0u;
    size_t count = hal_module_count();
    nx_status status = nx_size_mul(count,
                                  HAL_MODULE_NAME_MAX + 40u, &bytes);
    if (status != NX_STATUS_OK ||
        nx_size_add(bytes, (size_t)1u, &bytes) != NX_STATUS_OK)
        return val_err((int)NX_STATUS_OVERFLOW,
                       "HAL catalog formatting overflow");
    char *buffer = nexs_alloc(bytes);
    if (buffer == NULL)
        return val_err((int)NX_STATUS_RESOURCE_EXHAUSTED,
                       "HAL catalog formatting memory unavailable");
    size_t offset = 0u;
    HalModuleEntry *entry = atomic_load_explicit(
        &s_modules, memory_order_acquire);
    for (size_t i = 0u; i < count && entry != NULL; ++i) {
        int written = snprintf(buffer + offset, bytes - offset,
                               "%s [%s] %s\n", entry->module.name,
                               hal_module_type_name(entry->module.type),
                               hal_module_state_name(atomic_load_explicit(
                                   &entry->state, memory_order_acquire)));
        if (written < 0 || (size_t)written >= bytes - offset) {
            nexs_free(buffer, bytes);
            return val_err((int)NX_STATUS_OVERFLOW,
                           "HAL catalog changed while reading");
        }
        offset += (size_t)written;
        entry = entry->next;
    }
    if (entry != NULL) {
        nexs_free(buffer, bytes);
        return val_err((int)NX_STATUS_BUSY,
                       "HAL catalog changed during listing");
    }
    if (offset > 0u && buffer[offset - 1u] == '\n')
        buffer[offset - 1u] = '\0';
    else
        buffer[offset] = '\0';
    Value value = val_str(buffer);
    nexs_free(buffer, bytes);
    return value;
}

nx_status hal_module_register_builtins(void) {
    /* Never publish a callback invoking probe/init into the script VM:
     * no verified NXEC principal, token or device epoch is available here.
     * A read-only diagnostic listing is not an authorization grant. */
    int status = fn_register_builtin_sig(
        "hal_module_list", hal_module_list_builtin,
        "hal_module_list() -> str");
    return status == 0 ? NX_STATUS_OK : NX_STATUS_RESOURCE_EXHAUSTED;
}
