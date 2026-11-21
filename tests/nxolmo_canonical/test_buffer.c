/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_buffer.h"
#include "nexs_buffer_capability_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n",              \
                          __FILE__, __LINE__, #expression);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct auth_context {
    uint64_t epoch;
    uint64_t allowed_rights;
    nx_object_ref denied_target;
    uint32_t deny_all;
} auth_context;

static nx_status authorize(void *context, nx_object_ref principal,
                           nx_object_ref target, uint64_t rights,
                           uint64_t epoch) {
    auth_context *auth = (auth_context *)context;
    if (auth == NULL || !nx_object_ref_is_valid(principal) ||
        epoch != auth->epoch || auth->deny_all != 0u ||
        (rights & ~auth->allowed_rights) != 0u)
        return NX_STATUS_ACCESS_DENIED;
    if (nx_object_ref_is_valid(auth->denied_target) &&
        nx_object_ref_equal(auth->denied_target, target))
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static nx_object_ref make_principal(uint64_t id) {
    nx_object_ref ref;
    ref.id.high = UINT64_C(0x5052494e43495041);
    ref.id.low = id;
    ref.generation = 1u;
    return ref;
}

static int init_manager(nx_buffer_manager *manager, nx_state_store *registry,
                        nx_state_slot *registry_slots, uint32_t *registry_index,
                        uint32_t *registry_rehash_index,
                        nx_buffer_slot *buffer_slots,
                        nx_buffer_lease_slot *leases, uint8_t *arena,
                        uint8_t *bitmap, nx_result *result) {
    const uint64_t key[2] = {
        UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210)
    };
    nx_state_node_spec preexisting;
    nx_object_ref preexisting_ref;
    CHECK(nx_state_store_init(registry, registry_slots, 16u,
                              registry_index, registry_rehash_index, 32u, UINT64_C(0x425546464552),
                              key, result) == NX_STATUS_OK);
    memset(&preexisting, 0, sizeof(preexisting));
    preexisting.type_id = UINT64_C(0x5052454558495354);
    preexisting.schema_id = 1u;
    preexisting.schema_version = 1u;
    preexisting.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    CHECK(nx_state_store_create(registry, &preexisting, &preexisting_ref,
                                result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_init(manager, registry, buffer_slots, 8u,
                                 leases, 16u, arena, 4096u, 64u,
                                 bitmap, 8u, result) == NX_STATUS_OK);
    return 0;
}

static int test_crc64(void) {
    static const uint8_t vector[] = "123456789";
    uint64_t checksum = 0u;
    CHECK(nx_buffer_crc64_ecma((nx_const_span){vector, 9u}, &checksum) ==
          NX_STATUS_OK);
    CHECK(checksum == UINT64_C(0x6c40df5f0b497347));
    CHECK(nx_buffer_crc64_ecma((nx_const_span){NULL, 0u}, &checksum) ==
          NX_STATUS_OK);
    CHECK(checksum == 0u);
    CHECK(nx_buffer_crc64_ecma((nx_const_span){NULL, 1u}, &checksum) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_buffer_crc64_ecma((nx_const_span){vector + 1, 8u}, &checksum) ==
          NX_STATUS_OK);
    return 0;
}

static int test_buffer_lifecycle_integrity_and_zeroing(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    auth_context auth = {
        7u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_MAP | NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(1u), 7u, authorize, &auth};
    nx_buffer_authority other_authority = {make_principal(99u), 7u, authorize, &auth};
    nx_object_ref buffer;
    nx_object_ref second;
    nx_buffer_view view = {0};
    nx_buffer_view stale_view = {0};
    nx_buffer_stats stats;
    nx_state_node node;
    nx_operation_metrics_snapshot metrics;
    const char payload[] = "hello";

    memset(arena, 0xa5, sizeof(arena));
    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    CHECK(nx_result_validate(&result) == NX_STATUS_OK);
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);
    CHECK(nx_object_ref_is_valid(buffer) != 0);
    CHECK(nx_state_store_read(&registry, buffer, &node, &result) ==
          NX_STATUS_OK);
    CHECK(node.spec.type_id == NX_BUFFER_TYPE_ID);
    CHECK(nx_object_ref_equal(node.spec.owner, authority.principal) != 0);
    CHECK(node.spec.payload_offset < 8u);
    CHECK(node.spec.payload_length == 0u);
    CHECK(arena[0] == 0u && arena[127] == 0u);

    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_WRITE | NX_BUFFER_RIGHT_MAP,
                            &view, &result) == NX_STATUS_OK);
    CHECK(view.write_bytes.size == 5u);
    memcpy(view.write_bytes.data, payload, sizeof(payload) - 1u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view,
                                sizeof(payload) - 1u, &result) == NX_STATUS_OK);
    CHECK(view.active == 0u);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_OK);
    CHECK(memcmp(view.read_bytes.data, payload, 5u) == 0);
    stale_view = view;
    view.read_bytes.data++;
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) ==
          NX_STATUS_INVALID_ARGUMENT);
    view.read_bytes.data--;
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.pinned_bytes == 5u && stats.active_leases == 1u);
    CHECK(nx_buffer_view_release(&manager, &other_authority, &view, &result) ==
          NX_STATUS_STALE_REFERENCE);
    stale_view.lease_id++;
    CHECK(nx_buffer_view_release(&manager, &authority, &stale_view, &result) ==
          NX_STATUS_STALE_REFERENCE);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_BUSY);
    {
        nx_buffer_view denied_view = {0};
        CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 1u,
                                NX_BUFFER_RIGHT_WRITE, &denied_view, &result) ==
              NX_STATUS_BUSY);
    }
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_buffer_verify(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);

    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 6u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_OK);
    memcpy(view.write_bytes.data, "world!", 6u);
    CHECK(nx_buffer_view_commit(&manager, &other_authority, &view, 6u, &result) ==
          NX_STATUS_STALE_REFERENCE);
    CHECK(view.active == 1u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 6u, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_verify(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.active_buffers == 1u);
    CHECK(stats.logical_bytes == 6u);
    CHECK(stats.active_leases == 0u);
    CHECK(stats.pinned_bytes == 0u);

    /* A data bit flip must fail closed and quarantine the canonical node. */
    arena[(size_t)buffer_slots[0].arena_offset] ^= UINT8_C(0x01);
    CHECK(nx_buffer_verify(&manager, &authority, buffer, &result) ==
          NX_STATUS_INTEGRITY_FAILURE);
    CHECK(buffer_slots[0].state == NX_BUFFER_STATE_QUARANTINED);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 1u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_INTEGRITY_FAILURE);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &second, &result) == NX_STATUS_OK);
    CHECK(!nx_object_ref_equal(buffer, second));
    CHECK(arena[0] == 0u && arena[127] == 0u);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 1u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_NOT_FOUND);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_metrics_read(&manager, NX_BUFFER_METRIC_CREATE,
                                 &metrics) == NX_STATUS_OK);
    CHECK(metrics.calls == 2u && metrics.failures == 0u);
    CHECK(nx_buffer_metrics_read(&manager, NX_BUFFER_METRIC_STATS,
                                 &metrics) == NX_STATUS_OK);
    CHECK(metrics.calls == 2u && metrics.operation_id == NX_OP_BUFFER_STATS_READ);
    CHECK(metrics.known_elapsed_samples == 2u);
    CHECK(nx_buffer_metrics_read(&manager, NX_BUFFER_METRIC_VERIFY,
                                 &metrics) == NX_STATUS_OK);
    CHECK(metrics.calls == 3u && metrics.failures == 1u);
    CHECK(nx_buffer_destroy(&manager, &authority, second, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    return 0;
}

static int test_destroy_checks_lease_table_not_aggregates(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    auth_context auth = {
        3u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(8u), 3u, authorize, &auth};
    nx_object_ref buffer;
    nx_buffer_view view = {0};

    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 4u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) == NX_STATUS_OK);
    memcpy(view.write_bytes.data, "lease", 4u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 4u, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 4u,
                            NX_BUFFER_RIGHT_READ, &view, &result) == NX_STATUS_OK);

    /* Aggregate counters are diagnostic caches; the lease table is authoritative
     * for reclamation and a damaged aggregate must never permit early free. */
    buffer_slots[view.slot_index].lease_count = 0u;
    buffer_slots[view.slot_index].pinned_bytes = 0u;
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_BUSY);
    buffer_slots[view.slot_index].lease_count = 1u;
    buffer_slots[view.slot_index].pinned_bytes = view.length;
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    {
        size_t i;
        for (i = 0u; i < 128u; ++i)
            CHECK(arena[i] == 0u);
    }
    return 0;
}

