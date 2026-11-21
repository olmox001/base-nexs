/* Product port of pinned NXOLMO external/references/nxolmo/src/buffer/buffer.c; no reference linkage. */
#include "nexs_buffer.h"

#include "nexs_checked.h"
#include "nexs_crypto.h"
#include "nexs_crypto.h"

#include <stddef.h>
#include <string.h>

#define NX_BUFFER_KNOWN_FLAGS (NX_BUFFER_FLAG_ZERO_ON_ALLOC | \
                               NX_BUFFER_FLAG_IMMUTABLE | \
                               NX_BUFFER_FLAG_CONTENT_ADDRESSED)
#define NX_BUFFER_SLOT_ACTIVE UINT32_C(1)
#define NX_BUFFER_LEASE_READ UINT32_C(1)
#define NX_BUFFER_LEASE_WRITE UINT32_C(2)
#define NX_BUFFER_RING_KNOWN_FLAGS UINT32_C(0)

static int nx_object_ref_is_zero(nx_object_ref reference) {
    return reference.id.high == 0u && reference.id.low == 0u &&
           reference.generation == 0u;
}

static int nx_is_power_of_two_u64(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}


static nx_status nx_buffer_result_init(nx_result *result, nx_status status,
                                       uint64_t operation_id,
                                       uint64_t detail) {
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    (void)nx_result_init(result, status, NX_RESULT_DOMAIN_PROTOCOL,
                         operation_id, 0u, detail, NX_CYCLES_UNKNOWN, 0u);
    return status;
}

static nx_status nx_buffer_scope_begin(nx_operation_scope *scope,
                                       nx_operation_metrics *metrics,
                                       const nx_cycle_counter *counter,
                                       uint64_t operation_id,
                                       uint64_t fuel_limit) {
    return nx_operation_scope_begin_with_counter(
        scope, metrics, counter, operation_id, fuel_limit, UINT64_MAX);
}

static nx_status nx_buffer_fuel_add_scaled(uint64_t *total, uint64_t value,
                                           uint64_t factor) {
    uint64_t term;
    nx_status status = nx_u64_mul(value, factor, &term);
    return status == NX_STATUS_OK ? nx_u64_add(*total, term, total) : status;
}

static nx_status nx_buffer_fuel_bound(const nx_buffer_manager *manager,
                                      uint64_t byte_work,
                                      uint64_t granule_factor,
                                      uint64_t slot_factor,
                                      uint64_t lease_factor,
                                      uint64_t registry_factor,
                                      uint64_t fixed_work,
                                      uint64_t *fuel_limit) {
    uint64_t total = byte_work;
    nx_status status;
    if (manager == NULL || manager->registry == NULL || fuel_limit == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_buffer_fuel_add_scaled(&total, manager->granule_count,
                                       granule_factor);
    if (status == NX_STATUS_OK)
        status = nx_buffer_fuel_add_scaled(&total, manager->slot_capacity,
                                           slot_factor);
    if (status == NX_STATUS_OK)
        status = nx_buffer_fuel_add_scaled(&total, manager->lease_capacity,
                                           lease_factor);
    if (status == NX_STATUS_OK)
        status = nx_buffer_fuel_add_scaled(&total,
                                           manager->registry->index_capacity,
                                           registry_factor);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(total, fixed_work, &total);
    if (status == NX_STATUS_OK)
        *fuel_limit = total;
    return status;
}

static nx_status nx_buffer_scope_finish_bytes(nx_operation_scope *scope,
                                              nx_status status,
                                              uint64_t completed,
                                              uint64_t detail,
                                              uint64_t bytes_used,
                                              nx_result *result) {
    return nx_operation_scope_finish_ex(scope, status,
                                        NX_RESULT_DOMAIN_PROTOCOL,
                                        completed, detail, bytes_used, result);
}

static nx_status nx_buffer_scope_finish(nx_operation_scope *scope,
                                        nx_status status,
                                        uint64_t completed,
                                        uint64_t detail,
                                        nx_result *result) {
    return nx_buffer_scope_finish_bytes(scope, status, completed, detail, 0u,
                                        result);
}

static int nx_buffer_manager_try_lock(nx_buffer_manager *manager) {
    uint32_t expected = 0u;
    return atomic_compare_exchange_strong_explicit(
        &manager->lock, &expected, 1u,
        memory_order_acquire, memory_order_relaxed);
}

static void nx_buffer_manager_unlock(nx_buffer_manager *manager) {
    atomic_store_explicit(&manager->lock, 0u, memory_order_release);
}

static nx_status nx_buffer_create_finish(nx_operation_scope *scope,
                                         nx_status status, uint64_t capacity,
                                         uint64_t allocated_bytes,
                                         nx_result *result) {
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? 1u : 0u,
        status == NX_STATUS_OK ? capacity : 0u,
        status == NX_STATUS_OK ? allocated_bytes : 0u, result);
}

static nx_status nx_buffer_create_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t capacity, uint64_t allocated_bytes, nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_create_finish(scope, status, capacity, allocated_bytes,
                                   result);
}

static nx_status nx_buffer_acquire_finish(nx_operation_scope *scope,
                                          nx_status status, uint64_t length,
                                          uint64_t lease_id, nx_result *result) {
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? length : 0u,
        status == NX_STATUS_OK ? lease_id : 0u,
        status == NX_STATUS_OK ? length : 0u, result);
}

static nx_status nx_buffer_acquire_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t length, uint64_t lease_id, nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_acquire_finish(scope, status, length, lease_id, result);
}

static nx_status nx_buffer_destroy_finish(nx_operation_scope *scope,
                                          nx_status status,
                                          uint64_t destroyed_bytes,
                                          nx_result *result) {
    return nx_buffer_scope_finish_bytes(scope, status,
                                        status == NX_STATUS_OK ? 1u : 0u,
                                        0u,
                                        status == NX_STATUS_OK ? destroyed_bytes : 0u,
                                        result);
}

static nx_status nx_buffer_destroy_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t destroyed_bytes, nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_destroy_finish(scope, status, destroyed_bytes, result);
}

static nx_status nx_buffer_stats_finish(
    nx_operation_scope *scope, nx_status status, uint64_t active,
    uint64_t allocated, nx_buffer_stats *stats, nx_result *result) {
    if (status != NX_STATUS_OK && stats != NULL)
        memset(stats, 0, sizeof(*stats));
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? active : 0u,
        status == NX_STATUS_OK ? allocated : 0u,
        status == NX_STATUS_OK ? allocated : 0u, result);
}

static nx_status nx_buffer_stats_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t active, uint64_t allocated, nx_buffer_stats *stats,
    nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_stats_finish(scope, status, active, allocated, stats,
                                  result);
}


static int nx_buffer_external_pointer_valid(const nx_buffer_manager *manager,
                                            const void *pointer, size_t size,
                                            size_t alignment) {
    int overlap;
    if (manager == NULL || pointer == NULL || size == 0u || alignment == 0u ||
        ((uintptr_t)pointer % (uintptr_t)alignment) != 0u)
        return 0;
#define NX_BUFFER_EXTERNAL_CHECK(base, bytes) \
    do { \
        overlap = nx_pointer_ranges_overlap(pointer, size, (base), (bytes)); \
        if (overlap != 0) return 0; \
    } while (0)
    NX_BUFFER_EXTERNAL_CHECK(manager, sizeof(*manager));
    NX_BUFFER_EXTERNAL_CHECK(manager->registry, sizeof(*manager->registry));
    NX_BUFFER_EXTERNAL_CHECK(manager->slots,
                             (size_t)manager->slot_capacity * sizeof(*manager->slots));
    NX_BUFFER_EXTERNAL_CHECK(manager->leases,
                             (size_t)manager->lease_capacity * sizeof(*manager->leases));
    NX_BUFFER_EXTERNAL_CHECK(manager->arena, (size_t)manager->arena_size);
    NX_BUFFER_EXTERNAL_CHECK(manager->allocation_bitmap,
                             (size_t)(((uint64_t)manager->granule_count + 7u) / 8u));
    NX_BUFFER_EXTERNAL_CHECK(manager->registry->slots,
                             (size_t)manager->registry->slot_capacity *
                             sizeof(*manager->registry->slots));
    NX_BUFFER_EXTERNAL_CHECK(manager->registry->index,
                             (size_t)manager->registry->index_capacity *
                             sizeof(*manager->registry->index));
    NX_BUFFER_EXTERNAL_CHECK(manager->registry->rehash_index,
                             (size_t)manager->registry->index_capacity *
                             sizeof(*manager->registry->rehash_index));
#undef NX_BUFFER_EXTERNAL_CHECK
    return 1;
}

