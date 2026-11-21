/* Production-linked Base-NEXS HAL module authority boundary.
 * Real registry, builtin table, allocator and native registration.
 * The untrusted VM may enumerate metadata but may NOT probe hardware.
 */
#include "nexs_hal_module.h"
#include "nexs_runtime.h"
#include "nexs_fn.h"
#include "nexs_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#define CHECK(x) do { if (!(x)) { \
    (void)fprintf(stderr, "HAL_MODULE_FAILURE line=%d: %s\n", \
                  __LINE__, #x); return 1; } } while (0)
enum { EXTRA = 96 };
static HalModuleEntry entries[EXTRA];
static HalModuleEntry malicious_entry;
static _Atomic uint32_t probe_calls;
static _Atomic uint32_t init_calls;
static _Atomic uint32_t nested_status;
static HalModuleEntry concurrent_entry;
static HalModuleEntry during_probe_entry;
static pthread_mutex_t barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier = PTHREAD_COND_INITIALIZER;
static int probe_entered;
static int probe_released;
static nx_status thread_result;

static int concurrent_probe(void) {
    if (pthread_mutex_lock(&barrier_lock) != 0)
        return -1;
    probe_entered = 1;
    (void)pthread_cond_broadcast(&barrier);
    while (probe_released == 0) {
        if (pthread_cond_wait(&barrier, &barrier_lock) != 0) {
            (void)pthread_mutex_unlock(&barrier_lock);
            return -1;
        }
    }
    return pthread_mutex_unlock(&barrier_lock);
}
static void *run_parallel_probe(void *unused) {
    (void)unused;
    thread_result = hal_module_probe_all(HAL_MOD_BLK);
    return NULL;
}
static int test_concurrent_admission(void) {
    HalModule module = {0};
    (void)snprintf(module.name, sizeof(module.name), "concurrent_blk");
    module.type = HAL_MOD_BLK;
    module.probe = concurrent_probe;
    CHECK(hal_module_register(&concurrent_entry, &module) == NX_STATUS_OK);
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, run_parallel_probe, NULL) == 0);
    CHECK(pthread_mutex_lock(&barrier_lock) == 0);
    while (!probe_entered) {
        CHECK(pthread_cond_wait(&barrier, &barrier_lock) == 0);
    }
    CHECK(pthread_mutex_unlock(&barrier_lock) == 0);
    /* Another caller may inspect metadata and register another independent
     * device. It cannot reenter the hardware probe already in progress. */
    CHECK(hal_module_probe_all(HAL_MOD_BLK) == NX_STATUS_BUSY);
    module = (HalModule){0};
    (void)snprintf(module.name, sizeof(module.name), "hot_registered");
    module.type = HAL_MOD_TIMER;
    CHECK(hal_module_register(&during_probe_entry, &module) == NX_STATUS_OK);
    CHECK(hal_module_count() == EXTRA + 2u);
    CHECK(pthread_mutex_lock(&barrier_lock) == 0);
    probe_released = 1;
    CHECK(pthread_cond_broadcast(&barrier) == 0);
    CHECK(pthread_mutex_unlock(&barrier_lock) == 0);
    CHECK(pthread_join(worker, NULL) == 0 && thread_result == NX_STATUS_OK);
    CHECK(hal_module_probe_all(HAL_MOD_BLK) == NX_STATUS_OK);
    return 0;
}