static int test_registry_full_create_rollback(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[4];
    uint32_t registry_index[8];
    uint32_t registry_rehash_index[8];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[4];
    nx_buffer_lease_slot leases[8];
    _Alignas(64) uint8_t arena[256];
    uint8_t bitmap[4] = {0u, 0u, 0u, 0u};
    const uint64_t key[2] = {
        UINT64_C(0x1020304050607080), UINT64_C(0x8877665544332211)
    };
    nx_state_node_spec blocker_spec;
    nx_object_ref blockers[4];
    nx_object_ref created;
    nx_buffer_stats stats;
    nx_result result;
    auth_context auth = {
        5u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(55u), 5u, authorize, &auth};
    uint32_t i;

    CHECK(nx_state_store_init(&registry, registry_slots, 4u, registry_index,
                              registry_rehash_index, 8u,
                              UINT64_C(0x4641554c545354), key, &result) ==
          NX_STATUS_OK);
    memset(&blocker_spec, 0, sizeof(blocker_spec));
    blocker_spec.type_id = UINT64_C(0x4641554c544e4f44);
    blocker_spec.schema_id = 1u;
    blocker_spec.schema_version = 1u;
    blocker_spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    for (i = 0u; i < 4u; ++i)
        CHECK(nx_state_store_create(&registry, &blocker_spec, &blockers[i],
                                    &result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_init(&manager, &registry, buffer_slots, 4u,
                                 leases, 8u, arena, sizeof(arena), 64u,
                                 bitmap, sizeof(bitmap), &result) == NX_STATUS_OK);

    memset(arena, 0xa5, sizeof(arena));
    created.id.high = UINT64_C(0xaaaaaaaaaaaaaaaa);
    created.id.low = UINT64_C(0xbbbbbbbbbbbbbbbb);
    created.generation = 99u;
    CHECK(nx_buffer_create(&manager, &authority, 64u, 64u, 0u,
                           &created, &result) == NX_STATUS_RESOURCE_EXHAUSTED);
    CHECK(created.id.high == 0u && created.id.low == 0u &&
          created.generation == 0u);
    for (i = 0u; i < 4u; ++i)
        CHECK(bitmap[i] == 0u);
    for (i = 0u; i < 4u; ++i)
        CHECK(buffer_slots[i].state == NX_BUFFER_STATE_FREE);
    for (i = 0u; i < 64u; ++i)
        CHECK(arena[i] == 0u);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.active_buffers == 0u && stats.allocated_bytes == 0u);
    CHECK(stats.free_granules == 4u && stats.active_leases == 0u);
    CHECK(nx_state_store_validate(&registry, &result) == NX_STATUS_OK);

    /* Releasing the registry slot must allow a retry; the failed publish must
     * not have leaked the arena extent, bitmap bits, or manager slot. */
    CHECK(nx_state_store_delete(&registry, blockers[0], 1u, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_create(&manager, &authority, 64u, 64u, 0u,
                           &created, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_destroy(&manager, &authority, created, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    return 0;
}

static int test_registry_revision_conflict_quarantine_recovery(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    nx_state_node node;
    nx_state_node updated;
    nx_buffer_stats stats;
    auth_context auth = {
        23u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(230u), 23u, authorize, &auth};
    nx_object_ref buffer;
    nx_buffer_view view = {0};
    uint32_t i;

    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 16u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_OK);
    memcpy(view.write_bytes.data, "registry-race!!", 15u);

    /* Model an out-of-band registry writer advancing the revision after the
     * lease was issued but before the buffer's transaction commits. */
    CHECK(nx_state_store_read(&registry, buffer, &node, &result) == NX_STATUS_OK);
    CHECK(nx_state_store_update(&registry, buffer, node.revision, &node.spec,
                                &updated, &result) == NX_STATUS_OK);
    CHECK(updated.revision == node.revision + 1u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 16u, &result) ==
          NX_STATUS_CORRUPT);
    CHECK(view.active == 0u && view.lease_id == 0u);
    CHECK(buffer_slots[0].state == NX_BUFFER_STATE_QUARANTINED);
    CHECK(nx_state_store_read(&registry, buffer, &node, &result) == NX_STATUS_OK);
    CHECK(node.spec.lifecycle == NX_STATE_LIFECYCLE_QUARANTINED);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);

    /* Quarantined state is not exposed as a successful buffer; destroy performs
     * the verified recovery path and scrubs the complete physical allocation. */
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    CHECK(nx_state_store_read(&registry, buffer, &node, &result) ==
          NX_STATUS_NOT_FOUND);
    for (i = 0u; i < 128u; ++i)
        CHECK(arena[i] == 0u);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.active_buffers == 0u && stats.allocated_bytes == 0u);
    CHECK(stats.free_granules == 64u && stats.active_leases == 0u);
    return 0;
}