static nx_status nx_buffer_manager_ready(nx_buffer_manager *manager,
                                         nx_result *result,
                                         uint64_t operation_id) {
    if (manager == NULL ||
        atomic_load_explicit(&manager->initialized, memory_order_acquire) == 0u) {
        (void)nx_buffer_result_init(result, NX_STATUS_INVALID_ARGUMENT,
                                    operation_id, 0u);
        return NX_STATUS_INVALID_ARGUMENT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_authorize(const nx_buffer_authority *authority,
                                     nx_object_ref target,
                                     uint64_t rights) {
    if (authority == NULL || authority->authorize == NULL ||
        !nx_object_ref_is_valid(authority->principal) || rights == 0u)
        return NX_STATUS_ACCESS_DENIED;
    return authority->authorize(authority->context, authority->principal,
                                target, rights, authority->epoch);
}

static int nx_bitmap_get(const uint8_t *bitmap, uint64_t index) {
    return (bitmap[(size_t)(index >> 3u)] & (uint8_t)(UINT8_C(1) << (index & 7u))) != 0u;
}

static void nx_bitmap_set(uint8_t *bitmap, uint64_t index) {
    bitmap[(size_t)(index >> 3u)] |= (uint8_t)(UINT8_C(1) << (index & 7u));
}

static void nx_bitmap_clear(uint8_t *bitmap, uint64_t index) {
    bitmap[(size_t)(index >> 3u)] &= (uint8_t)~(uint8_t)(UINT8_C(1) << (index & 7u));
}

static nx_status nx_bitmap_count(const uint8_t *bitmap, uint64_t count,
                                 nx_operation_scope *scope, uint64_t *used) {
    uint64_t i;
    if (bitmap == NULL || scope == NULL || used == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    *used = 0u;
    for (i = 0u; i < count; ++i) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (nx_bitmap_get(bitmap, i) != 0)
            (*used)++;
    }
    return NX_STATUS_OK;
}

/* CRC64 is retained as a fast corruption sentinel. The canonical registry
 * stores SHA-256; neither digest is an authenticator or capability token. */
nx_status nx_buffer_crc64_ecma(nx_const_span bytes, uint64_t *checksum) {
    return nx_crc64_ecma(bytes, checksum);
}

static void nx_buffer_make_node_spec(const nx_buffer_slot *slot,
                                     uint32_t slot_index,
                                     uint64_t logical_length,
                                     const nx_digest256 *digest,
                                     nx_state_node_spec *spec) {
    memset(spec, 0, sizeof(*spec));
    spec->owner = slot->owner;
    spec->type_id = NX_BUFFER_TYPE_ID;
    spec->schema_id = NX_BUFFER_SCHEMA_ID;
    spec->schema_version = NX_BUFFER_SCHEMA_VERSION;
    spec->lifecycle = (slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u
                          ? NX_STATE_LIFECYCLE_FROZEN
                          : NX_STATE_LIFECYCLE_LIVE;
    spec->flags = (slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u
                      ? NX_STATE_NODE_FLAG_IMMUTABLE
                      : 0u;
    spec->payload_offset = slot_index;
    spec->payload_length = logical_length;
    if (digest != NULL)
        spec->payload_digest = *digest;
}

static int nx_buffer_slot_geometry_valid(const nx_buffer_manager *manager,
                                         const nx_buffer_slot *slot) {
    uint64_t start;
    if (slot->allocation_granules == 0u ||
        slot->allocation_granules > manager->granule_count ||
        slot->allocation_granules > UINT64_MAX / manager->granule_size ||
        slot->arena_offset % manager->granule_size != 0u ||
        slot->allocation_bytes !=
            (uint64_t)slot->allocation_granules * manager->granule_size ||
        slot->allocation_bytes > manager->arena_size ||
        slot->arena_offset > manager->arena_size - slot->allocation_bytes ||
        slot->capacity == 0u || slot->capacity > slot->allocation_bytes ||
        slot->logical_length > slot->capacity ||
        (slot->flags & ~(uint32_t)NX_BUFFER_KNOWN_FLAGS) != 0u ||
        (slot->flags & NX_BUFFER_FLAG_CONTENT_ADDRESSED) != 0u)
        return 0;
    start = slot->arena_offset / manager->granule_size;
    if (start > manager->granule_count - slot->allocation_granules)
        return 0;
    return 1;
}

static nx_status nx_buffer_find_locked(nx_buffer_manager *manager,
                                       nx_object_ref reference,
                                       uint32_t *slot_index,
                                       nx_state_node *node) {
    nx_result registry_result;
    nx_status status;
    uint64_t index;
    nx_buffer_slot *slot;

    status = nx_state_store_read(manager->registry, reference, node,
                                 &registry_result);
    if (status != NX_STATUS_OK)
        return status;
    if (node->spec.type_id != NX_BUFFER_TYPE_ID ||
        node->spec.schema_id != NX_BUFFER_SCHEMA_ID ||
        node->spec.schema_version != NX_BUFFER_SCHEMA_VERSION ||
        node->spec.payload_offset >= manager->slot_capacity)
        return NX_STATUS_CORRUPT;
    index = node->spec.payload_offset;
    slot = &manager->slots[(size_t)index];
    if ((slot->state != NX_BUFFER_STATE_ACTIVE &&
         slot->state != NX_BUFFER_STATE_SEALED &&
         slot->state != NX_BUFFER_STATE_QUARANTINED) ||
        !nx_buffer_slot_geometry_valid(manager, slot) ||
        !nx_object_ref_equal(slot->reference, reference) ||
        !nx_object_ref_equal(slot->owner, node->spec.owner) ||
        slot->registry_revision != node->revision ||
        slot->logical_length != node->spec.payload_length ||
        memcmp(slot->digest.bytes, node->spec.payload_digest.bytes,
               sizeof(slot->digest.bytes)) != 0 ||
        node->spec.flags != ((slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u
                                 ? NX_STATE_NODE_FLAG_IMMUTABLE : 0u) ||
        (slot->state == NX_BUFFER_STATE_QUARANTINED
             ? (node->spec.lifecycle != NX_STATE_LIFECYCLE_QUARANTINED &&
                node->spec.lifecycle != NX_STATE_LIFECYCLE_LIVE &&
                node->spec.lifecycle != NX_STATE_LIFECYCLE_FROZEN)
             : node->spec.lifecycle !=
                   ((slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u
                        ? NX_STATE_LIFECYCLE_FROZEN
                        : NX_STATE_LIFECYCLE_LIVE)))
        return NX_STATUS_CORRUPT;
    *slot_index = (uint32_t)index;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_find_free_extent(nx_buffer_manager *manager,
                                            uint64_t needed_granules,
                                            uint64_t alignment,
                                            uint64_t *start_index,
                                            nx_operation_scope *scope) {
    uint64_t start;
    if (needed_granules == 0u || needed_granules > manager->granule_count)
        return NX_STATUS_NO_SPACE;
    for (start = 0u; start <= manager->granule_count - needed_granules;
         ++start) {
        uint64_t byte_offset = (uint64_t)start * manager->granule_size;
        uint64_t j;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if ((((uint64_t)(uintptr_t)manager->arena + byte_offset) &
             (alignment - 1u)) != 0u)
            continue;
        uint32_t occupied = 0u;
        for (j = 0u; j < needed_granules && occupied == 0u; ++j) {
            status = nx_operation_scope_consume(scope, 1u);
            if (status != NX_STATUS_OK)
                return status;
            occupied = (uint32_t)nx_bitmap_get(
                manager->allocation_bitmap, start + j);
        }
        if (occupied == 0u) {
            *start_index = start;
            return NX_STATUS_OK;
        }
        if (j > 1u)
            start += j - 1u;
    }
    return NX_STATUS_NO_SPACE;
}

static uint64_t nx_buffer_release_extent(nx_buffer_manager *manager,
                                         const nx_buffer_slot *slot) {
    uint64_t i;
    uint64_t start = slot->arena_offset / manager->granule_size;
    for (i = 0u; i < slot->allocation_granules; ++i)
        nx_bitmap_clear(manager->allocation_bitmap, start + i);
    return slot->allocation_granules;
}

static nx_status nx_buffer_has_active_lease_locked(
    nx_buffer_manager *manager, nx_object_ref reference,
    nx_operation_scope *scope, int *has_lease) {
    uint32_t i;
    if (has_lease == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    *has_lease = 0;
    for (i = 0u; i < manager->lease_capacity; ++i) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (manager->leases[i].active != 0u &&
            nx_object_ref_equal(manager->leases[i].buffer, reference)) {
            *has_lease = 1;
            return NX_STATUS_OK;
        }
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_find_lease_locked(nx_buffer_manager *manager,
                                             const nx_buffer_view *view,
                                             nx_object_ref principal,
                                             uint32_t *lease_index,
                                             nx_operation_scope *scope) {
    uint32_t i;
    if (view->slot_index >= manager->slot_capacity)
        return NX_STATUS_STALE_REFERENCE;
    for (i = 0u; i < manager->lease_capacity; ++i) {
        nx_buffer_lease_slot *lease = &manager->leases[i];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (lease->active != 0u && lease->lease_id == view->lease_id &&
            lease->buffer_slot == view->slot_index &&
            nx_object_ref_equal(lease->buffer, view->buffer) &&
            nx_object_ref_equal(lease->principal, principal) &&
            lease->offset == view->offset && lease->length == view->length &&
            lease->rights == view->rights && lease->reserved == 0u &&
            view->reserved == 0u) {
            *lease_index = i;
            return NX_STATUS_OK;
        }
    }
    return NX_STATUS_STALE_REFERENCE;
}

static int nx_buffer_view_span_valid(const nx_buffer_manager *manager,
                                     const nx_buffer_slot *slot,
                                     const nx_buffer_view *view,
                                     int write_access) {
    uint64_t absolute_offset;
    const uint8_t *expected;
    if (view->offset > slot->capacity ||
        view->length > slot->capacity - view->offset ||
        view->offset > UINT64_MAX - slot->arena_offset ||
        slot->arena_offset + view->offset > manager->arena_size ||
        view->length > manager->arena_size -
                           (slot->arena_offset + view->offset))
        return 0;
    absolute_offset = slot->arena_offset + view->offset;
    expected = manager->arena + (size_t)absolute_offset;
    if (write_access != 0)
        return view->write_bytes.data == expected &&
               view->write_bytes.size == (size_t)view->length &&
               view->read_bytes.data == NULL && view->read_bytes.size == 0u;
    return view->read_bytes.data == expected &&
           view->read_bytes.size == (size_t)view->length &&
           view->write_bytes.data == NULL && view->write_bytes.size == 0u;
}

static void nx_buffer_release_lease_locked(nx_buffer_manager *manager,
                                           uint32_t lease_index) {
    nx_buffer_lease_slot *lease = &manager->leases[lease_index];
    if (lease->buffer_slot < manager->slot_capacity) {
        nx_buffer_slot *slot = &manager->slots[lease->buffer_slot];
        if (slot->lease_count != 0u)
            slot->lease_count--;
        if (slot->pinned_bytes >= lease->length)
            slot->pinned_bytes -= lease->length;
        else
            slot->state = NX_BUFFER_STATE_QUARANTINED;
    }
    memset(lease, 0, sizeof(*lease));
}

static nx_status nx_buffer_quarantine_locked(nx_buffer_manager *manager,
                                             uint32_t slot_index,
                                             nx_state_node *node) {
    nx_buffer_slot *slot = &manager->slots[slot_index];
    nx_state_node_spec spec;
    nx_state_node updated;
    nx_result registry_result;
    nx_status status = NX_STATUS_CONFLICT;
    uint32_t attempt;

    slot->state = NX_BUFFER_STATE_QUARANTINED;
    for (attempt = 0u; attempt < 3u; ++attempt) {
        nx_buffer_make_node_spec(slot, slot_index, slot->logical_length,
                                 &slot->digest, &spec);
        spec.lifecycle = NX_STATE_LIFECYCLE_QUARANTINED;
        status = nx_state_store_update(manager->registry, slot->reference,
                                       node->revision, &spec, &updated,
                                       &registry_result);
        if (status == NX_STATUS_OK) {
            slot->registry_revision = updated.revision;
            *node = updated;
            return NX_STATUS_OK;
        }
        if (status != NX_STATUS_CONFLICT)
            return status;
        status = nx_state_store_read(manager->registry, slot->reference, node,
                                     &registry_result);
        if (status != NX_STATUS_OK ||
            node->spec.type_id != NX_BUFFER_TYPE_ID ||
            node->spec.schema_id != NX_BUFFER_SCHEMA_ID ||
            !nx_object_ref_equal(node->spec.owner, slot->owner) ||
            node->spec.payload_offset != slot_index)
            return NX_STATUS_CORRUPT;
    }
    return status;
}

typedef struct {
    const void *data;
    size_t size;
} nx_buffer_memory_range;

static nx_status nx_buffer_ranges_disjoint(
    const nx_buffer_memory_range *ranges, uint32_t range_count) {
    uint32_t left;
    for (left = 0u; left < range_count; ++left) {
        uint32_t right;
        for (right = left + 1u; right < range_count; ++right) {
            int overlap = nx_pointer_ranges_overlap(
                ranges[left].data, ranges[left].size,
                ranges[right].data, ranges[right].size);
            if (overlap < 0)
                return NX_STATUS_OVERFLOW;
            if (overlap != 0)
                return NX_STATUS_INVALID_ARGUMENT;
        }
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_manager_init_geometry(
    nx_buffer_manager *manager, nx_state_store *registry,
    nx_buffer_slot *slots, uint32_t slot_capacity,
    nx_buffer_lease_slot *leases, uint32_t lease_capacity,
    uint8_t *arena, uint64_t arena_size, uint64_t granule_size,
    uint8_t *allocation_bitmap, size_t allocation_bitmap_size,
    uint64_t *granules, size_t *required_bitmap) {
    if (manager == NULL || registry == NULL || slots == NULL ||
        leases == NULL || arena == NULL || allocation_bitmap == NULL ||
        slot_capacity == 0u || lease_capacity == 0u || arena_size == 0u ||
        arena_size > (uint64_t)SIZE_MAX ||
        !nx_is_power_of_two_u64(granule_size) ||
        granule_size < (uint64_t)_Alignof(max_align_t) ||
        arena_size % granule_size != 0u ||
        ((uintptr_t)arena % (uintptr_t)granule_size) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    *granules = arena_size / granule_size;
    if (*granules == 0u || slot_capacity > registry->slot_capacity)
        return NX_STATUS_INVALID_ARGUMENT;
    {
        uint64_t required = *granules / 8u +
                            ((*granules % 8u) != 0u ? 1u : 0u);
        if (required > (uint64_t)SIZE_MAX)
            return NX_STATUS_OVERFLOW;
        *required_bitmap = (size_t)required;
    }
    if (allocation_bitmap_size < *required_bitmap)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_manager_init_ranges(
    nx_buffer_manager *manager, nx_state_store *registry,
    nx_buffer_slot *slots, uint32_t slot_capacity,
    nx_buffer_lease_slot *leases, uint32_t lease_capacity,
    uint8_t *arena, uint64_t arena_size, uint8_t *allocation_bitmap,
    size_t required_bitmap, size_t *slots_bytes, size_t *leases_bytes) {
    nx_buffer_memory_range ranges[9];
    size_t registry_slots_bytes;
    size_t registry_index_bytes;
    nx_status status;
    status = nx_size_mul((size_t)slot_capacity, sizeof(*slots), slots_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_size_mul((size_t)lease_capacity, sizeof(*leases), leases_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_size_mul((size_t)registry->slot_capacity,
                         sizeof(*registry->slots), &registry_slots_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_size_mul((size_t)registry->index_capacity,
                         sizeof(*registry->index), &registry_index_bytes);
    if (status != NX_STATUS_OK)
        return status;
    if (((uintptr_t)slots % (uintptr_t)_Alignof(nx_buffer_slot)) != 0u ||
        ((uintptr_t)leases % (uintptr_t)_Alignof(nx_buffer_lease_slot)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    ranges[0] = (nx_buffer_memory_range){manager, sizeof(*manager)};
    ranges[1] = (nx_buffer_memory_range){slots, *slots_bytes};
    ranges[2] = (nx_buffer_memory_range){leases, *leases_bytes};
    ranges[3] = (nx_buffer_memory_range){arena, (size_t)arena_size};
    ranges[4] = (nx_buffer_memory_range){allocation_bitmap, required_bitmap};
    ranges[5] = (nx_buffer_memory_range){registry, sizeof(*registry)};
    ranges[6] = (nx_buffer_memory_range){registry->slots, registry_slots_bytes};
    ranges[7] = (nx_buffer_memory_range){registry->index, registry_index_bytes};
    ranges[8] = (nx_buffer_memory_range){registry->rehash_index,
                                         registry_index_bytes};
    return nx_buffer_ranges_disjoint(ranges, 9u);
}

static nx_status nx_buffer_manager_init_scope(
    nx_buffer_manager *manager, nx_cycle_counter *counter,
    size_t slots_bytes, size_t leases_bytes, uint64_t arena_size,
    size_t bitmap_bytes, nx_operation_scope *scope) {
    nx_status status;
    uint64_t work;
    status = nx_u64_add((uint64_t)slots_bytes, (uint64_t)leases_bytes,
                        &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, arena_size, &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, (uint64_t)bitmap_bytes, &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, UINT64_C(32), &work);
    if (status != NX_STATUS_OK)
        return status;
    atomic_init(&manager->lock, 0u);
    atomic_init(&manager->initialized, 0u);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_INIT],
                              NX_OP_BUFFER_MANAGER_INIT);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_CREATE],
                              NX_OP_BUFFER_CREATE);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_ACQUIRE],
                              NX_OP_BUFFER_ACQUIRE);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_COMMIT],
                              NX_OP_BUFFER_VIEW_COMMIT);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_RELEASE],
                              NX_OP_BUFFER_VIEW_RELEASE);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_VERIFY],
                              NX_OP_BUFFER_VERIFY);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_DESTROY],
                              NX_OP_BUFFER_DESTROY);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_VALIDATE],
                              NX_OP_BUFFER_MANAGER_VALIDATE);
    nx_operation_metrics_init(&manager->metrics[NX_BUFFER_METRIC_STATS],
                              NX_OP_BUFFER_STATS_READ);
    status = nx_cycle_counter_init(counter);
    if (status != NX_STATUS_OK && status != NX_STATUS_NOT_SUPPORTED)
        return status;
    manager->cycle_counter = *counter;
    return nx_buffer_scope_begin(
        scope, &manager->metrics[NX_BUFFER_METRIC_INIT],
        &manager->cycle_counter, NX_OP_BUFFER_MANAGER_INIT, work);
}

static nx_status nx_buffer_manager_init_publish(
    nx_buffer_manager *manager, nx_state_store *registry,
    nx_buffer_slot *slots, uint32_t slot_capacity,
    nx_buffer_lease_slot *leases, uint32_t lease_capacity,
    uint8_t *arena, uint64_t arena_size, uint64_t granule_size,
    uint8_t *allocation_bitmap, size_t allocation_bitmap_size,
    size_t slots_bytes, size_t leases_bytes, uint64_t granules,
    size_t required_bitmap, nx_operation_scope *scope, nx_result *result) {
    uint64_t work;
    nx_status status = nx_u64_add((uint64_t)slots_bytes,
                                  (uint64_t)leases_bytes, &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, arena_size, &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, (uint64_t)required_bitmap, &work);
    if (status == NX_STATUS_OK)
        status = nx_u64_add(work, UINT64_C(32), &work);
    if (status != NX_STATUS_OK)
        return nx_buffer_scope_finish(scope, status, 0u, 0u, result);
    status = nx_operation_scope_consume(scope, work);
    if (status != NX_STATUS_OK)
        return nx_buffer_scope_finish(scope, status, 0u, 0u, result);
    memset(slots, 0, slots_bytes);
    memset(leases, 0, leases_bytes);
    memset(arena, 0, (size_t)arena_size);
    memset(allocation_bitmap, 0, required_bitmap);
    manager->registry = registry;
    manager->slots = slots;
    manager->slot_capacity = slot_capacity;
    manager->leases = leases;
    manager->lease_capacity = lease_capacity;
    manager->arena = arena;
    manager->arena_size = arena_size;
    manager->granule_size = granule_size;
    manager->granule_count = granules;
    manager->allocation_bitmap = allocation_bitmap;
    manager->allocation_bitmap_size = allocation_bitmap_size;
    manager->next_lease_id = 1u;
    atomic_store_explicit(&manager->initialized, 1u, memory_order_release);
    return nx_buffer_scope_finish(scope, NX_STATUS_OK, 1u, granules, result);
}

nx_status nx_buffer_manager_init(
    nx_buffer_manager *manager, nx_state_store *registry,
    nx_buffer_slot *slots, uint32_t slot_capacity,
    nx_buffer_lease_slot *leases, uint32_t lease_capacity,
    uint8_t *arena, uint64_t arena_size, uint64_t granule_size,
    uint8_t *allocation_bitmap, size_t allocation_bitmap_size,
    nx_result *result) {
    nx_operation_scope scope;
    nx_cycle_counter counter;
    nx_state_store_stats registry_stats;
    nx_result registry_result;
    uint64_t granules;
    size_t required_bitmap;
    size_t slots_bytes;
    size_t leases_bytes;
    nx_status status;
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_buffer_manager_init_geometry(
        manager, registry, slots, slot_capacity, leases, lease_capacity,
        arena, arena_size, granule_size, allocation_bitmap,
        allocation_bitmap_size, &granules, &required_bitmap);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_MANAGER_INIT, 0u);
    status = nx_buffer_manager_init_ranges(
        manager, registry, slots, slot_capacity, leases, lease_capacity,
        arena, arena_size, allocation_bitmap, required_bitmap,
        &slots_bytes, &leases_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_MANAGER_INIT, 0u);
    status = nx_state_store_stats_read(registry, &registry_stats,
                                       &registry_result);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_MANAGER_INIT, 0u);
    if (registry_stats.slot_capacity < slot_capacity)
        return nx_buffer_result_init(result, NX_STATUS_BUSY,
                                     NX_OP_BUFFER_MANAGER_INIT, 0u);
    status = nx_buffer_manager_init_scope(
        manager, &counter, slots_bytes, leases_bytes, arena_size,
        required_bitmap, &scope);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_MANAGER_INIT, 0u);
    return nx_buffer_manager_init_publish(
        manager, registry, slots, slot_capacity, leases, lease_capacity,
        arena, arena_size, granule_size, allocation_bitmap,
        allocation_bitmap_size, slots_bytes, leases_bytes, granules,
        required_bitmap, &scope, result);
}

