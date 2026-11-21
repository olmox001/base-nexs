/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_buffer.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define THREAD_COUNT 8u
#define ITERATIONS 1000u
#define BUFFER_LENGTH 64u

static int test_failure(int line) {
    (void)fprintf(stderr, "buffer/registry concurrency failure at line %d\n", line);
    return 1;
}

typedef struct registry_reader_test {
    nx_state_store *registry;
    nx_object_ref *references;
    uint32_t reference_count;
    _Atomic uint64_t completed;
    _Atomic uint32_t failed;
} registry_reader_test;

typedef struct registry_reader_argument {
    registry_reader_test *shared;
    uint32_t thread_index;
} registry_reader_argument;

typedef struct shared_test {
    nx_buffer_manager *manager;
    nx_buffer_authority *authority;
    nx_object_ref buffer;
    uint8_t expected[BUFFER_LENGTH];
    _Atomic uint64_t completed;
    _Atomic uint32_t failed;
} shared_test;

static nx_status authorize_all(void *context, nx_object_ref principal,
                               nx_object_ref target, uint64_t rights,
                               uint64_t epoch) {
    (void)context;
    (void)target;
    (void)rights;
    (void)epoch;
    return nx_object_ref_is_valid(principal) != 0
               ? NX_STATUS_OK : NX_STATUS_ACCESS_DENIED;
}

static void *registry_reader_main(void *argument) {
    registry_reader_argument *reader = (registry_reader_argument *)argument;
    registry_reader_test *shared = reader->shared;
    uint32_t iteration;
    for (iteration = 0u; iteration < ITERATIONS * 4u; ++iteration) {
        uint32_t selected = (iteration + reader->thread_index) %
                            shared->reference_count;
        nx_state_node node;
        nx_result result;
        nx_status status;
        do {
            status = nx_state_store_read(shared->registry,
                                         shared->references[selected],
                                         &node, &result);
            if (status == NX_STATUS_BUSY ||
                status == NX_STATUS_BUDGET_EXHAUSTED)
                (void)sched_yield();
        } while (status == NX_STATUS_BUSY ||
                 status == NX_STATUS_BUDGET_EXHAUSTED);
        if (status != NX_STATUS_OK ||
            node.spec.type_id != UINT64_C(0x5245484153480000) +
                                     (uint64_t)selected + UINT64_C(12)) {
            (void)fprintf(stderr,
                          "registry reader failure: thread=%u iteration=%u selected=%u status=%u actual_type=0x%llx expected_type=0x%llx\n",
                          (unsigned int)reader->thread_index,
                          (unsigned int)iteration, (unsigned int)selected,
                          (unsigned int)status,
                          (unsigned long long)node.spec.type_id,
                          (unsigned long long)(UINT64_C(0x5245484153480000) +
                              (uint64_t)selected + UINT64_C(12)));
            atomic_store_explicit(&shared->failed, 1u, memory_order_release);
            return NULL;
        }
        atomic_fetch_add_explicit(&shared->completed, 1u, memory_order_relaxed);
    }
    return NULL;
}

static void *reader_main(void *argument) {
    shared_test *shared = (shared_test *)argument;
    uint32_t iteration;
    for (iteration = 0u; iteration < ITERATIONS; ++iteration) {
        nx_buffer_view view = {0};
        nx_result result;
        nx_status status;
        do {
            status = nx_buffer_acquire(shared->manager, shared->authority,
                                       shared->buffer, 0u, BUFFER_LENGTH,
                                       NX_BUFFER_RIGHT_READ, &view, &result);
            if (status == NX_STATUS_BUSY || status == NX_STATUS_BUDGET_EXHAUSTED)
                (void)sched_yield();
        } while (status == NX_STATUS_BUSY || status == NX_STATUS_BUDGET_EXHAUSTED);
        if (status != NX_STATUS_OK ||
            memcmp(view.read_bytes.data, shared->expected, BUFFER_LENGTH) != 0) {
            atomic_store_explicit(&shared->failed, 1u, memory_order_release);
            return NULL;
        }
        do {
            status = nx_buffer_view_release(shared->manager, shared->authority,
                                            &view, &result);
            if (status == NX_STATUS_BUSY || status == NX_STATUS_BUDGET_EXHAUSTED)
                (void)sched_yield();
        } while (status == NX_STATUS_BUSY || status == NX_STATUS_BUDGET_EXHAUSTED);
        if (status != NX_STATUS_OK) {
            atomic_store_explicit(&shared->failed, 1u, memory_order_release);
            return NULL;
        }
        atomic_fetch_add_explicit(&shared->completed, 1u, memory_order_relaxed);
    }
    return NULL;
}