static uint64_t test_rng_next(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13u;
    value ^= value >> 7u;
    value ^= value << 17u;
    *state = value;
    return value;
}

static int test_model_based_lifecycle_history(void) {
    typedef struct model_buffer {
        nx_object_ref reference;
        uint64_t capacity;
        uint64_t length;
        uint8_t bytes[256];
        uint32_t active;
        uint32_t sealed;
    } model_buffer;
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    nx_buffer_stats stats;
    auth_context auth = {
        17u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(170u), 17u, authorize, &auth};
    model_buffer model[8];
    uint64_t random_state = UINT64_C(0x9e3779b97f4a7c15);
    uint32_t creates = 0u;
    uint32_t writes = 0u;
    uint32_t reads = 0u;
    uint32_t verifies = 0u;
    uint32_t destroys = 0u;
    uint32_t step;

    memset(model, 0, sizeof(model));
    memset(arena, 0xa5, sizeof(arena));
    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);

    for (step = 0u; step < 2048u; ++step) {
        uint64_t random = test_rng_next(&random_state);
        uint32_t index = (uint32_t)(random % 8u);
        model_buffer *entry = &model[index];

        switch (step % 5u) {
        case 0u:
            if (entry->active == 0u) {
                uint64_t capacity = UINT64_C(64) *
                                    (UINT64_C(1) + ((random >> 8u) % 4u));
                nx_object_ref reference;
                nx_status status = nx_buffer_create(
                    &manager, &authority, capacity, 64u, 0u, &reference, &result);
                if (status == NX_STATUS_OK) {
                    entry->reference = reference;
                    entry->capacity = capacity;
                    entry->length = 0u;
                    entry->active = 1u;
                    entry->sealed = 0u;
                    memset(entry->bytes, 0, sizeof(entry->bytes));
                    creates++;
                } else {
                    CHECK(status == NX_STATUS_NO_SPACE ||
                          status == NX_STATUS_RESOURCE_EXHAUSTED);
                }
            }
            break;
        case 1u:
            if (entry->active != 0u) {
                nx_buffer_view view = {0};
                uint64_t length = UINT64_C(1) +
                                 (random >> 16u) % entry->capacity;
                size_t byte_index;
                CHECK(nx_buffer_acquire(&manager, &authority, entry->reference,
                                        0u, length, NX_BUFFER_RIGHT_WRITE,
                                        &view, &result) == NX_STATUS_OK);
                CHECK(view.write_bytes.size == (size_t)length);
                for (byte_index = 0u; byte_index < (size_t)length; ++byte_index)
                    entry->bytes[byte_index] =
                        (uint8_t)(test_rng_next(&random_state) & UINT64_C(0xff));
                memcpy(view.write_bytes.data, entry->bytes, (size_t)length);
                CHECK(nx_buffer_view_commit(&manager, &authority, &view,
                                            length, &result) == NX_STATUS_OK);
                entry->length = length;
                entry->sealed = 1u;
                writes++;
            }
            break;
        case 2u:
            if (entry->active != 0u && entry->length != 0u) {
                nx_buffer_view view = {0};
                CHECK(nx_buffer_acquire(&manager, &authority, entry->reference,
                                        0u, entry->length, NX_BUFFER_RIGHT_READ,
                                        &view, &result) == NX_STATUS_OK);
                CHECK(view.read_bytes.size == (size_t)entry->length);
                CHECK(memcmp(view.read_bytes.data, entry->bytes,
                             (size_t)entry->length) == 0);
                CHECK(nx_buffer_view_release(&manager, &authority, &view,
                                             &result) == NX_STATUS_OK);
                reads++;
            }
            break;
        case 3u:
            if (entry->active != 0u) {
                nx_status verify_status = nx_buffer_verify(
                    &manager, &authority, entry->reference, &result);
                if (entry->sealed != 0u) {
                    CHECK(verify_status == NX_STATUS_OK);
                    verifies++;
                } else {
                    CHECK(verify_status == NX_STATUS_BUSY);
                }
            }
            break;
        case 4u:
            if (entry->active != 0u) {
                CHECK(nx_buffer_destroy(&manager, &authority, entry->reference,
                                        &result) == NX_STATUS_OK);
                memset(entry, 0, sizeof(*entry));
                destroys++;
            }
            break;
        default:
            CHECK(0);
            break;
        }

        {
            uint64_t expected_buffers = 0u;
            uint64_t expected_bytes = 0u;
            uint32_t i;
            nx_status validation_status = nx_buffer_manager_validate(&manager,
                                                                     &result);
            if (validation_status != NX_STATUS_OK) {
                uint32_t slot_index;
                (void)fprintf(stderr,
                              "model history manager invariant failed: step=%u "
                              "index=%u status=%s active=%u sealed=%u length=%llu "
                              "capacity=%llu\n",
                              step, index, nx_status_name(validation_status),
                              entry->active, entry->sealed,
                              (unsigned long long)entry->length,
                              (unsigned long long)entry->capacity);
                for (slot_index = 0u; slot_index < manager.slot_capacity;
                     ++slot_index) {
                    nx_buffer_slot *slot = &manager.slots[slot_index];
                    nx_state_node node;
                    nx_result read_result;
                    nx_status read_status = slot->state == NX_BUFFER_STATE_FREE
                        ? NX_STATUS_NOT_FOUND
                        : nx_state_store_read(&registry, slot->reference, &node,
                                              &read_result);
                    (void)fprintf(stderr,
                                  "slot=%u state=%u id=%llu gen=%llu owner=%llu "
                                  "offset=%llu cap=%llu alloc=%llu granules=%llu "
                                  "len=%llu leases=%llu pinned=%llu revision=%llu "
                                  "registry=%s node_rev=%llu node_lifecycle=%u "
                                  "node_offset=%llu\n",
                                  slot_index, slot->state,
                                  (unsigned long long)slot->reference.id.low,
                                  (unsigned long long)slot->reference.generation,
                                  (unsigned long long)slot->owner.id.low,
                                  (unsigned long long)slot->arena_offset,
                                  (unsigned long long)slot->capacity,
                                  (unsigned long long)slot->allocation_bytes,
                                  (unsigned long long)slot->allocation_granules,
                                  (unsigned long long)slot->logical_length,
                                  (unsigned long long)slot->lease_count,
                                  (unsigned long long)slot->pinned_bytes,
                                  (unsigned long long)slot->registry_revision,
                                  nx_status_name(read_status),
                                  (unsigned long long)(read_status == NX_STATUS_OK
                                                           ? node.revision : 0u),
                                  read_status == NX_STATUS_OK
                                      ? node.spec.lifecycle : 0u,
                                  (unsigned long long)(read_status == NX_STATUS_OK
                                                           ? node.spec.payload_offset
                                                           : 0u));
                }
                return 1;
            }
            CHECK(nx_state_store_validate(&registry, &result) == NX_STATUS_OK);
            CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
            for (i = 0u; i < 8u; ++i) {
                if (model[i].active != 0u) {
                    expected_buffers++;
                    expected_bytes += model[i].length;
                }
            }
            CHECK(stats.active_buffers == expected_buffers);
            CHECK(stats.logical_bytes == expected_bytes);
            CHECK(stats.active_leases == 0u && stats.pinned_bytes == 0u);
        }
    }

    CHECK(creates != 0u && writes != 0u && reads != 0u &&
          verifies != 0u && destroys != 0u);
    {
        uint32_t i;
        for (i = 0u; i < 8u; ++i) {
            if (model[i].active != 0u)
                CHECK(nx_buffer_destroy(&manager, &authority, model[i].reference,
                                        &result) == NX_STATUS_OK);
        }
    }
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_state_store_validate(&registry, &result) == NX_STATUS_OK);
    return 0;
}