static nx_status nx_buffer_create_validate_request(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    uint64_t capacity, uint64_t alignment, uint32_t flags,
    uint64_t *needed, uint64_t *allocated_bytes) {
    uint64_t needed64;
    nx_status status;
    if (capacity == 0u || !nx_is_power_of_two_u64(alignment) ||
        alignment < manager->granule_size ||
        alignment % manager->granule_size != 0u ||
        (flags & ~(uint32_t)NX_BUFFER_KNOWN_FLAGS) != 0u ||
        (flags & NX_BUFFER_FLAG_CONTENT_ADDRESSED) != 0u)
        return (flags & NX_BUFFER_FLAG_CONTENT_ADDRESSED) != 0u
                   ? NX_STATUS_NOT_SUPPORTED : NX_STATUS_INVALID_ARGUMENT;
    status = nx_buffer_authorize(
        authority, (nx_object_ref){{0u, 0u}, 0u}, NX_BUFFER_RIGHT_CREATE);
    if (status != NX_STATUS_OK)
        return status;
    if (capacity > UINT64_MAX - (manager->granule_size - 1u))
        return NX_STATUS_OVERFLOW;
    needed64 = (capacity + manager->granule_size - 1u) /
               manager->granule_size;
    if (needed64 == 0u || needed64 > manager->granule_count)
        return NX_STATUS_NO_SPACE;
    if (needed64 > UINT64_MAX / manager->granule_size)
        return NX_STATUS_OVERFLOW;
    *needed = needed64;
    *allocated_bytes = needed64 * manager->granule_size;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_create_find_slot_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    uint32_t *slot_index) {
    uint32_t index;
    for (index = 0u; index < manager->slot_capacity; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (manager->slots[index].state == NX_BUFFER_STATE_FREE) {
            *slot_index = index;
            return NX_STATUS_OK;
        }
    }
    return NX_STATUS_RESOURCE_EXHAUSTED;
}

static nx_status nx_buffer_create_reserve_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    const nx_buffer_authority *authority, uint64_t capacity,
    uint64_t alignment, uint32_t flags, uint64_t needed,
    uint32_t *slot_index, uint64_t *start) {
    uint64_t index;
    nx_status status = nx_buffer_create_find_slot_locked(
        manager, scope, slot_index);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_find_free_extent(
        manager, needed, alignment, start, scope);
    if (status != NX_STATUS_OK)
        return status;
    for (index = 0u; index < needed; ++index) {
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK) {
            while (index != 0u) {
                --index;
                nx_bitmap_clear(manager->allocation_bitmap, *start + index);
            }
            return status;
        }
        nx_bitmap_set(manager->allocation_bitmap, *start + index);
    }
    status = nx_operation_scope_consume(scope, 1u);
    if (status != NX_STATUS_OK) {
        for (index = 0u; index < needed; ++index)
            nx_bitmap_clear(manager->allocation_bitmap, *start + index);
        return status;
    }
    {
        nx_buffer_slot *slot = &manager->slots[*slot_index];
        memset(slot, 0, sizeof(*slot));
        slot->arena_offset = (uint64_t)*start * manager->granule_size;
        slot->capacity = capacity;
        slot->allocation_bytes = (uint64_t)needed * manager->granule_size;
        slot->allocation_granules = needed;
        slot->flags = flags;
        slot->owner = authority->principal;
        slot->state = NX_BUFFER_STATE_ACTIVE;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_create_rollback_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_slot *slot, nx_status failure) {
    uint64_t cleanup_work;
    uint64_t released_granules;
    nx_status cleanup_status = nx_u64_add(
        slot->allocation_bytes, slot->allocation_granules, &cleanup_work);
    if (cleanup_status == NX_STATUS_OK)
        cleanup_status = nx_u64_add(cleanup_work, UINT64_C(1), &cleanup_work);
    if (cleanup_status == NX_STATUS_OK)
        cleanup_status = nx_operation_scope_consume(scope, cleanup_work);
    /* Scrubbing is mandatory even when the work budget itself is exhausted. */
    memset(manager->arena + (size_t)slot->arena_offset, 0,
           (size_t)slot->allocation_bytes);
    released_granules = nx_buffer_release_extent(manager, slot);
    if (cleanup_status == NX_STATUS_OK)
        cleanup_status = nx_operation_scope_consume(scope, released_granules);
    memset(slot, 0, sizeof(*slot));
    return cleanup_status == NX_STATUS_OK ? failure : cleanup_status;
}