static int test_probe(void) {
    (void)atomic_fetch_add_explicit(&probe_calls, 1u, memory_order_relaxed);
    atomic_store_explicit(&nested_status, hal_module_probe_all(HAL_MOD_BLK),
                          memory_order_release);
    return 0;
}
static int test_init(void) {
    (void)atomic_fetch_add_explicit(&init_calls, 1u, memory_order_relaxed);
    return 0;
}
static int test_reject_malicious_descriptors(void) {
    HalModule invalid = {0};
    invalid.type = HAL_MOD_BLK;
    CHECK(hal_module_register(&malicious_entry, NULL) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(hal_module_register(NULL, &invalid) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(hal_module_register(&malicious_entry, &invalid) ==
          NX_STATUS_INVALID_ARGUMENT);
    (void)snprintf(invalid.name, sizeof(invalid.name), "escape/../dev");
    CHECK(hal_module_register(&malicious_entry, &invalid) ==
          NX_STATUS_INVALID_ARGUMENT);
    (void)snprintf(invalid.name, sizeof(invalid.name), "device_01");
    invalid.type = (HalModuleType)99;
    CHECK(hal_module_register(&malicious_entry, &invalid) ==
          NX_STATUS_INVALID_ARGUMENT);
    invalid.type = HAL_MOD_BLK;
    invalid.flags = 1u;
    CHECK(hal_module_register(&malicious_entry, &invalid) ==
          NX_STATUS_INVALID_ARGUMENT);
    invalid.flags = 0u;
    memset(invalid.name, 'x', sizeof(invalid.name));
    CHECK(hal_module_register(&malicious_entry, &invalid) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(hal_module_count() == 0u);
    return 0;
}
static int test_catalog_and_vm_denial(void) {
    NexsFnDef *script_list = fn_lookup("hal_module_list");
    CHECK(script_list != NULL && script_list->is_builtin != 0 &&
          script_list->builtin_fn != NULL);
    CHECK(fn_lookup("hal_module_probe") == NULL);
    CHECK(test_reject_malicious_descriptors() == 0);
    for (unsigned i = 0u; i < EXTRA; ++i) {
        HalModule device = {0};
        int written = snprintf(device.name, sizeof(device.name),
                               "driver_%03u", i);
        CHECK(written > 0 && (size_t)written < sizeof(device.name));
        device.type = i == 0u ? HAL_MOD_BLK : HAL_MOD_TIMER;
        if (i == 0u) {
            device.probe = test_probe;
            device.init = test_init;
        }
        CHECK(hal_module_register(&entries[i], &device) == NX_STATUS_OK);
        CHECK(hal_module_find(device.name) == &entries[i].module);
        CHECK(hal_module_register(&entries[i], &device) == NX_STATUS_CONFLICT);
        CHECK(hal_module_count() == (size_t)i + 1u);
    }
    CHECK(hal_module_count() == EXTRA);
    CHECK(hal_module_find(NULL) == NULL);
    CHECK(hal_module_probe_all((HalModuleType)100) ==
          NX_STATUS_INVALID_ARGUMENT);
    Value before = script_list->builtin_fn(NULL, 0);
    CHECK(before.type == TYPE_STR && before.data != NULL);
    CHECK(strstr((const char *)before.data, "driver_000 [blk] registered") != NULL);
    val_free(&before);
    CHECK(atomic_load_explicit(&probe_calls, memory_order_acquire) == 0u &&
          atomic_load_explicit(&init_calls, memory_order_acquire) == 0u);
    CHECK(hal_module_probe_all(HAL_MOD_BLK) == NX_STATUS_OK);
    CHECK(atomic_load_explicit(&probe_calls, memory_order_acquire) == 1u &&
          atomic_load_explicit(&init_calls, memory_order_acquire) == 1u);
    CHECK(atomic_load_explicit(&nested_status, memory_order_acquire) ==
          NX_STATUS_BUSY);
    CHECK(hal_module_probe_all(HAL_MOD_BLK) == NX_STATUS_OK);
    CHECK(atomic_load_explicit(&probe_calls, memory_order_acquire) == 1u &&
          atomic_load_explicit(&init_calls, memory_order_acquire) == 1u);
    Value after = script_list->builtin_fn(NULL, 0);
    CHECK(after.type == TYPE_STR && after.data != NULL);
    CHECK(strstr((const char *)after.data, "driver_000 [blk] active") != NULL);
    val_free(&after);
    CHECK(fn_lookup("hal_module_probe") == NULL);
    return 0;
}
int main(void) {
    CHECK(nexs_runtime_init() == 0);
    CHECK(test_catalog_and_vm_denial() == 0);
    CHECK(test_concurrent_admission() == 0);
    (void)puts("BASE_NEXS_HAL_CATALOG_PASS admitted=96 no_fixed_device_count=1 "
               "no_untrusted_probe=1 nested_probe_BUSY=1");
    return 0;
}