static int test_repeated_lifecycle_reclamation_no_resource_leak(void) {
    enum { LIFECYCLES = 10000, VALIDATION_INTERVAL = 256 };
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    nx_buffer_stats buffer_stats;
    nx_state_store_stats registry_stats;
    auth_context auth = {
        23u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(230u), 23u, authorize, &auth};
    uint8_t payload[128];
    uint32_t iteration;
    size_t byte_index;

    memset(arena, 0xa5, sizeof(arena));
    memset(bitmap, 0xff, sizeof(bitmap));
    memset(payload, 0, sizeof(payload));
    CHECK(init_manager(&manager, &registry, registry_slots, registry_index,
                       registry_rehash_index, buffer_slots, leases, arena,
                       bitmap, &result) == 0);
    CHECK(result.operation_id == NX_OP_BUFFER_MANAGER_INIT);
    CHECK(result.fuel_used == (uint64_t)sizeof(buffer_slots) +
                              (uint64_t)sizeof(leases) +
                              (uint64_t)sizeof(arena) +
                              (uint64_t)sizeof(bitmap) + 32u);
    for (byte_index = 0u; byte_index < sizeof(arena); ++byte_index)
        CHECK(arena[byte_index] == 0u);

    for (iteration = 0u; iteration < LIFECYCLES; ++iteration) {
        nx_object_ref reference;
        nx_buffer_view view = {0};
        size_t payload_index;
        for (payload_index = 0u; payload_index < sizeof(payload);
             ++payload_index)
            payload[payload_index] =
                (uint8_t)((iteration + (uint32_t)payload_index) & 0xffu);
        CHECK(nx_buffer_create(&manager, &authority, sizeof(payload), 64u,
                               NX_BUFFER_FLAG_ZERO_ON_ALLOC, &reference,
                               &result) == NX_STATUS_OK);
        CHECK(nx_buffer_acquire(&manager, &authority, reference, 0u,
                                sizeof(payload), NX_BUFFER_RIGHT_WRITE, &view,
                                &result) == NX_STATUS_OK);
        memcpy(view.write_bytes.data, payload, sizeof(payload));
        CHECK(nx_buffer_view_commit(&manager, &authority, &view,
                                    sizeof(payload), &result) == NX_STATUS_OK);
        CHECK(nx_buffer_verify(&manager, &authority, reference, &result) ==
              NX_STATUS_OK);
        CHECK(nx_buffer_destroy(&manager, &authority, reference, &result) ==
              NX_STATUS_OK);
        if ((iteration + 1u) % VALIDATION_INTERVAL == 0u) {
            CHECK(nx_buffer_manager_validate(&manager, &result) ==
                  NX_STATUS_OK);
            CHECK(nx_state_store_validate(&registry, &result) == NX_STATUS_OK);
        }
    }

    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_state_store_validate(&registry, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &buffer_stats, &result) ==
          NX_STATUS_OK);
    CHECK(buffer_stats.active_buffers == 0u);
    CHECK(buffer_stats.active_leases == 0u);
    CHECK(buffer_stats.pinned_bytes == 0u);
    CHECK(buffer_stats.logical_bytes == 0u);
    CHECK(buffer_stats.allocated_bytes == 0u);
    CHECK(buffer_stats.free_granules == manager.granule_count);
    CHECK(nx_state_store_stats_read(&registry, &registry_stats, &result) ==
          NX_STATUS_OK);
    CHECK(registry_stats.count == 1u);
    CHECK(registry_stats.retired_slots == 0u);
    for (byte_index = 0u; byte_index < sizeof(arena); ++byte_index)
        CHECK(arena[byte_index] == 0u);
    for (byte_index = 0u; byte_index < sizeof(bitmap); ++byte_index)
        CHECK(bitmap[byte_index] == 0u);
    (void)printf("buffer lifecycle reclamation: %u create/commit/verify/destroy "
                 "cycles; zero active resources and fully scrubbed arena\n",
                 LIFECYCLES);
    return 0;
}