static nx_status nx_buffer_create_publish_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    uint32_t slot_index, nx_object_ref *created,
    nx_result *registry_result) {
    nx_buffer_slot *slot = &manager->slots[slot_index];
    nx_state_node_spec spec;
    nx_object_ref reference;
    nx_digest256 digest;
    uint64_t checksum;
    nx_status status = nx_operation_scope_consume(scope,
                                                  slot->allocation_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_rollback_locked(manager, scope, slot, status);
    memset(manager->arena + (size_t)slot->arena_offset, 0,
           (size_t)slot->allocation_bytes);
    status = nx_buffer_crc64_ecma((nx_const_span){NULL, 0u}, &checksum);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_rollback_locked(manager, scope, slot, status);
    status = nx_sha256_compute((nx_const_span){NULL, 0u}, &digest);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_rollback_locked(manager, scope, slot, status);
    slot->checksum = checksum;
    slot->digest = digest;
    nx_buffer_make_node_spec(slot, slot_index, 0u, &digest, &spec);
    status = nx_state_store_create(manager->registry, &spec, &reference,
                                   registry_result);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_rollback_locked(manager, scope, slot, status);
    slot->reference = reference;
    slot->registry_revision = 1u;
    *created = reference;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_create_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    const nx_buffer_authority *authority, uint64_t capacity,
    uint64_t alignment, uint32_t flags, uint64_t needed,
    nx_object_ref *created, nx_result *registry_result) {
    uint32_t slot_index = NX_BUFFER_NO_SLOT;
    uint64_t start = 0u;
    nx_status status = nx_buffer_authorize(
        authority, (nx_object_ref){{0u, 0u}, 0u}, NX_BUFFER_RIGHT_CREATE);
    if (status == NX_STATUS_OK)
        status = nx_buffer_create_reserve_locked(
            manager, scope, authority, capacity, alignment, flags, needed,
            &slot_index, &start);
    if (status == NX_STATUS_OK)
        status = nx_buffer_create_publish_locked(
            manager, scope, slot_index, created, registry_result);
    return status;
}

static nx_status nx_buffer_create_scope_begin(
    nx_buffer_manager *manager, nx_operation_scope *scope) {
    uint64_t fuel_limit, byte_work;
    nx_status status = nx_u64_mul(manager->arena_size, UINT64_C(2),
                                  &byte_work);
    if (status == NX_STATUS_OK)
        status = nx_buffer_fuel_bound(manager, byte_work, 4u, 1u,
                                      0u, 1u, UINT64_C(32), &fuel_limit);
    if (status != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    return nx_buffer_scope_begin(
        scope, &manager->metrics[NX_BUFFER_METRIC_CREATE],
        &manager->cycle_counter, NX_OP_BUFFER_CREATE, fuel_limit);
}

nx_status nx_buffer_create(nx_buffer_manager *manager,
                           const nx_buffer_authority *authority,
                           uint64_t capacity, uint64_t alignment,
                           uint32_t flags, nx_object_ref *created,
                           nx_result *result) {
    nx_operation_scope scope;
    nx_result registry_result;
    uint64_t allocated_bytes = 0u;
    uint64_t needed = 0u;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_CREATE);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_create_scope_begin(manager, &scope);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_CREATE, 0u);
    if (!nx_buffer_external_pointer_valid(manager, created, sizeof(*created),
                                          _Alignof(nx_object_ref)))
        return nx_buffer_create_finish(&scope, NX_STATUS_INVALID_ARGUMENT,
                                       capacity, allocated_bytes, result);
    memset(created, 0, sizeof(*created));
    status = nx_buffer_create_validate_request(
        manager, authority, capacity, alignment, flags, &needed,
        &allocated_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_finish(&scope, status, capacity,
                                       allocated_bytes, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_create_finish(&scope, NX_STATUS_BUSY, capacity,
                                       allocated_bytes, result);
    status = nx_buffer_create_locked(
        manager, &scope, authority, capacity, alignment, flags, needed,
        created, &registry_result);
    if (status != NX_STATUS_OK)
        return nx_buffer_create_unlock_finish(
            manager, &scope, status, capacity, allocated_bytes, result);
    nx_buffer_manager_unlock(manager);
    return nx_buffer_create_finish(&scope, NX_STATUS_OK, capacity,
                                   allocated_bytes, result);
}

static nx_status nx_buffer_acquire_validate_request(
    nx_buffer_manager *manager, nx_object_ref reference, uint64_t offset,
    uint64_t length, uint32_t rights, nx_buffer_view *view) {
    if (!nx_buffer_external_pointer_valid(manager, view, sizeof(*view),
                                          _Alignof(nx_buffer_view)) ||
        view->active != 0u || !nx_object_ref_is_valid(reference) ||
        length == 0u || offset > UINT64_MAX - length)
        return NX_STATUS_INVALID_ARGUMENT;
    if ((rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) == 0u ||
        (rights & ~(uint32_t)(NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
                              NX_BUFFER_RIGHT_MAP)) != 0u ||
        ((rights & NX_BUFFER_RIGHT_READ) != 0u &&
         (rights & NX_BUFFER_RIGHT_WRITE) != 0u))
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_acquire_resolve_locked(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_object_ref reference, uint64_t offset, uint64_t length,
    uint32_t rights, uint32_t *slot_index, nx_buffer_slot **slot_out) {
    nx_state_node node;
    nx_buffer_slot *slot;
    nx_status status = nx_buffer_find_locked(
        manager, reference, slot_index, &node);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_authorize(authority, reference, rights);
    if (status != NX_STATUS_OK)
        return status;
    slot = &manager->slots[*slot_index];
    if (slot->state == NX_BUFFER_STATE_QUARANTINED ||
        slot->state == NX_BUFFER_STATE_RETIRED)
        return NX_STATUS_INTEGRITY_FAILURE;
    if (offset + length > slot->capacity ||
        ((rights & NX_BUFFER_RIGHT_READ) != 0u &&
         offset + length > slot->logical_length))
        return NX_STATUS_INVALID_ARGUMENT;
    if ((rights & NX_BUFFER_RIGHT_WRITE) != 0u) {
        if ((slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u &&
            slot->state == NX_BUFFER_STATE_SEALED)
            return NX_STATUS_ACCESS_DENIED;
        if (slot->lease_count != 0u)
            return NX_STATUS_BUSY;
    } else if (slot->state != NX_BUFFER_STATE_SEALED ||
               slot->lease_count == UINT64_MAX) {
        return slot->state == NX_BUFFER_STATE_SEALED
                   ? NX_STATUS_RESOURCE_EXHAUSTED : NX_STATUS_BUSY;
    }
    *slot_out = slot;
    return NX_STATUS_OK;
}

static void nx_buffer_acquire_publish_lease(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_object_ref reference, uint64_t offset, uint64_t length,
    uint32_t rights, uint32_t slot_index, uint32_t lease_index,
    nx_buffer_slot *slot, nx_buffer_view *view) {
    nx_buffer_lease_slot *lease = &manager->leases[lease_index];
    lease->buffer = reference;
    lease->principal = authority->principal;
    lease->lease_id = manager->next_lease_id++;
    lease->offset = offset;
    lease->length = length;
    lease->buffer_slot = slot_index;
    lease->rights = rights;
    lease->active = 1u;
    slot->lease_count++;
    slot->pinned_bytes += length;
    if ((rights & NX_BUFFER_RIGHT_WRITE) != 0u)
        slot->state = NX_BUFFER_STATE_ACTIVE;
    memset(view, 0, sizeof(*view));
    view->buffer = reference;
    view->lease_id = lease->lease_id;
    view->offset = offset;
    view->length = length;
    view->slot_index = slot_index;
    view->rights = rights;
    view->active = 1u;
    if ((rights & NX_BUFFER_RIGHT_WRITE) != 0u) {
        view->write_bytes.data = manager->arena +
                                 (size_t)(slot->arena_offset + offset);
        view->write_bytes.size = (size_t)length;
    } else {
        view->read_bytes.data = manager->arena +
                                (size_t)(slot->arena_offset + offset);
        view->read_bytes.size = (size_t)length;
    }
}

static nx_status nx_buffer_acquire_reserve_lease_locked(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_object_ref reference, uint64_t offset, uint64_t length,
    uint32_t rights, uint32_t slot_index, nx_buffer_slot *slot,
    nx_operation_scope *scope, nx_buffer_view *view) {
    uint32_t lease_index = NX_BUFFER_NO_SLOT;
    uint32_t index;
    for (index = 0u; index < manager->lease_capacity &&
         lease_index == NX_BUFFER_NO_SLOT; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (manager->leases[index].active == 0u) {
            lease_index = index;
        }
    }
    if (lease_index == NX_BUFFER_NO_SLOT || manager->next_lease_id == 0u ||
        manager->next_lease_id == UINT64_MAX ||
        length > UINT64_MAX - slot->pinned_bytes)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    nx_buffer_acquire_publish_lease(
        manager, authority, reference, offset, length, rights, slot_index,
        lease_index, slot, view);
    return NX_STATUS_OK;
}

nx_status nx_buffer_acquire(nx_buffer_manager *manager,
                            const nx_buffer_authority *authority,
                            nx_object_ref reference, uint64_t offset,
                            uint64_t length, uint32_t rights,
                            nx_buffer_view *view, nx_result *result) {
    nx_operation_scope scope;
    nx_buffer_slot *slot = NULL;
    uint32_t slot_index = NX_BUFFER_NO_SLOT;
    uint64_t operation_fuel;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_ACQUIRE);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = (uint64_t)manager->lease_capacity + UINT64_C(32);
    status = nx_buffer_scope_begin(
        &scope, &manager->metrics[NX_BUFFER_METRIC_ACQUIRE],
        &manager->cycle_counter, NX_OP_BUFFER_ACQUIRE, operation_fuel);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_ACQUIRE, 0u);
    status = nx_buffer_acquire_validate_request(
        manager, reference, offset, length, rights, view);
    if (status != NX_STATUS_OK)
        return nx_buffer_acquire_finish(&scope, status, length, 0u, result);
    status = nx_buffer_authorize(authority, reference, rights);
    if (status != NX_STATUS_OK)
        return nx_buffer_acquire_finish(&scope, status, length, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_acquire_finish(
            &scope, NX_STATUS_BUSY, length, 0u, result);
    status = nx_buffer_acquire_resolve_locked(
        manager, authority, reference, offset, length, rights,
        &slot_index, &slot);
    if (status == NX_STATUS_OK)
        status = nx_buffer_acquire_reserve_lease_locked(
            manager, authority, reference, offset, length, rights,
            slot_index, slot, &scope, view);
    if (status != NX_STATUS_OK)
        return nx_buffer_acquire_unlock_finish(
            manager, &scope, status, length, 0u, result);
    nx_buffer_manager_unlock(manager);
    return nx_buffer_acquire_finish(&scope, NX_STATUS_OK, length,
                                    view->lease_id, result);
}

static nx_status nx_buffer_commit_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t new_length, uint64_t checksum, nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? new_length : 0u,
        status == NX_STATUS_OK ? checksum : 0u,
        status == NX_STATUS_OK ? new_length : 0u, result);
}

static nx_status nx_buffer_commit_quarantine_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_view *view, nx_buffer_slot *slot, uint32_t lease_index,
    nx_status status, uint64_t new_length, uint64_t checksum,
    nx_result *result) {
    nx_state_node node;
    nx_result registry_result;
    if (nx_state_store_read(manager->registry, view->buffer, &node,
                            &registry_result) == NX_STATUS_OK)
        (void)nx_buffer_quarantine_locked(manager, view->slot_index, &node);
    else
        slot->state = NX_BUFFER_STATE_QUARANTINED;
    nx_buffer_release_lease_locked(manager, lease_index);
    memset(view, 0, sizeof(*view));
    return nx_buffer_commit_unlock_finish(manager, scope, status, new_length,
                                          checksum, result);
}

