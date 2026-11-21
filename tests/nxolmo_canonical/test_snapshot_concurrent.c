/* Product-owned independent NXOLMO regression test port. */
#include "nexs_state_store.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WRITER_UPDATES UINT32_C(5000)
#define SNAPSHOT_MIN_SUCCESS UINT32_C(1000)

typedef struct snapshot_race_context {
    nx_state_store *store;
    nx_object_ref reference;
    _Atomic uint32_t writer_done;
    _Atomic uint32_t errors;
    _Atomic uint32_t snapshots;
    _Atomic uint32_t busy;
} snapshot_race_context;

static nx_state_node_spec make_spec(uint64_t type_id) {
    nx_state_node_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type_id = type_id;
    spec.schema_id = 1u;
    spec.schema_version = 1u;
    spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    spec.payload_offset = type_id * UINT64_C(16);
    spec.payload_length = 16u;
    spec.payload_digest.bytes[0] = (uint8_t)type_id;
    return spec;
}

static void *snapshot_writer(void *argument) {
    snapshot_race_context *context = (snapshot_race_context *)argument;
    uint32_t update;
    for (update = 0u; update < WRITER_UPDATES; ++update) {
        nx_state_node_spec spec = make_spec((uint64_t)update + UINT64_C(2));
        uint64_t expected_revision = (uint64_t)update + UINT64_C(1);
        uint32_t attempts = 0u;
        nx_status status;
        nx_result result;
        do {
            status = nx_state_store_update(context->store, context->reference,
                                           expected_revision, &spec, NULL,
                                           &result);
            if (status == NX_STATUS_BUSY)
                (void)sched_yield();
            attempts++;
        } while (status == NX_STATUS_BUSY && attempts < UINT32_C(100000));
        if (status != NX_STATUS_OK) {
            (void)atomic_fetch_add_explicit(&context->errors, 1u,
                                            memory_order_relaxed);
            break;
        }
    }
    atomic_store_explicit(&context->writer_done, 1u, memory_order_release);
    return NULL;
}

static void *snapshot_reader(void *argument) {
    snapshot_race_context *context = (snapshot_race_context *)argument;
    nx_state_node storage[1];
    nx_state_snapshot snapshot;
    nx_result result;
    uint32_t successes = 0u;

    while (atomic_load_explicit(&context->writer_done, memory_order_acquire) == 0u ||
           successes < SNAPSHOT_MIN_SUCCESS) {
        nx_status status = nx_state_store_snapshot_capture(
            context->store, storage, 1u, &snapshot, &result);
        if (status == NX_STATUS_BUSY) {
            (void)atomic_fetch_add_explicit(&context->busy, 1u,
                                            memory_order_relaxed);
            (void)sched_yield();
            continue;
        }
        if (status != NX_STATUS_OK ||
            nx_state_snapshot_validate(&snapshot) != NX_STATUS_OK ||
            snapshot.node_count != 1u ||
            snapshot.nodes[0].revision != snapshot.epoch ||
            snapshot.store_id != UINT64_C(0x4e58534e41505348)) {
            (void)atomic_fetch_add_explicit(&context->errors, 1u,
                                            memory_order_relaxed);
            break;
        }
        successes++;
        (void)atomic_fetch_add_explicit(&context->snapshots, 1u,
                                        memory_order_relaxed);
    }
    return NULL;
}

int main(void) {
    nx_state_store store;
    nx_state_slot slots[4];
    uint32_t index[8];
    uint32_t rehash_index[8];
    const uint64_t hash_key[2] = {
        UINT64_C(0x1020304050607080), UINT64_C(0x8877665544332211)
    };
    nx_state_node_spec initial_spec = make_spec(1u);
    nx_object_ref initial_ref;
    nx_state_node final_node;
    nx_state_store_stats stats;
    nx_result result;
    snapshot_race_context context;
    pthread_t writer;
    pthread_t reader;

    if (nx_state_store_init(&store, slots, 4u, index, rehash_index, 8u,
                            UINT64_C(0x4e58534e41505348), hash_key,
                            &result) != NX_STATUS_OK)
        return 1;
    if (nx_state_store_create(&store, &initial_spec, &initial_ref, &result) != NX_STATUS_OK)
        return 1;

    memset(&context, 0, sizeof(context));
    context.store = &store;
    context.reference = initial_ref;
    atomic_init(&context.writer_done, 0u);
    atomic_init(&context.errors, 0u);
    atomic_init(&context.snapshots, 0u);
    atomic_init(&context.busy, 0u);

    if (pthread_create(&writer, NULL, snapshot_writer, &context) != 0)
        return 1;
    if (pthread_create(&reader, NULL, snapshot_reader, &context) != 0) {
        (void)pthread_join(writer, NULL);
        return 1;
    }
    (void)pthread_join(writer, NULL);
    (void)pthread_join(reader, NULL);

    if (atomic_load_explicit(&context.errors, memory_order_relaxed) != 0u ||
        atomic_load_explicit(&context.snapshots, memory_order_relaxed) <
            SNAPSHOT_MIN_SUCCESS ||
        nx_state_store_read(&store, initial_ref, &final_node, &result) != NX_STATUS_OK ||
        nx_state_store_stats_read(&store, &stats, &result) != NX_STATUS_OK ||
        nx_state_store_validate(&store, &result) != NX_STATUS_OK ||
        final_node.revision != (uint64_t)WRITER_UPDATES + UINT64_C(1) ||
        stats.epoch != final_node.revision || stats.count != 1u)
        return 1;

    (void)printf(
        "registry snapshot concurrency: PASS (%u snapshots, %u BUSY)\n",
        atomic_load_explicit(&context.snapshots, memory_order_relaxed),
        atomic_load_explicit(&context.busy, memory_order_relaxed));
    return 0;
}