static int test_abort_auth_bounds_and_immutable(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    auth_context auth = {
        9u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(2u), 9u, authorize, &auth};
    nx_buffer_authority other_authority = {make_principal(98u), 9u, authorize, &auth};
    nx_object_ref buffer;
    nx_object_ref immutable;
    nx_buffer_view view = {0};

    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    CHECK(nx_buffer_create(&manager, &authority, 64u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 63u, 2u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_INVALID_ARGUMENT);
    auth.deny_all = 1u;
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 1u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_ACCESS_DENIED);
    auth.deny_all = 0u;
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 64u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_OK);
    memset(view.write_bytes.data, 0x5a, view.write_bytes.size);
    CHECK(nx_buffer_view_abort(&manager, &other_authority, &view, &result) ==
          NX_STATUS_STALE_REFERENCE);
    CHECK(view.active == 1u);
    CHECK(nx_buffer_view_abort(&manager, &authority, &view, &result) ==
          NX_STATUS_CANCELLED);
    CHECK(buffer_slots[0].state == NX_BUFFER_STATE_QUARANTINED);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);

    CHECK(nx_buffer_create(&manager, &authority, 64u, 64u,
                           NX_BUFFER_FLAG_IMMUTABLE, &immutable, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, immutable, 0u, 9u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_OK);
    memcpy(view.write_bytes.data, "immutable", 9u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 9u, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, immutable, 0u, 1u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_buffer_destroy(&manager, &authority, immutable, &result) ==
          NX_STATUS_OK);
    return 0;
}