static nx_status nx_buffer_commit_resolve_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    const nx_buffer_authority *authority, nx_buffer_view *view,
    uint64_t new_length, uint32_t *lease_index, nx_buffer_slot **slot_out,
    nx_state_node *node, nx_result *result) {
    uint32_t resolved_slot = NX_BUFFER_NO_SLOT;
    nx_buffer_slot *slot;
    if (slot_out == NULL)
        return nx_buffer_commit_unlock_finish(
            manager, scope, NX_STATUS_INVALID_ARGUMENT, new_length, 0u, result);
    *slot_out = NULL;
    nx_status status = nx_buffer_authorize(
        authority, view->buffer, NX_BUFFER_RIGHT_WRITE);
    if (status != NX_STATUS_OK)
        return nx_buffer_commit_unlock_finish(
            manager, scope, status, new_length, 0u, result);
    status = nx_buffer_find_lease_locked(
        manager, view, authority->principal, lease_index, scope);
    if (status != NX_STATUS_OK)
        return nx_buffer_commit_unlock_finish(
            manager, scope, status, new_length, 0u, result);
    slot = &manager->slots[view->slot_index];
    if (slot->state != NX_BUFFER_STATE_ACTIVE ||
        !nx_buffer_slot_geometry_valid(manager, slot) ||
        new_length > slot->capacity ||
        !nx_buffer_view_span_valid(manager, slot, view, 1))
        return nx_buffer_commit_unlock_finish(
            manager, scope, NX_STATUS_INVALID_ARGUMENT, new_length, 0u, result);
    status = nx_buffer_find_locked(manager, view->buffer, &resolved_slot, node);
    if (status != NX_STATUS_OK || resolved_slot != view->slot_index) {
        if (status == NX_STATUS_OK)
            status = NX_STATUS_CORRUPT;
        return nx_buffer_commit_quarantine_finish(
            manager, scope, view, slot, *lease_index, status, new_length,
            0u, result);
    }
    *slot_out = slot;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_commit_scrub_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_view *view, nx_buffer_slot *slot, uint64_t new_length) {
    uint64_t old_length = slot->logical_length;
    uint64_t write_end = view->offset + view->length;
    uint64_t scrub_bytes = 0u;
    uint64_t fuel;
    nx_status status;
    if (new_length < old_length)
        scrub_bytes = slot->allocation_bytes - new_length;
    else {
        if (view->offset > old_length)
            scrub_bytes += view->offset - old_length;
        if (write_end < new_length)
            scrub_bytes += new_length - write_end;
    }
    if (new_length > UINT64_MAX - scrub_bytes)
        return NX_STATUS_OVERFLOW;
    fuel = new_length + scrub_bytes;
    status = nx_operation_scope_consume(scope, fuel);
    if (status != NX_STATUS_OK)
        return status;
    if (new_length < old_length)
        memset(manager->arena + (size_t)(slot->arena_offset + new_length), 0,
               (size_t)(slot->allocation_bytes - new_length));
    else {
        if (view->offset > old_length)
            memset(manager->arena + (size_t)(slot->arena_offset + old_length), 0,
                   (size_t)(view->offset - old_length));
        if (write_end < new_length)
            memset(manager->arena + (size_t)(slot->arena_offset + write_end), 0,
                   (size_t)(new_length - write_end));
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_commit_digest_publish_locked(
    nx_buffer_manager *manager, nx_buffer_view *view, nx_buffer_slot *slot,
    const nx_state_node *node, uint64_t new_length, uint64_t *checksum) {
    nx_state_node updated;
    nx_state_node_spec spec;
    nx_result registry_result;
    nx_digest256 digest;
    nx_const_span data = {
        manager->arena + (size_t)slot->arena_offset, (size_t)new_length};
    nx_status status = nx_buffer_crc64_ecma(data, checksum);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_sha256_compute(data, &digest);
    if (status != NX_STATUS_OK)
        return status;
    nx_buffer_make_node_spec(slot, view->slot_index, new_length, &digest, &spec);
    status = nx_state_store_update(manager->registry, view->buffer,
                                   node->revision, &spec, &updated,
                                   &registry_result);
    if (status != NX_STATUS_OK)
        return status;
    slot->logical_length = new_length;
    slot->checksum = *checksum;
    slot->digest = digest;
    slot->registry_revision = updated.revision;
    slot->state = NX_BUFFER_STATE_SEALED;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_commit_apply_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_view *view, nx_buffer_slot *slot, uint32_t lease_index,
    const nx_state_node *node, uint64_t new_length, nx_result *result) {
    uint64_t checksum = 0u;
    nx_status status = nx_buffer_commit_scrub_locked(
        manager, scope, view, slot, new_length);
    if (status == NX_STATUS_OK)
        status = nx_buffer_commit_digest_publish_locked(
            manager, view, slot, node, new_length, &checksum);
    if (status != NX_STATUS_OK)
        return nx_buffer_commit_quarantine_finish(
            manager, scope, view, slot, lease_index, status, new_length,
            checksum, result);
    nx_buffer_release_lease_locked(manager, lease_index);
    memset(view, 0, sizeof(*view));
    return nx_buffer_commit_unlock_finish(
        manager, scope, NX_STATUS_OK, new_length, checksum, result);
}

nx_status nx_buffer_view_commit(nx_buffer_manager *manager,
                                const nx_buffer_authority *authority,
                                nx_buffer_view *view, uint64_t new_length,
                                nx_result *result) {
    nx_operation_scope scope;
    nx_buffer_slot *slot = NULL;
    nx_state_node node;
    uint32_t lease_index = NX_BUFFER_NO_SLOT;
    uint64_t operation_fuel = 0u;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_VIEW_COMMIT);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_fuel_bound(manager, manager->arena_size, 2u, 2u,
                                  2u, 2u, UINT64_C(64), &operation_fuel);
    if (status == NX_STATUS_OK)
        status = nx_buffer_scope_begin(
            &scope, &manager->metrics[NX_BUFFER_METRIC_COMMIT],
            &manager->cycle_counter, NX_OP_BUFFER_VIEW_COMMIT, operation_fuel);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_VIEW_COMMIT, 0u);
    if (!nx_buffer_external_pointer_valid(manager, view, sizeof(*view),
                                          _Alignof(nx_buffer_view)) ||
        view->active == 0u || (view->rights & NX_BUFFER_RIGHT_WRITE) == 0u ||
        view->slot_index >= manager->slot_capacity ||
        view->offset > UINT64_MAX - view->length ||
        new_length < view->offset + view->length)
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_BUSY, 0u, 0u, 0u, result);
    status = nx_buffer_commit_resolve_locked(
        manager, &scope, authority, view, new_length, &lease_index, &slot,
        &node, result);
    if (status != NX_STATUS_OK)
        return status;
    if (slot == NULL) {
        nx_buffer_release_lease_locked(manager, lease_index);
        memset(view, 0, sizeof(*view));
        return nx_buffer_commit_unlock_finish(
            manager, &scope, NX_STATUS_CORRUPT, new_length, 0u, result);
    }
    return nx_buffer_commit_apply_locked(
        manager, &scope, view, slot, lease_index, &node, new_length, result);
}

static nx_status nx_buffer_view_resolve_lease_locked(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_buffer_view *view, uint32_t required_rights, uint32_t write_access,
    nx_operation_scope *scope, uint32_t *lease_index) {
    nx_status status = nx_buffer_authorize(
        authority, view->buffer, required_rights);
    if (status == NX_STATUS_OK)
        status = nx_buffer_find_lease_locked(
            manager, view, authority->principal, lease_index, scope);
    if (status == NX_STATUS_OK &&
        !nx_buffer_view_span_valid(
            manager, &manager->slots[view->slot_index],
            view, write_access != 0u))
        status = NX_STATUS_INVALID_ARGUMENT;
    return status;
}

nx_status nx_buffer_view_release(nx_buffer_manager *manager,
                                 const nx_buffer_authority *authority,
                                 nx_buffer_view *view, nx_result *result) {
    nx_operation_scope scope;
    uint32_t lease_index = NX_BUFFER_NO_SLOT;
    uint64_t released_bytes;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_VIEW_RELEASE);
    if (status != NX_STATUS_OK)
        return status;
    {
        uint64_t fuel_limit;
        status = nx_buffer_fuel_bound(manager, 0u, 1u, 0u, 1u, 1u,
                                      UINT64_C(8), &fuel_limit);
        if (status != NX_STATUS_OK)
            return nx_buffer_result_init(result, NX_STATUS_OVERFLOW,
                                         NX_OP_BUFFER_VIEW_RELEASE, 0u);
        status = nx_buffer_scope_begin(
            &scope, &manager->metrics[NX_BUFFER_METRIC_RELEASE],
            &manager->cycle_counter, NX_OP_BUFFER_VIEW_RELEASE, fuel_limit);
    }
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_VIEW_RELEASE, 0u);
    if (!nx_buffer_external_pointer_valid(manager, view, sizeof(*view),
                                          _Alignof(nx_buffer_view)) ||
        view->active == 0u || (view->rights & NX_BUFFER_RIGHT_READ) == 0u)
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, 0u, result);
    released_bytes = view->length;
    status = nx_buffer_authorize(authority, view->buffer, NX_BUFFER_RIGHT_READ);
    if (status != NX_STATUS_OK)
        return nx_buffer_scope_finish_bytes(
            &scope, status, 0u, 0u, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_BUSY, 0u, 0u, 0u, result);
    status = nx_buffer_view_resolve_lease_locked(
        manager, authority, view, NX_BUFFER_RIGHT_READ, 0u, &scope,
        &lease_index);
    if (status == NX_STATUS_OK) {
        nx_buffer_release_lease_locked(manager, lease_index);
        memset(view, 0, sizeof(*view));
    }
    nx_buffer_manager_unlock(manager);
    return nx_buffer_scope_finish_bytes(
        &scope, status, status == NX_STATUS_OK ? 1u : 0u, 0u,
        status == NX_STATUS_OK ? released_bytes : 0u, result);
}

static nx_status nx_buffer_view_abort_locked(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_buffer_view *view, nx_operation_scope *scope) {
    uint32_t lease_index = NX_BUFFER_NO_SLOT;
    nx_state_node node;
    nx_result registry_result;
    nx_status status = nx_buffer_view_resolve_lease_locked(
        manager, authority, view, NX_BUFFER_RIGHT_WRITE, 1u, scope,
        &lease_index);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_state_store_read(manager->registry, view->buffer, &node,
                            &registry_result) == NX_STATUS_OK)
        (void)nx_buffer_quarantine_locked(manager, view->slot_index, &node);
    else
        manager->slots[view->slot_index].state = NX_BUFFER_STATE_QUARANTINED;
    nx_buffer_release_lease_locked(manager, lease_index);
    memset(view, 0, sizeof(*view));
    return NX_STATUS_CANCELLED;
}

nx_status nx_buffer_view_abort(nx_buffer_manager *manager,
                               const nx_buffer_authority *authority,
                               nx_buffer_view *view, nx_result *result) {
    nx_operation_scope scope;
    uint64_t aborted_bytes;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_VIEW_RELEASE);
    if (status != NX_STATUS_OK)
        return status;
    {
        uint64_t fuel_limit;
        status = nx_buffer_fuel_bound(manager, 0u, 0u, 0u, 1u, 1u,
                                      UINT64_C(16), &fuel_limit);
        if (status == NX_STATUS_OK)
            status = nx_buffer_scope_begin(
                &scope, &manager->metrics[NX_BUFFER_METRIC_RELEASE],
                &manager->cycle_counter, NX_OP_BUFFER_VIEW_RELEASE, fuel_limit);
    }
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_VIEW_RELEASE, 0u);
    if (!nx_buffer_external_pointer_valid(manager, view, sizeof(*view),
                                          _Alignof(nx_buffer_view)) ||
        view->active == 0u || (view->rights & NX_BUFFER_RIGHT_WRITE) == 0u ||
        view->slot_index >= manager->slot_capacity)
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, 0u, result);
    aborted_bytes = view->length;
    status = nx_buffer_authorize(authority, view->buffer, NX_BUFFER_RIGHT_WRITE);
    if (status != NX_STATUS_OK)
        return nx_buffer_scope_finish_bytes(
            &scope, status, 0u, 0u, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_BUSY, 0u, 0u, 0u, result);
    status = nx_buffer_view_abort_locked(manager, authority, view, &scope);
    nx_buffer_manager_unlock(manager);
    return nx_buffer_scope_finish_bytes(
        &scope, status, 0u, 0u,
        status == NX_STATUS_CANCELLED ? aborted_bytes : 0u, result);
}

static nx_status nx_buffer_verify_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t checksum, uint64_t verified_bytes, nx_result *result) {
    nx_buffer_manager_unlock(manager);
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? 1u : 0u,
        status == NX_STATUS_OK ? checksum : 0u,
        status == NX_STATUS_OK ? verified_bytes : 0u, result);
}

static nx_status nx_buffer_verify_quarantine_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    uint32_t slot_index, nx_state_node *node, nx_status status,
    uint64_t checksum, uint64_t verified_bytes, nx_result *result) {
    (void)nx_buffer_quarantine_locked(manager, slot_index, node);
    return nx_buffer_verify_unlock_finish(manager, scope, status, checksum,
                                          verified_bytes, result);
}