int main(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[32];
    uint32_t registry_index[64];
    uint32_t registry_rehash_index[64];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[16];
    nx_buffer_lease_slot leases[64];
    _Alignas(64) uint8_t arena[16384];
    uint8_t bitmap[32];
    nx_result result;
    nx_buffer_authority authority;
    nx_object_ref principal;
    nx_object_ref buffer;
    nx_buffer_view writer = {0};
    shared_test shared;
    registry_reader_test registry_shared;
    registry_reader_argument registry_arguments[THREAD_COUNT];
    nx_object_ref registry_references[24];
    pthread_t threads[THREAD_COUNT];
    const uint64_t key[2] = {UINT64_C(0x0123456789abcdef),
                             UINT64_C(0xfedcba9876543210)};
    uint32_t i;
    nx_state_node_spec registry_spec;
    nx_status registry_validation_status;
    uint64_t registry_completed;
    uint32_t registry_failed;

    if (nx_state_store_init(&registry, registry_slots, 32u, registry_index,
                            registry_rehash_index, 64u, 88u, key,
                            &result) != NX_STATUS_OK)
        return test_failure(__LINE__);
    memset(&registry_spec, 0, sizeof(registry_spec));
    registry_spec.schema_id = 1u;
    registry_spec.schema_version = 1u;
    registry_spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    for (i = 0u; i < 24u; ++i) {
        registry_spec.type_id = UINT64_C(0x5245484153480000) + i;
        if (nx_state_store_create(&registry, &registry_spec,
                                  &registry_references[i], &result) != NX_STATUS_OK)
            return test_failure(__LINE__);
    }
    for (i = 0u; i < 12u; ++i)
        if (nx_state_store_delete(&registry, registry_references[i], 1u,
                                  &result) != NX_STATUS_OK)
            return test_failure(__LINE__);
    if (registry.rehash_state != NX_STATE_STORE_REHASH_MIGRATING &&
        registry.rehash_state != NX_STATE_STORE_REHASH_CLEANING)
        return test_failure(__LINE__);
    registry_shared.registry = &registry;
    registry_shared.references = &registry_references[12];
    registry_shared.reference_count = 12u;
    atomic_init(&registry_shared.completed, 0u);
    atomic_init(&registry_shared.failed, 0u);
    for (i = 0u; i < THREAD_COUNT; ++i) {
        registry_arguments[i].shared = &registry_shared;
        registry_arguments[i].thread_index = i;
        if (pthread_create(&threads[i], NULL, registry_reader_main,
                           &registry_arguments[i]) != 0)
            return test_failure(__LINE__);
    }
    for (i = 0u; i < THREAD_COUNT; ++i)
        (void)pthread_join(threads[i], NULL);
    registry_failed = atomic_load_explicit(&registry_shared.failed,
                                           memory_order_acquire);
    registry_completed = atomic_load_explicit(&registry_shared.completed,
                                              memory_order_acquire);
    registry_validation_status = nx_state_store_validate(&registry, &result);
    if (registry_failed != 0u ||
        registry_completed != (uint64_t)THREAD_COUNT * ITERATIONS * 4u ||
        registry_validation_status != NX_STATUS_OK ||
        registry.rehash_state != NX_STATE_STORE_REHASH_IDLE) {
        (void)fprintf(stderr,
                      "registry rehash check: failed=%u completed=%llu expected=%llu validate=%u rehash_state=%u\n",
                      (unsigned int)registry_failed,
                      (unsigned long long)registry_completed,
                      (unsigned long long)((uint64_t)THREAD_COUNT * ITERATIONS * 4u),
                      (unsigned int)registry_validation_status,
                      (unsigned int)registry.rehash_state);
        return test_failure(__LINE__);
    }
    if (nx_buffer_manager_init(&manager, &registry, buffer_slots, 16u,
                               leases, 64u, arena, sizeof(arena), 64u,
                               bitmap, sizeof(bitmap), &result) != NX_STATUS_OK)
        return test_failure(__LINE__);
    principal.id.high = 11u;
    principal.id.low = 22u;
    principal.generation = 1u;
    authority = (nx_buffer_authority){principal, 1u, authorize_all, NULL};
    if (nx_buffer_create(&manager, &authority, BUFFER_LENGTH, 64u, 0u,
                         &buffer, &result) != NX_STATUS_OK)
        return test_failure(__LINE__);
    if (nx_buffer_acquire(&manager, &authority, buffer, 0u, BUFFER_LENGTH,
                          NX_BUFFER_RIGHT_WRITE, &writer, &result) != NX_STATUS_OK)
        return test_failure(__LINE__);
    for (i = 0u; i < BUFFER_LENGTH; ++i)
        shared.expected[i] = (uint8_t)(i * 29u + 7u);
    memcpy(writer.write_bytes.data, shared.expected, BUFFER_LENGTH);
    if (nx_buffer_view_commit(&manager, &authority, &writer, BUFFER_LENGTH,
                              &result) != NX_STATUS_OK)
        return test_failure(__LINE__);

    shared.manager = &manager;
    shared.authority = &authority;
    shared.buffer = buffer;
    atomic_init(&shared.completed, 0u);
    atomic_init(&shared.failed, 0u);
    for (i = 0u; i < THREAD_COUNT; ++i)
        if (pthread_create(&threads[i], NULL, reader_main, &shared) != 0)
            return test_failure(__LINE__);
    for (i = 0u; i < THREAD_COUNT; ++i)
        (void)pthread_join(threads[i], NULL);
    if (atomic_load_explicit(&shared.failed, memory_order_acquire) != 0u ||
        atomic_load_explicit(&shared.completed, memory_order_acquire) !=
            (uint64_t)THREAD_COUNT * ITERATIONS)
        return test_failure(__LINE__);
    if (nx_buffer_manager_validate(&manager, &result) != NX_STATUS_OK)
        return test_failure(__LINE__);
    (void)puts("registry concurrent rehash readers: 8 x 4000, PASS");
    (void)puts("buffer manager concurrent read leases: 8 x 1000, PASS");
    return 0;
}