static int test_sparse_extension_truncate_and_view_integrity(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    auth_context auth = {
        1u,
        NX_BUFFER_RIGHT_CREATE | NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
            NX_BUFFER_RIGHT_DESTROY,
        {{0u, 0u}, 0u},
        0u
    };
    nx_buffer_authority authority = {make_principal(4u), 1u, authorize, &auth};
    nx_object_ref buffer;
    nx_buffer_view view = {0};
    uint8_t expected[16];

    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);

    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 16u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) == NX_STATUS_OK);
    memcpy(view.write_bytes.data, "abcdefghijklmnop", 16u);
    /* A forged lease offset or mapped pointer cannot be committed. */
    view.offset = 1u;
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 17u, &result) ==
          NX_STATUS_STALE_REFERENCE);
    view.offset = 0u;
    view.write_bytes.data++;
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 16u, &result) ==
          NX_STATUS_INVALID_ARGUMENT);
    view.write_bytes.data--;
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 16u, &result) ==
          NX_STATUS_OK);

    /* Shrinking must scrub the old tail; later sparse extension must zero holes
     * and any suffix beyond the new write span before publishing the length. */
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 2u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) == NX_STATUS_OK);
    memcpy(view.write_bytes.data, "XY", 2u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 2u, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 10u, 1u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) == NX_STATUS_OK);
    view.write_bytes.data[0] = (uint8_t)'Z';
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 16u, &result) ==
          NX_STATUS_OK);
    memset(expected, 0, sizeof(expected));
    expected[0] = (uint8_t)'X';
    expected[1] = (uint8_t)'Y';
    expected[10] = (uint8_t)'Z';
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, sizeof(expected),
                            NX_BUFFER_RIGHT_READ, &view, &result) == NX_STATUS_OK);
    CHECK(memcmp(view.read_bytes.data, expected, sizeof(expected)) == 0);
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) == NX_STATUS_OK);
    return 0;
}