static nx_status nx_buffer_verify_resolve_locked(
    nx_buffer_manager *manager, const nx_buffer_authority *authority,
    nx_object_ref reference, uint32_t *slot_index, nx_buffer_slot **slot_out,
    nx_state_node *node, uint64_t *verified_bytes) {
    nx_status status = nx_buffer_find_locked(
        manager, reference, slot_index, node);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_authorize(authority, reference, NX_BUFFER_RIGHT_READ);
    if (status != NX_STATUS_OK)
        return status;
    *slot_out = &manager->slots[*slot_index];
    *verified_bytes = (*slot_out)->logical_length;
    if ((*slot_out)->state == NX_BUFFER_STATE_QUARANTINED)
        return NX_STATUS_INTEGRITY_FAILURE;
    if ((*slot_out)->state != NX_BUFFER_STATE_SEALED ||
        (*slot_out)->lease_count != 0u)
        return NX_STATUS_BUSY;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_verify_digests(
    nx_buffer_manager *manager, const nx_buffer_slot *slot,
    const nx_state_node *node, uint64_t *checksum) {
    nx_digest256 digest;
    nx_const_span data = {
        manager->arena + (size_t)slot->arena_offset,
        (size_t)slot->logical_length};
    nx_status status = nx_buffer_crc64_ecma(data, checksum);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_sha256_compute(data, &digest);
    if (status != NX_STATUS_OK)
        return status;
    if (*checksum != slot->checksum ||
        memcmp(digest.bytes, slot->digest.bytes, sizeof(digest.bytes)) != 0 ||
        memcmp(digest.bytes, node->spec.payload_digest.bytes,
               sizeof(digest.bytes)) != 0)
        return NX_STATUS_INTEGRITY_FAILURE;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_verify_scope_begin(
    nx_buffer_manager *manager, nx_operation_scope *scope) {
    uint64_t fuel_limit;
    nx_status status = nx_buffer_fuel_bound(
        manager, manager->arena_size, 1u, 2u, 1u, 2u, UINT64_C(64),
        &fuel_limit);
    if (status != NX_STATUS_OK)
        return status;
    return nx_buffer_scope_begin(
        scope, &manager->metrics[NX_BUFFER_METRIC_VERIFY],
        &manager->cycle_counter, NX_OP_BUFFER_VERIFY, fuel_limit);
}

nx_status nx_buffer_verify(nx_buffer_manager *manager,
                           const nx_buffer_authority *authority,
                           nx_object_ref reference, nx_result *result) {
    nx_operation_scope scope;
    nx_state_node node;
    nx_buffer_slot *slot = NULL;
    uint32_t slot_index = NX_BUFFER_NO_SLOT;
    uint64_t checksum = 0u;
    uint64_t verified_bytes = 0u;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_VERIFY);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_verify_scope_begin(manager, &scope);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_VERIFY, 0u);
    status = nx_buffer_authorize(authority, reference, NX_BUFFER_RIGHT_READ);
    if (status != NX_STATUS_OK)
        return nx_buffer_scope_finish_bytes(&scope, status, 0u, 0u, 0u,
                                            result);
    if (!nx_object_ref_is_valid(reference))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_BUSY, 0u, 0u, 0u, result);
    status = nx_buffer_verify_resolve_locked(
        manager, authority, reference, &slot_index, &slot, &node,
        &verified_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_verify_unlock_finish(
            manager, &scope, status, checksum, verified_bytes, result);
    status = nx_operation_scope_consume(&scope, verified_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_verify_unlock_finish(
            manager, &scope, status, checksum, verified_bytes, result);
    status = nx_buffer_verify_digests(manager, slot, &node, &checksum);
    if (status != NX_STATUS_OK)
        return nx_buffer_verify_quarantine_finish(
            manager, &scope, slot_index, &node, status, checksum,
            verified_bytes, result);
    return nx_buffer_verify_unlock_finish(
        manager, &scope, NX_STATUS_OK, checksum, verified_bytes, result);
}

static nx_status nx_buffer_destroy_prepare_release(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_slot *slot, uint64_t *destroyed_bytes) {
    uint64_t cleanup_fuel;
    if (!nx_buffer_slot_geometry_valid(manager, slot))
        return NX_STATUS_CORRUPT;
    *destroyed_bytes = slot->allocation_bytes;
    if (*destroyed_bytes > UINT64_MAX -
                               (uint64_t)slot->allocation_granules - 1u)
        return NX_STATUS_OVERFLOW;
    cleanup_fuel = *destroyed_bytes +
                   (uint64_t)slot->allocation_granules + 1u;
    return nx_operation_scope_consume(scope, cleanup_fuel);
}

static void nx_buffer_destroy_release_allocation(
    nx_buffer_manager *manager, nx_buffer_slot *slot) {
    memset(manager->arena + (size_t)slot->arena_offset, 0,
           (size_t)slot->allocation_bytes);
    (void)nx_buffer_release_extent(manager, slot);
    memset(slot, 0, sizeof(*slot));
}

static nx_status nx_buffer_destroy_find_quarantined(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_object_ref reference, nx_status lookup_status, uint32_t *slot_index) {
    uint32_t candidate;
    int has_lease = 0;
    nx_status status;
    if (lookup_status != NX_STATUS_CORRUPT &&
        lookup_status != NX_STATUS_NOT_FOUND)
        return lookup_status;
    status = nx_buffer_has_active_lease_locked(
        manager, reference, scope, &has_lease);
    if (status != NX_STATUS_OK)
        return status;
    if (has_lease != 0)
        return NX_STATUS_BUSY;
    for (candidate = 0u; candidate < manager->slot_capacity; ++candidate) {
        nx_buffer_slot *slot = &manager->slots[candidate];
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (slot->state != NX_BUFFER_STATE_QUARANTINED ||
            !nx_object_ref_equal(slot->reference, reference))
            continue;
        if (slot->lease_count != 0u || slot->pinned_bytes != 0u)
            return NX_STATUS_BUSY;
        *slot_index = candidate;
        return NX_STATUS_OK;
    }
    return lookup_status;
}

static nx_status nx_buffer_destroy_quarantined_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_object_ref reference, nx_status lookup_status,
    uint64_t *destroyed_bytes) {
    nx_state_node node;
    nx_result registry_result;
    uint32_t slot_index = NX_BUFFER_NO_SLOT;
    nx_buffer_slot *slot;
    nx_status status = nx_buffer_destroy_find_quarantined(
        manager, scope, reference, lookup_status, &slot_index);
    if (status != NX_STATUS_OK)
        return status;
    slot = &manager->slots[slot_index];
    status = nx_buffer_destroy_prepare_release(
        manager, scope, slot, destroyed_bytes);
    if (status != NX_STATUS_OK)
        return status;
    if (lookup_status == NX_STATUS_CORRUPT) {
        status = nx_state_store_read(manager->registry, reference, &node,
                                     &registry_result);
        if (status != NX_STATUS_OK ||
            node.spec.type_id != NX_BUFFER_TYPE_ID ||
            node.spec.schema_id != NX_BUFFER_SCHEMA_ID ||
            !nx_object_ref_equal(node.spec.owner, slot->owner) ||
            node.spec.payload_offset != slot_index)
            return NX_STATUS_CORRUPT;
        status = nx_state_store_delete(manager->registry, reference,
                                       node.revision, &registry_result);
        if (status != NX_STATUS_OK)
            return status;
    }
    nx_buffer_destroy_release_allocation(manager, slot);
    return NX_STATUS_OK;
}

static nx_status nx_buffer_destroy_live_locked(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    const nx_buffer_authority *authority, nx_object_ref reference,
    const nx_state_node *node, nx_buffer_slot *slot,
    uint64_t *destroyed_bytes) {
    nx_result registry_result;
    int has_lease = 0;
    nx_status status = nx_buffer_authorize(
        authority, reference, NX_BUFFER_RIGHT_DESTROY);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_has_active_lease_locked(
        manager, reference, scope, &has_lease);
    if (status != NX_STATUS_OK)
        return status;
    if (has_lease != 0 || slot->lease_count != 0u || slot->pinned_bytes != 0u)
        return NX_STATUS_BUSY;
    status = nx_buffer_destroy_prepare_release(
        manager, scope, slot, destroyed_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_delete(manager->registry, reference,
                                   node->revision, &registry_result);
    if (status != NX_STATUS_OK)
        return status;
    nx_buffer_destroy_release_allocation(manager, slot);
    return NX_STATUS_OK;
}

nx_status nx_buffer_destroy(nx_buffer_manager *manager,
                            const nx_buffer_authority *authority,
                            nx_object_ref reference, nx_result *result) {
    nx_operation_scope scope;
    nx_state_node node;
    uint32_t slot_index = NX_BUFFER_NO_SLOT;
    nx_buffer_slot *slot;
    uint64_t destroyed_bytes = 0u;
    uint64_t fuel_limit;
    uint64_t byte_work;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_DESTROY);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_u64_mul(manager->arena_size, UINT64_C(2), &byte_work);
    if (status == NX_STATUS_OK)
        status = nx_buffer_fuel_bound(manager, byte_work, 2u, 1u,
                                      1u, 2u, UINT64_C(32), &fuel_limit);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, NX_STATUS_OVERFLOW,
                                     NX_OP_BUFFER_DESTROY, manager->arena_size);
    status = nx_buffer_scope_begin(
        &scope, &manager->metrics[NX_BUFFER_METRIC_DESTROY],
        &manager->cycle_counter, NX_OP_BUFFER_DESTROY, fuel_limit);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_DESTROY, 0u);
    status = nx_buffer_authorize(authority, reference, NX_BUFFER_RIGHT_DESTROY);
    if (status != NX_STATUS_OK)
        return nx_buffer_destroy_finish(&scope, status, 0u, result);
    if (!nx_object_ref_is_valid(reference))
        return nx_buffer_destroy_finish(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, result);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_destroy_finish(&scope, NX_STATUS_BUSY, 0u, result);
    status = nx_buffer_find_locked(manager, reference, &slot_index, &node);
    if (status == NX_STATUS_OK) {
        slot = &manager->slots[slot_index];
        status = nx_buffer_destroy_live_locked(
            manager, &scope, authority, reference, &node, slot,
            &destroyed_bytes);
    } else {
        status = nx_buffer_destroy_quarantined_locked(
            manager, &scope, reference, status, &destroyed_bytes);
    }
    return nx_buffer_destroy_unlock_finish(
        manager, &scope, status, destroyed_bytes, result);
}

static nx_status nx_buffer_validate_unlock_finish(
    nx_buffer_manager *manager, nx_operation_scope *scope, nx_status status,
    uint64_t active_leases, uint64_t allocated_granules, nx_result *result) {
    uint32_t i;
    for (i = 0u; i < manager->slot_capacity; ++i) {
        nx_status cleanup_status = nx_operation_scope_consume(scope, 1u);
        if (cleanup_status != NX_STATUS_OK && status == NX_STATUS_OK)
            status = cleanup_status;
        manager->slots[i].validation_lease_count = 0u;
        manager->slots[i].validation_pinned_bytes = 0u;
        manager->slots[i].validation_writer_count = 0u;
    }
    nx_buffer_manager_unlock(manager);
    return nx_buffer_scope_finish_bytes(
        scope, status, status == NX_STATUS_OK ? active_leases : 0u,
        status == NX_STATUS_OK ? allocated_granules : 0u,
        status == NX_STATUS_OK ? allocated_granules * manager->granule_size : 0u,
        result);
}