typedef struct buffer_cap_resolver_context {
    nx_capability_slot_state state;
    uint64_t index;
} buffer_cap_resolver_context;

static nx_status buffer_cap_resolve(void *context, uint64_t index,
                                    nx_capability_slot_state *state) {
    buffer_cap_resolver_context *resolver =
        (buffer_cap_resolver_context *)context;
    if (index != resolver->index)
        return NX_STATUS_NOT_FOUND;
    *state = resolver->state;
    return NX_STATUS_OK;
}

static int test_capability_authorized_buffer_flow(void) {
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t registry_index[32];
    uint32_t registry_rehash_index[32];
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    _Alignas(64) uint8_t arena[4096];
    uint8_t bitmap[8];
    nx_result result;
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims claims;
    nx_buffer_capability_context capability_context;
    buffer_cap_resolver_context resolver;
    nx_object_ref principal = make_principal(77u);
    nx_object_ref buffer;
    nx_buffer_authority authority;
    nx_buffer_view view = {0};
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    size_t i;

    CHECK(init_manager(&manager, &registry, registry_slots, registry_index, registry_rehash_index,
                       buffer_slots, leases, arena, bitmap, &result) == 0);
    memset(&key, 0, sizeof(key));
    key.key_id = 44u;
    key.active = 1u;
    key.not_before_tick = 1u;
    key.not_after_tick = 1000u;
    for (i = 0u; i < sizeof(key.secret); ++i)
        key.secret[i] = (uint8_t)(i + 19u);
    keyring.keys = &key;
    keyring.key_count = 1u;

    memset(&claims, 0, sizeof(claims));
    claims.virtual_slot_index = 1u;
    claims.slot_generation = 1u;
    claims.rights_mask = NX_BUFFER_RIGHT_CREATE;
    claims.scope_id = 88u;
    claims.lease_epoch = 12u;
    claims.expiry_tick = 900u;
    claims.nonce = 1u;
    claims.subject = principal;
    claims.policy_version = 3u;
    CHECK(nx_capability_issue(&keyring, key.key_id, &claims, 20u, token) ==
          NX_STATUS_OK);

    memset(&resolver, 0, sizeof(resolver));
    resolver.index = 1u;
    resolver.state.object = make_principal(999u);
    resolver.state.subject = principal;
    resolver.state.generation = 1u;
    resolver.state.scope_id = 88u;
    resolver.state.lease_epoch = 12u;
    resolver.state.rights_ceiling = NX_BUFFER_RIGHT_CREATE;
    resolver.state.policy_version = 3u;
    resolver.state.active = 1u;
    capability_context.keyring = &keyring;
    capability_context.token = (nx_const_span){token, sizeof(token)};
    capability_context.resolve = buffer_cap_resolve;
    capability_context.resolve_context = &resolver;
    capability_context.current_tick = 30u;
    capability_context.slot_capacity = 8u;
    capability_context.scope_id = 88u;
    capability_context.policy_version = 3u;
    authority = (nx_buffer_authority){principal, 12u,
                                      nx_buffer_capability_authorize,
                                      &capability_context};
    CHECK(nx_buffer_create(&manager, &authority, 128u, 64u, 0u,
                           &buffer, &result) == NX_STATUS_OK);

    memset(&claims, 0, sizeof(claims));
    claims.virtual_slot_index = 2u;
    claims.slot_generation = buffer.generation;
    claims.rights_mask = NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
                         NX_BUFFER_RIGHT_MAP | NX_BUFFER_RIGHT_DESTROY;
    claims.scope_id = 88u;
    claims.lease_epoch = 12u;
    claims.expiry_tick = 900u;
    claims.nonce = 2u;
    claims.subject = principal;
    claims.policy_version = 3u;
    CHECK(nx_capability_issue(&keyring, key.key_id, &claims, 30u, token) ==
          NX_STATUS_OK);
    capability_context.token = (nx_const_span){token, sizeof(token)};
    resolver.index = 2u;
    resolver.state.object = buffer;
    resolver.state.generation = buffer.generation;
    resolver.state.rights_ceiling = claims.rights_mask;

    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_WRITE, &view, &result) ==
          NX_STATUS_OK);
    memcpy(view.write_bytes.data, "token", 5u);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view, 5u, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_verify(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_OK);
    CHECK(memcmp(view.read_bytes.data, "token", 5u) == 0);
    CHECK(nx_buffer_view_release(&manager, &authority, &view, &result) ==
          NX_STATUS_OK);

    resolver.state.object = make_principal(1000u);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_ACCESS_DENIED);
    resolver.state.object = buffer;
    capability_context.current_tick = 900u;
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u, 5u,
                            NX_BUFFER_RIGHT_READ, &view, &result) ==
          NX_STATUS_ACCESS_DENIED);
    capability_context.current_tick = 30u;
    CHECK(nx_buffer_destroy(&manager, &authority, buffer, &result) ==
          NX_STATUS_OK);
    return 0;
}

static nx_buffer_message make_message(uint64_t request_id) {
    nx_buffer_message message;
    memset(&message, 0, sizeof(message));
    message.request_id = request_id;
    message.request_generation = 1u;
    message.buffer.id.high = 1u;
    message.buffer.id.low = 2u;
    message.buffer.generation = 1u;
    message.offset = 0u;
    message.length = 16u;
    message.lease_id = request_id;
    message.expected_revision = 1u;
    message.operation = NX_BUFFER_MESSAGE_READ;
    message.rights = (uint32_t)NX_BUFFER_RIGHT_READ;
    return message;
}

static int test_ring_bounds_and_integrity(void) {
    nx_buffer_ring ring;
    nx_buffer_ring_cell cells[2];
    nx_buffer_message input;
    nx_buffer_message output;
    nx_result result;

    CHECK(nx_buffer_ring_init(&ring, cells, 2u, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_ring_dequeue(&ring, &output, &result) == NX_STATUS_NOT_FOUND);
    input = make_message(1u);
    CHECK(nx_buffer_ring_enqueue(&ring, &input, &result) == NX_STATUS_OK);
    input = make_message(2u);
    CHECK(nx_buffer_ring_enqueue(&ring, &input, &result) == NX_STATUS_OK);
    input = make_message(3u);
    CHECK(nx_buffer_ring_enqueue(&ring, &input, &result) == NX_STATUS_NO_SPACE);
    CHECK(nx_buffer_ring_dequeue(&ring, &output, &result) == NX_STATUS_OK);
    CHECK(output.request_id == 1u);
    CHECK(nx_buffer_ring_dequeue(&ring, &output, &result) == NX_STATUS_OK);
    CHECK(output.request_id == 2u);
    CHECK(nx_buffer_ring_dequeue(&ring, &output, &result) == NX_STATUS_NOT_FOUND);

    input = make_message(4u);
    CHECK(nx_buffer_ring_enqueue(&ring, &input, &result) == NX_STATUS_OK);
    cells[0].message.reserved = 1u;
    CHECK(nx_buffer_ring_dequeue(&ring, &output, &result) == NX_STATUS_CORRUPT);
    CHECK(output.request_id == 0u);
    input = make_message(5u);
    input.offset = UINT64_MAX;
    CHECK(nx_buffer_ring_enqueue(&ring, &input, &result) == NX_STATUS_MALFORMED);
    input = make_message(6u);
    input.buffer.generation = 0u;
    CHECK(nx_buffer_message_validate(&input) == NX_STATUS_MALFORMED);
    return 0;
}

int main(void) {
    CHECK(test_crc64() == 0);
    CHECK(test_buffer_lifecycle_integrity_and_zeroing() == 0);
    CHECK(test_destroy_checks_lease_table_not_aggregates() == 0);
    CHECK(test_registry_full_create_rollback() == 0);
    CHECK(test_registry_revision_conflict_quarantine_recovery() == 0);
    CHECK(test_model_based_lifecycle_history() == 0);
    CHECK(test_repeated_lifecycle_reclamation_no_resource_leak() == 0);
    CHECK(test_abort_auth_bounds_and_immutable() == 0);
    CHECK(test_sparse_extension_truncate_and_view_integrity() == 0);
    CHECK(test_capability_authorized_buffer_flow() == 0);
    CHECK(test_ring_bounds_and_integrity() == 0);
    (void)puts("buffer protocol unit tests: PASS");
    return 0;
}