static nx_status nx_buffer_validate_reset_marks(
    nx_buffer_manager *manager, nx_operation_scope *scope) {
    uint32_t index;
    for (index = 0u; index < manager->slot_capacity; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        manager->slots[index].validation_lease_count = 0u;
        manager->slots[index].validation_pinned_bytes = 0u;
        manager->slots[index].validation_writer_count = 0u;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_slot_state(
    nx_buffer_manager *manager, const nx_buffer_slot *slot,
    uint32_t *allocated) {
    if (slot->state == NX_BUFFER_STATE_FREE) {
        if (slot->lease_count != 0u || slot->pinned_bytes != 0u ||
            slot->allocation_granules != 0u || slot->allocation_bytes != 0u ||
            slot->capacity != 0u || slot->logical_length != 0u ||
            !nx_object_ref_is_zero(slot->reference) ||
            !nx_object_ref_is_zero(slot->owner))
            return NX_STATUS_CORRUPT;
        *allocated = 0u;
        return NX_STATUS_OK;
    }
    if (slot->state != NX_BUFFER_STATE_ACTIVE &&
        slot->state != NX_BUFFER_STATE_SEALED &&
        slot->state != NX_BUFFER_STATE_QUARANTINED)
        return NX_STATUS_CORRUPT;
    if (!nx_object_ref_is_valid(slot->reference) ||
        !nx_object_ref_is_valid(slot->owner) ||
        !nx_buffer_slot_geometry_valid(manager, slot))
        return NX_STATUS_CORRUPT;
    *allocated = 1u;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_slot_bitmap(
    nx_buffer_manager *manager, const nx_buffer_slot *slot,
    nx_operation_scope *scope) {
    uint64_t start = slot->arena_offset / manager->granule_size;
    uint64_t index;
    for (index = 0u; index < slot->allocation_granules; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (nx_bitmap_get(manager->allocation_bitmap, start + index) == 0)
            return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_slot_registry(
    nx_buffer_manager *manager, const nx_buffer_slot *slot,
    uint32_t slot_index) {
    nx_state_node node;
    uint32_t resolved_slot = NX_BUFFER_NO_SLOT;
    nx_status status = nx_buffer_find_locked(
        manager, slot->reference, &resolved_slot, &node);
    if (status != NX_STATUS_OK || resolved_slot != slot_index)
        return NX_STATUS_CORRUPT;
    if (slot->state == NX_BUFFER_STATE_QUARANTINED)
        return node.spec.lifecycle == NX_STATE_LIFECYCLE_QUARANTINED
                   ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    return node.spec.lifecycle ==
            ((slot->flags & NX_BUFFER_FLAG_IMMUTABLE) != 0u
                 ? NX_STATE_LIFECYCLE_FROZEN : NX_STATE_LIFECYCLE_LIVE)
               ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status nx_buffer_validate_slots(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    uint64_t *allocated_granules) {
    uint32_t index;
    for (index = 0u; index < manager->slot_capacity; ++index) {
        nx_buffer_slot *slot = &manager->slots[index];
        uint32_t allocated;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status == NX_STATUS_OK)
            status = nx_buffer_validate_slot_state(manager, slot, &allocated);
        if (status == NX_STATUS_OK && allocated != 0u)
            status = nx_buffer_validate_slot_bitmap(manager, slot, scope);
        if (status == NX_STATUS_OK && allocated != 0u)
            status = nx_buffer_validate_slot_registry(manager, slot, index);
        if (status != NX_STATUS_OK)
            return status;
        if (allocated != 0u)
            *allocated_granules += (uint64_t)slot->allocation_granules;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_lease_record(
    const nx_buffer_manager *manager, const nx_buffer_lease_slot *lease) {
    const nx_buffer_slot *slot;
    if (lease->buffer_slot >= manager->slot_capacity)
        return NX_STATUS_CORRUPT;
    slot = &manager->slots[lease->buffer_slot];
    if (lease->lease_id == 0u || !nx_object_ref_is_valid(lease->principal) ||
        !nx_object_ref_equal(lease->buffer, slot->reference) ||
        lease->offset > UINT64_MAX - lease->length ||
        lease->offset + lease->length > slot->capacity ||
        (((lease->rights & NX_BUFFER_RIGHT_READ) != 0u) &&
         lease->offset + lease->length > slot->logical_length) ||
        lease->reserved != 0u ||
        (lease->rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) == 0u ||
        (lease->rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) ==
            (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE) ||
        (lease->rights & ~(uint32_t)(NX_BUFFER_RIGHT_READ |
                                     NX_BUFFER_RIGHT_WRITE |
                                     NX_BUFFER_RIGHT_MAP)) != 0u)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_lease_account(
    nx_buffer_manager *manager, const nx_buffer_lease_slot *lease) {
    nx_buffer_slot *slot = &manager->slots[lease->buffer_slot];
    if (lease->length > UINT64_MAX - slot->validation_pinned_bytes ||
        slot->validation_lease_count == UINT64_MAX)
        return NX_STATUS_OVERFLOW;
    slot->validation_lease_count++;
    slot->validation_pinned_bytes += lease->length;
    if ((lease->rights & NX_BUFFER_RIGHT_WRITE) != 0u)
        slot->validation_writer_count++;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_leases(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    uint64_t *active_leases) {
    uint32_t index;
    for (index = 0u; index < manager->lease_capacity; ++index) {
        nx_buffer_lease_slot *lease = &manager->leases[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (lease->active == 0u)
            continue;
        (*active_leases)++;
        status = nx_buffer_validate_lease_record(manager, lease);
        if (status == NX_STATUS_OK)
            status = nx_buffer_validate_lease_account(manager, lease);
        if (status != NX_STATUS_OK)
            return status;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_validate_lease_totals(
    nx_buffer_manager *manager, nx_operation_scope *scope) {
    uint32_t index;
    for (index = 0u; index < manager->slot_capacity; ++index) {
        nx_buffer_slot *slot = &manager->slots[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (slot->validation_lease_count != slot->lease_count ||
            slot->validation_pinned_bytes != slot->pinned_bytes ||
            slot->validation_writer_count > 1u ||
            (slot->validation_writer_count != 0u &&
             slot->validation_lease_count != 1u))
            return NX_STATUS_CORRUPT;
        slot->validation_lease_count = 0u;
        slot->validation_pinned_bytes = 0u;
        slot->validation_writer_count = 0u;
    }
    return NX_STATUS_OK;
}

nx_status nx_buffer_manager_validate(nx_buffer_manager *manager,
                                     nx_result *result) {
    nx_operation_scope scope;
    uint64_t allocated_granules = 0u;
    uint64_t bitmap_granules;
    uint64_t active_leases = 0u;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_MANAGER_VALIDATE);
    if (status != NX_STATUS_OK)
        return status;
    {
        uint64_t fuel_limit;
        status = nx_buffer_fuel_bound(
            manager, 0u, 2u,
            (uint64_t)manager->registry->index_capacity + 4u, 1u, 0u,
            UINT64_C(32), &fuel_limit);
        if (status == NX_STATUS_OK)
            status = nx_buffer_scope_begin(
                &scope, &manager->metrics[NX_BUFFER_METRIC_VALIDATE],
                &manager->cycle_counter, NX_OP_BUFFER_MANAGER_VALIDATE, fuel_limit);
    }
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_MANAGER_VALIDATE, 0u);
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_scope_finish_bytes(
            &scope, NX_STATUS_BUSY, 0u, 0u, 0u, result);
    status = nx_buffer_validate_reset_marks(manager, &scope);
    if (status == NX_STATUS_OK)
        status = nx_buffer_validate_slots(manager, &scope,
                                          &allocated_granules);
    if (status == NX_STATUS_OK)
        status = nx_buffer_validate_leases(manager, &scope, &active_leases);
    if (status == NX_STATUS_OK)
        status = nx_buffer_validate_lease_totals(manager, &scope);
    if (status == NX_STATUS_OK)
        status = nx_bitmap_count(manager->allocation_bitmap,
                                 manager->granule_count, &scope,
                                 &bitmap_granules);
    if (status == NX_STATUS_OK && bitmap_granules != allocated_granules)
        status = NX_STATUS_CORRUPT;
    return nx_buffer_validate_unlock_finish(
        manager, &scope, status, active_leases, allocated_granules, result);
}

typedef struct {
    uint64_t allocated;
    uint64_t logical;
    uint64_t pinned;
    uint64_t active;
    uint64_t leases;
    uint64_t quarantined;
} nx_buffer_stats_accumulator;

static nx_status nx_buffer_stats_collect_slots(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_stats_accumulator *totals) {
    uint32_t index;
    for (index = 0u; index < manager->slot_capacity; ++index) {
        nx_buffer_slot *slot = &manager->slots[index];
        nx_status status;
        if (slot->state == NX_BUFFER_STATE_FREE)
            continue;
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (totals->allocated > UINT64_MAX - slot->allocation_bytes ||
            totals->logical > UINT64_MAX - slot->logical_length ||
            totals->pinned > UINT64_MAX - slot->pinned_bytes)
            return NX_STATUS_OVERFLOW;
        totals->active++;
        totals->allocated += slot->allocation_bytes;
        totals->logical += slot->logical_length;
        totals->pinned += slot->pinned_bytes;
        if (slot->state == NX_BUFFER_STATE_QUARANTINED)
            totals->quarantined++;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_stats_collect_leases(
    nx_buffer_manager *manager, nx_operation_scope *scope,
    nx_buffer_stats_accumulator *totals) {
    uint32_t index;
    for (index = 0u; index < manager->lease_capacity; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (manager->leases[index].active != 0u)
            totals->leases++;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_stats_publish(
    nx_buffer_manager *manager, nx_buffer_stats *stats,
    const nx_buffer_stats_accumulator *totals,
    nx_operation_scope *scope) {
    uint64_t bitmap_granules;
    nx_status status;
    stats->arena_bytes = manager->arena_size;
    stats->allocated_bytes = totals->allocated;
    stats->logical_bytes = totals->logical;
    stats->pinned_bytes = totals->pinned;
    stats->active_buffers = totals->active;
    stats->active_leases = totals->leases;
    status = nx_bitmap_count(manager->allocation_bitmap,
                             manager->granule_count, scope, &bitmap_granules);
    if (status != NX_STATUS_OK)
        return status;
    stats->free_granules = manager->granule_count - bitmap_granules;
    stats->quarantined_buffers = totals->quarantined;
    return NX_STATUS_OK;
}

nx_status nx_buffer_stats_read(nx_buffer_manager *manager,
                                nx_buffer_stats *stats, nx_result *result) {
    nx_operation_scope scope;
    nx_buffer_stats_accumulator totals = {0u};
    uint64_t fuel;
    nx_status status = nx_buffer_manager_ready(
        manager, result, NX_OP_BUFFER_STATS_READ);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_buffer_fuel_bound(manager, 0u, 1u, 1u, 1u, 0u,
                                  UINT64_C(8), &fuel);
    if (status == NX_STATUS_OK)
        status = nx_buffer_scope_begin(
            &scope, &manager->metrics[NX_BUFFER_METRIC_STATS],
            &manager->cycle_counter, NX_OP_BUFFER_STATS_READ, fuel);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_STATS_READ, 0u);
    if (!nx_buffer_external_pointer_valid(manager, stats, sizeof(*stats),
                                          _Alignof(nx_buffer_stats)))
        return nx_buffer_stats_finish(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, stats, result);
    memset(stats, 0, sizeof(*stats));
    if (!nx_buffer_manager_try_lock(manager))
        return nx_buffer_stats_finish(
            &scope, NX_STATUS_BUSY, 0u, 0u, stats, result);
    status = nx_buffer_stats_collect_slots(manager, &scope, &totals);
    if (status == NX_STATUS_OK)
        status = nx_buffer_stats_collect_leases(manager, &scope, &totals);
    if (status == NX_STATUS_OK)
        status = nx_buffer_stats_publish(manager, stats, &totals, &scope);
    return nx_buffer_stats_unlock_finish(
        manager, &scope, status, totals.active, totals.allocated, stats, result);
}

nx_status nx_buffer_metrics_read(const nx_buffer_manager *manager,
                                 nx_buffer_metric metric,
                                 nx_operation_metrics_snapshot *snapshot) {
    if (manager == NULL ||
        atomic_load_explicit(&manager->initialized, memory_order_acquire) == 0u ||
        snapshot == NULL || (uint32_t)metric >= NX_BUFFER_METRIC_COUNT)
        return NX_STATUS_INVALID_ARGUMENT;
    return nx_operation_metrics_read(&manager->metrics[(size_t)metric], snapshot);
}

nx_status nx_buffer_message_validate(const nx_buffer_message *message) {
    const uint32_t known_rights = (uint32_t)(NX_BUFFER_RIGHT_READ |
        NX_BUFFER_RIGHT_WRITE | NX_BUFFER_RIGHT_MAP | NX_BUFFER_RIGHT_SHARE |
        NX_BUFFER_RIGHT_DMA);
    if (message == NULL || message->request_id == 0u ||
        message->request_generation == 0u ||
        !nx_object_ref_is_valid(message->buffer) || message->length == 0u ||
        message->offset > UINT64_MAX - message->length ||
        message->lease_id == 0u || message->expected_revision == 0u ||
        message->rights == 0u ||
        (message->rights & ~known_rights) != 0u ||
        message->flags != NX_BUFFER_RING_KNOWN_FLAGS || message->reserved != 0u ||
        message->operation < NX_BUFFER_MESSAGE_READ ||
        message->operation > NX_BUFFER_MESSAGE_CANCEL)
        return NX_STATUS_MALFORMED;
    if ((message->operation == NX_BUFFER_MESSAGE_READ &&
         ((message->rights & NX_BUFFER_RIGHT_READ) == 0u ||
          (message->rights & NX_BUFFER_RIGHT_WRITE) != 0u)) ||
        ((message->operation == NX_BUFFER_MESSAGE_WRITE ||
          message->operation == NX_BUFFER_MESSAGE_COMMIT) &&
         ((message->rights & NX_BUFFER_RIGHT_WRITE) == 0u ||
          (message->rights & NX_BUFFER_RIGHT_READ) != 0u)) ||
        (message->operation == NX_BUFFER_MESSAGE_RELEASE &&
         ((message->rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) == 0u ||
          (message->rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) ==
              (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE))) ||
        (message->operation == NX_BUFFER_MESSAGE_CANCEL &&
         (message->rights & (NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE)) == 0u))
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_ring_validate_init_args(
    nx_buffer_ring *ring, nx_buffer_ring_cell *cells, uint64_t capacity,
    size_t *cells_bytes) {
    if (ring == NULL || cells == NULL ||
        ((uintptr_t)ring % (uintptr_t)_Alignof(nx_buffer_ring)) != 0u ||
        capacity < 2u || !nx_is_power_of_two_u64(capacity) ||
        capacity > (uint64_t)(SIZE_MAX / sizeof(*cells)) ||
        ((uintptr_t)cells % (uintptr_t)_Alignof(nx_buffer_ring_cell)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    *cells_bytes = (size_t)capacity * sizeof(*cells);
    if (nx_pointer_ranges_overlap(ring, sizeof(*ring), cells, *cells_bytes) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_buffer_ring_init_prepare(
    nx_buffer_ring *ring, nx_cycle_counter *counter,
    nx_operation_scope *scope, uint64_t capacity, nx_result *result) {
    nx_status status;
    atomic_init(&ring->initialized, 0u);
    atomic_init(&ring->enqueue_position, 0u);
    atomic_init(&ring->dequeue_position, 0u);
    nx_operation_metrics_init(&ring->init_metrics, NX_OP_BUFFER_RING_INIT);
    nx_operation_metrics_init(&ring->enqueue_metrics, NX_OP_BUFFER_RING_ENQUEUE);
    nx_operation_metrics_init(&ring->dequeue_metrics, NX_OP_BUFFER_RING_DEQUEUE);
    status = nx_cycle_counter_init(counter);
    if (status != NX_STATUS_OK && status != NX_STATUS_NOT_SUPPORTED)
        return nx_buffer_result_init(result, status, NX_OP_BUFFER_RING_INIT, 0u);
    ring->cycle_counter = *counter;
    return nx_buffer_scope_begin(scope, &ring->init_metrics,
                                 &ring->cycle_counter,
                                 NX_OP_BUFFER_RING_INIT, capacity + 8u);
}

static nx_status nx_buffer_ring_init_cells(
    nx_buffer_ring *ring, nx_buffer_ring_cell *cells, uint64_t capacity,
    size_t cells_bytes, nx_operation_scope *scope, nx_result *result) {
    uint64_t index;
    nx_status status;
    for (index = 0u; index < capacity; ++index) {
        atomic_init(&cells[index].sequence, index);
        memset(&cells[index].message, 0, sizeof(cells[index].message));
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return nx_buffer_scope_finish(scope, status, 0u, index, result);
    }
    ring->cells = cells;
    ring->capacity = capacity;
    ring->mask = capacity - 1u;
    if (atomic_is_lock_free(&ring->enqueue_position) == 0 ||
        atomic_is_lock_free(&ring->dequeue_position) == 0 ||
        atomic_is_lock_free(&cells[0].sequence) == 0)
        return nx_buffer_scope_finish(scope, NX_STATUS_NOT_SUPPORTED,
                                      0u, capacity, result);
    atomic_store_explicit(&ring->initialized, 1u, memory_order_release);
    return nx_buffer_scope_finish_bytes(scope, NX_STATUS_OK, capacity, 0u,
                                        (uint64_t)cells_bytes, result);
}

nx_status nx_buffer_ring_init(nx_buffer_ring *ring,
                               nx_buffer_ring_cell *cells,
                               uint64_t capacity, nx_result *result) {
    nx_cycle_counter counter;
    nx_operation_scope scope;
    size_t cells_bytes = 0u;
    nx_status status;
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_buffer_ring_validate_init_args(
        ring, cells, capacity, &cells_bytes);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status,
                                     NX_OP_BUFFER_RING_INIT, capacity);
    status = nx_buffer_ring_init_prepare(
        ring, &counter, &scope, capacity, result);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status,
                                     NX_OP_BUFFER_RING_INIT, 0u);
    return nx_buffer_ring_init_cells(
        ring, cells, capacity, cells_bytes, &scope, result);
}

static int nx_buffer_ring_message_pointer_valid(
    const nx_buffer_ring *ring, const nx_buffer_message *message) {
    return ((uintptr_t)message % (uintptr_t)_Alignof(nx_buffer_message)) == 0u &&
           nx_pointer_ranges_overlap(message, sizeof(*message),
                                     ring, sizeof(*ring)) == 0 &&
           nx_pointer_ranges_overlap(
               message, sizeof(*message), ring->cells,
               (size_t)ring->capacity * sizeof(*ring->cells)) == 0;
}

static nx_status nx_buffer_ring_enqueue_attempts(
    nx_buffer_ring *ring, const nx_buffer_message *message,
    nx_operation_scope *scope, uint64_t *attempts) {
    uint64_t position = atomic_load_explicit(
        &ring->enqueue_position, memory_order_relaxed);
    uint64_t iteration;
    nx_status status = NX_STATUS_BUSY;
    for (iteration = 0u; iteration <= NX_BUFFER_RING_RETRY_LIMIT;
         ++iteration) {
        nx_buffer_ring_cell *cell;
        uint64_t sequence;
        uint64_t expected;
        uint64_t next_position;
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if ((*attempts)++ >= NX_BUFFER_RING_RETRY_LIMIT) {
            status = NX_STATUS_BUSY;
            return status;
        }
        if (position >= UINT64_MAX - (ring->capacity * 2u)) {
            status = NX_STATUS_RESOURCE_EXHAUSTED;
            return status;
        }
        cell = &ring->cells[position & ring->mask];
        sequence = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        expected = position;
        if (sequence == expected) {
            next_position = position + 1u;
            if (atomic_compare_exchange_weak_explicit(
                    &ring->enqueue_position, &position, next_position,
                    memory_order_relaxed, memory_order_relaxed)) {
                cell->message = *message;
                atomic_store_explicit(&cell->sequence, next_position,
                                      memory_order_release);
                return NX_STATUS_OK;
            }
        } else if (sequence < expected) {
            return NX_STATUS_NO_SPACE;
        } else {
            position = atomic_load_explicit(&ring->enqueue_position,
                                            memory_order_relaxed);
        }
    }
    return status;
}

static nx_status nx_buffer_ring_dequeue_publish(
    nx_buffer_ring *ring, nx_buffer_ring_cell *cell,
    nx_buffer_message *message, uint64_t position) {
    nx_status status;
    *message = cell->message;
    atomic_store_explicit(&cell->sequence, position + ring->capacity,
                          memory_order_release);
    status = nx_buffer_message_validate(message);
    if (status != NX_STATUS_OK) {
        memset(message, 0, sizeof(*message));
        return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_buffer_ring_dequeue_attempts(
    nx_buffer_ring *ring, nx_buffer_message *message,
    nx_operation_scope *scope, uint64_t *attempts) {
    uint64_t position = atomic_load_explicit(
        &ring->dequeue_position, memory_order_relaxed);
    uint64_t iteration;
    nx_status status = NX_STATUS_BUSY;
    memset(message, 0, sizeof(*message));
    for (iteration = 0u; iteration <= NX_BUFFER_RING_RETRY_LIMIT;
         ++iteration) {
        nx_buffer_ring_cell *cell;
        uint64_t sequence;
        uint64_t expected;
        uint64_t next_position;
        status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if ((*attempts)++ >= NX_BUFFER_RING_RETRY_LIMIT) {
            status = NX_STATUS_BUSY;
            return status;
        }
        if (position >= UINT64_MAX - (ring->capacity * 2u)) {
            status = NX_STATUS_RESOURCE_EXHAUSTED;
            return status;
        }
        cell = &ring->cells[position & ring->mask];
        sequence = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        expected = position + 1u;
        if (sequence == expected) {
            next_position = position + 1u;
            if (atomic_compare_exchange_weak_explicit(
                    &ring->dequeue_position, &position, next_position,
                    memory_order_relaxed, memory_order_relaxed)) {
                return nx_buffer_ring_dequeue_publish(
                    ring, cell, message, position);
            }
        } else if (sequence < expected) {
            return NX_STATUS_NOT_FOUND;
        } else {
            position = atomic_load_explicit(&ring->dequeue_position,
                                            memory_order_relaxed);
        }
    }
    return status;
}

nx_status nx_buffer_ring_enqueue(nx_buffer_ring *ring,
                                 const nx_buffer_message *message,
                                 nx_result *result) {
    nx_operation_scope scope;
    uint64_t attempts = 0u;
    nx_status status;
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (ring == NULL ||
        atomic_load_explicit(&ring->initialized, memory_order_acquire) == 0u) {
        (void)nx_buffer_result_init(result, NX_STATUS_INVALID_ARGUMENT,
                                    NX_OP_BUFFER_RING_ENQUEUE, 0u);
        return NX_STATUS_INVALID_ARGUMENT;
    }
    status = nx_buffer_scope_begin(&scope, &ring->enqueue_metrics,
                                   &ring->cycle_counter,
                                   NX_OP_BUFFER_RING_ENQUEUE,
                                   NX_BUFFER_RING_RETRY_LIMIT);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status,
                                     NX_OP_BUFFER_RING_ENQUEUE, 0u);
    if (message == NULL || !nx_buffer_ring_message_pointer_valid(ring, message))
        status = NX_STATUS_INVALID_ARGUMENT;
    else
        status = nx_buffer_message_validate(message);
    if (status == NX_STATUS_OK)
        status = nx_buffer_ring_enqueue_attempts(
            ring, message, &scope, &attempts);
    return nx_buffer_scope_finish_bytes(
        &scope, status, status == NX_STATUS_OK ? 1u : 0u, attempts,
        status == NX_STATUS_OK ? sizeof(*message) : 0u, result);
}

nx_status nx_buffer_ring_dequeue(nx_buffer_ring *ring,
                                 nx_buffer_message *message,
                                 nx_result *result) {
    nx_operation_scope scope;
    uint64_t attempts = 0u;
    nx_status status;
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (ring == NULL || message == NULL ||
        atomic_load_explicit(&ring->initialized, memory_order_acquire) == 0u) {
        (void)nx_buffer_result_init(result, NX_STATUS_INVALID_ARGUMENT,
                                    NX_OP_BUFFER_RING_DEQUEUE, 0u);
        return NX_STATUS_INVALID_ARGUMENT;
    }
    status = nx_buffer_scope_begin(&scope, &ring->dequeue_metrics,
                                   &ring->cycle_counter,
                                   NX_OP_BUFFER_RING_DEQUEUE,
                                   NX_BUFFER_RING_RETRY_LIMIT);
    if (status != NX_STATUS_OK)
        return nx_buffer_result_init(result, status,
                                     NX_OP_BUFFER_RING_DEQUEUE, 0u);
    if (!nx_buffer_ring_message_pointer_valid(ring, message))
        status = NX_STATUS_INVALID_ARGUMENT;
    else
        status = nx_buffer_ring_dequeue_attempts(
            ring, message, &scope, &attempts);
    return nx_buffer_scope_finish_bytes(
        &scope, status, status == NX_STATUS_OK ? 1u : 0u, attempts,
        status == NX_STATUS_OK ? sizeof(*message) : 0u, result);
}
