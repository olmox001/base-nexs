/* NXOLMO registry algorithm port; one product-owned implementation. */
#include "nexs_state_store.h"
#include "nexs_checked.h"
#include "nexs_crypto.h"

#include <stddef.h>
#include <string.h>

#define NX_SLOT_FREE UINT32_C(0)
#define NX_SLOT_USED UINT32_C(1)
#define NX_SLOT_RETIRED UINT32_C(2)
#define NX_MARK_INDEX UINT32_C(1)
#define NX_MARK_REHASH_INDEX UINT32_C(2)
#define NX_MARK_FREE UINT32_C(4)
#define NX_MARK_TREE_VISITING UINT32_C(8)
#define NX_MARK_TREE_DONE UINT32_C(16)
#define NX_STATE_NODE_KNOWN_FLAGS (NX_STATE_NODE_FLAG_IMMUTABLE | \
                                   NX_STATE_NODE_FLAG_SPARSE | \
                                   NX_STATE_NODE_FLAG_CONTENT_ADDRESSED | \
                                   NX_STATE_NODE_FLAG_EXECUTABLE)



static uint64_t nx_rotl64(uint64_t value, unsigned int shift) {
    return (value << shift) | (value >> (64u - shift));
}

static uint64_t nx_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (i * 8u);
    return value;
}

static void nx_sip_round(uint64_t *v0, uint64_t *v1,
                         uint64_t *v2, uint64_t *v3) {
    *v0 += *v1;
    *v1 = nx_rotl64(*v1, 13u);
    *v1 ^= *v0;
    *v0 = nx_rotl64(*v0, 32u);
    *v2 += *v3;
    *v3 = nx_rotl64(*v3, 16u);
    *v3 ^= *v2;
    *v0 += *v3;
    *v3 = nx_rotl64(*v3, 21u);
    *v3 ^= *v0;
    *v2 += *v1;
    *v1 = nx_rotl64(*v1, 17u);
    *v1 ^= *v2;
    *v2 = nx_rotl64(*v2, 32u);
}

static uint64_t nx_siphash_object_id(nx_object_id id, const uint64_t key[2]) {
    uint8_t bytes[16];
    uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ key[0];
    uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ key[1];
    uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ key[0];
    uint64_t v3 = UINT64_C(0x7465646279746573) ^ key[1];
    uint64_t block;
    unsigned int i;

    for (i = 0u; i < 8u; ++i) {
        bytes[i] = (uint8_t)(id.high >> (i * 8u));
        bytes[i + 8u] = (uint8_t)(id.low >> (i * 8u));
    }

    block = nx_read_u64_le(bytes);
    v3 ^= block;
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    v0 ^= block;

    block = nx_read_u64_le(bytes + 8u);
    v3 ^= block;
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    v0 ^= block;

    block = UINT64_C(16) << 56;
    v3 ^= block;
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    v0 ^= block;
    v2 ^= UINT64_C(0xff);
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    nx_sip_round(&v0, &v1, &v2, &v3);
    return v0 ^ v1 ^ v2 ^ v3;
}


static nx_status nx_state_store_scope_begin(nx_state_store *store,
                                            nx_state_store_metric metric,
                                            uint64_t operation_id,
                                            uint64_t fuel_limit,
                                            nx_operation_scope *scope) {
    return nx_operation_scope_begin_with_counter(
        scope, &store->metrics[(size_t)metric], &store->cycle_counter,
        operation_id, fuel_limit, UINT64_MAX);
}

static nx_status nx_state_store_scope_finish(nx_operation_scope *scope,
                                             nx_status status,
                                             uint64_t completed,
                                             uint64_t detail,
                                             nx_result *result) {
    return nx_operation_scope_finish(scope, status, NX_RESULT_DOMAIN_REGISTRY,
                                     completed, detail, result);
}

static int nx_state_store_try_lock(nx_state_store *store) {
    uint32_t expected = 0u;
    return atomic_compare_exchange_strong_explicit(
        &store->lock, &expected, 1u, memory_order_acquire,
        memory_order_relaxed);
}

static void nx_state_store_unlock(nx_state_store *store) {
    atomic_store_explicit(&store->lock, 0u, memory_order_release);
}

/* Reject every new/old and new/new alias, not just corresponding arrays.
 * The caller owns each buffer. Full cross-product checking is mandatory
 * before the first byte of a newly admitted workspace can be mutated. */
static nx_status nx_state_store_grow_check_disjoint(
    const nx_state_store *store, const nx_state_slot *new_slots,
    const uint32_t *new_index, const uint32_t *new_rehash_index,
    size_t slots_bytes, size_t index_bytes) {
    const void *fresh[3] = {new_slots, new_index, new_rehash_index};
    const size_t fresh_bytes[3] = {slots_bytes, index_bytes, index_bytes};
    const void *prior[4] = {store, store->slots, store->index,
                            store->rehash_index};
    const size_t prior_bytes[4] = {
        sizeof(*store),
        (size_t)store->slot_capacity * sizeof(*store->slots),
        (size_t)store->index_capacity * sizeof(*store->index),
        (size_t)store->index_capacity * sizeof(*store->rehash_index)};
    for (size_t i = 0u; i < 3u; ++i) {
        for (size_t j = i + 1u; j < 3u; ++j) {
            int overlap = nx_pointer_ranges_overlap(
                fresh[i], fresh_bytes[i], fresh[j], fresh_bytes[j]);
            if (overlap < 0)
                return NX_STATUS_OVERFLOW;
            if (overlap != 0)
                return NX_STATUS_INVALID_ARGUMENT;
        }
        for (size_t j = 0u; j < 4u; ++j) {
            int overlap = nx_pointer_ranges_overlap(
                fresh[i], fresh_bytes[i], prior[j], prior_bytes[j]);
            if (overlap < 0)
                return NX_STATUS_OVERFLOW;
            if (overlap != 0)
                return NX_STATUS_INVALID_ARGUMENT;
        }
    }
    return NX_STATUS_OK;
}

/* Growth uses the existing NXOLMO slot/index authority, not a second
 * registry. The provider admits all replacement backing before this call.
 * Holding the store writer lock prevents readers from observing the swap. */
nx_status nx_state_store_grow(nx_state_store *store,
                              nx_state_slot *new_slots,
                              uint32_t new_slot_capacity,
                              uint32_t *new_index,
                              uint32_t *new_rehash_index,
                              uint32_t new_index_capacity) {
    uint32_t minimum = 0u;
    size_t slots_bytes = 0u;
    size_t index_bytes = 0u;
    nx_status status;
    if (store == NULL || new_slots == NULL ||
        new_index == NULL || new_rehash_index == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (atomic_load_explicit(&store->initialized, memory_order_acquire) != 1u ||
        new_slot_capacity <= store->slot_capacity)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_state_store_required_index_capacity(new_slot_capacity, &minimum);
    if (status != NX_STATUS_OK)
        return status;
    if (new_index_capacity < minimum ||
        (new_index_capacity & (new_index_capacity - 1u)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_size_mul(new_slot_capacity, sizeof(*new_slots), &slots_bytes) !=
            NX_STATUS_OK ||
        nx_size_mul(new_index_capacity, sizeof(*new_index), &index_bytes) !=
            NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    status = nx_state_store_grow_check_disjoint(
        store, new_slots, new_index, new_rehash_index,
        slots_bytes, index_bytes);
    if (status != NX_STATUS_OK)
        return status;
    if (!nx_state_store_try_lock(store))
        return NX_STATUS_BUSY;
    memset(new_slots, 0, slots_bytes);
    memset(new_index, 0, index_bytes);
    memset(new_rehash_index, 0, index_bytes);
    memcpy(new_slots, store->slots,
           (size_t)store->slot_capacity * sizeof(*store->slots));
    for (uint32_t i = store->slot_capacity; i < new_slot_capacity; ++i) {
        new_slots[i].state = NX_SLOT_FREE;
        new_slots[i].parent_slot = NX_STATE_STORE_NO_SLOT;
        new_slots[i].first_child_slot = NX_STATE_STORE_NO_SLOT;
        new_slots[i].previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
        new_slots[i].next_sibling_slot = NX_STATE_STORE_NO_SLOT;
        new_slots[i].next_free = i + 1u < new_slot_capacity
                                    ? i + 1u : store->free_head;
    }
    uint32_t indexed = 0u;
    status = NX_STATUS_OK;
    for (uint32_t i = 0u; i < store->slot_capacity &&
                         status == NX_STATUS_OK; ++i) {
        if (new_slots[i].state == NX_SLOT_USED) {
            uint32_t pos = (uint32_t)new_slots[i].hash &
                           (new_index_capacity - 1u);
            uint32_t probes = 0u;
            while (new_index[pos] != 0u && probes < new_index_capacity) {
                pos = (pos + 1u) & (new_index_capacity - 1u);
                ++probes;
            }
            if (probes == new_index_capacity) {
                status = NX_STATUS_CORRUPT;
            } else {
                new_index[pos] = i + 1u;
                ++indexed;
            }
        }
    }
    if (status == NX_STATUS_OK && indexed != store->count)
        status = NX_STATUS_CORRUPT;
    if (status == NX_STATUS_OK) {
        store->free_head = store->slot_capacity;
        store->slots = new_slots;
        store->slot_capacity = new_slot_capacity;
        store->index = new_index;
        store->rehash_index = new_rehash_index;
        store->index_capacity = new_index_capacity;
        store->rehash_cursor = 0u;
        store->index_tombstones = 0u;
        store->rehash_tombstones = 0u;
        store->rehash_state = NX_STATE_STORE_REHASH_IDLE;
    }
    nx_state_store_unlock(store);
    return status;
}

static nx_status nx_state_store_find_table_locked(
    nx_state_store *store, uint32_t *table, nx_object_id id,
    nx_operation_scope *scope, uint32_t *slot_index,
    uint32_t *index_position) {
    uint64_t hash = nx_siphash_object_id(id, store->hash_key);
    uint32_t mask = store->index_capacity - 1u;
    uint32_t position = (uint32_t)hash & mask;
    uint32_t probes;

    for (probes = 0u; probes < store->index_capacity; ++probes) {
        uint32_t entry;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        entry = table[position];
        if (entry == 0u)
            return NX_STATUS_NOT_FOUND;
        if (entry != NX_STATE_STORE_INDEX_TOMBSTONE) {
            uint32_t candidate = entry - 1u;
            nx_state_slot *slot;
            if (candidate >= store->slot_capacity)
                return NX_STATUS_CORRUPT;
            slot = &store->slots[candidate];
            if (slot->state != NX_SLOT_USED)
                return NX_STATUS_CORRUPT;
            if (nx_object_id_equal(slot->node.ref.id, id)) {
                if (slot->hash != hash)
                    return NX_STATUS_CORRUPT;
                *slot_index = candidate;
                *index_position = position;
                return NX_STATUS_OK;
            }
        }
        position = (position + 1u) & mask;
    }
    return NX_STATUS_NOT_FOUND;
}

static nx_status nx_state_store_find_locked(
    nx_state_store *store, nx_object_id id, nx_operation_scope *scope,
    uint32_t *slot_index, uint32_t *index_position, uint32_t **index_table) {
    nx_status status;
    if (store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING) {
        status = nx_state_store_find_table_locked(
            store, store->rehash_index, id, scope, slot_index, index_position);
        if (status == NX_STATUS_OK) {
            *index_table = store->rehash_index;
            return status;
        }
        if (status != NX_STATUS_NOT_FOUND)
            return status;
    }
    status = nx_state_store_find_table_locked(
        store, store->index, id, scope, slot_index, index_position);
    if (status == NX_STATUS_OK)
        *index_table = store->index;
    return status;
}

static nx_status nx_state_store_parent_resolve_locked(
    nx_state_store *store, nx_object_ref parent, nx_operation_scope *scope,
    uint32_t *parent_slot) {
    uint32_t position = NX_STATE_STORE_NO_SLOT;
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    uint32_t *table = NULL;
    nx_status status;
    *parent_slot = NX_STATE_STORE_NO_SLOT;
    if (nx_object_id_is_zero(parent.id))
        return parent.generation == 0u ? NX_STATUS_OK : NX_STATUS_MALFORMED;
    status = nx_state_store_find_locked(store, parent.id, scope, &slot_index,
                                        &position, &table);
    if (status != NX_STATUS_OK)
        return status;
    if (store->slots[slot_index].generation != parent.generation)
        return NX_STATUS_STALE_REFERENCE;
    if (store->slots[slot_index].node.spec.lifecycle ==
        NX_STATE_LIFECYCLE_QUARANTINED)
        return NX_STATUS_ACCESS_DENIED;
    *parent_slot = slot_index;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_parent_chain_validate_locked(
    nx_state_store *store, uint32_t node_slot, uint32_t parent_slot,
    nx_operation_scope *scope) {
    uint32_t visited;
    uint32_t current = parent_slot;
    for (visited = 0u; current != NX_STATE_STORE_NO_SLOT &&
         visited < store->slot_capacity; ++visited) {
        nx_state_slot *ancestor;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (current >= store->slot_capacity)
            return NX_STATUS_CORRUPT;
        if (current == node_slot)
            return NX_STATUS_CONFLICT;
        ancestor = &store->slots[current];
        if (ancestor->state != NX_SLOT_USED)
            return NX_STATUS_CORRUPT;
        if (ancestor->parent_slot == NX_STATE_STORE_NO_SLOT) {
            if (!nx_object_id_is_zero(ancestor->node.spec.parent.id) ||
                ancestor->node.spec.parent.generation != 0u)
                return NX_STATUS_CORRUPT;
        } else if (ancestor->parent_slot >= store->slot_capacity ||
                   store->slots[ancestor->parent_slot].state != NX_SLOT_USED ||
                   !nx_object_ref_equal(
                       store->slots[ancestor->parent_slot].node.ref,
                       ancestor->node.spec.parent)) {
            return NX_STATUS_CORRUPT;
        }
        current = ancestor->parent_slot;
    }
    return current == NX_STATE_STORE_NO_SLOT ? NX_STATUS_OK
                                             : NX_STATUS_CORRUPT;
}

static nx_status nx_state_store_child_parent_validate_locked(
    nx_state_store *store, uint32_t parent_slot) {
    nx_state_slot *parent;
    uint32_t first;
    if (parent_slot >= store->slot_capacity)
        return NX_STATUS_CORRUPT;
    parent = &store->slots[parent_slot];
    if (parent->state != NX_SLOT_USED)
        return NX_STATUS_CORRUPT;
    if (parent->child_count == UINT32_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    first = parent->first_child_slot;
    if (first == NX_STATE_STORE_NO_SLOT)
        return parent->child_count == 0u ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    if (first >= store->slot_capacity ||
        store->slots[first].state != NX_SLOT_USED ||
        store->slots[first].parent_slot != parent_slot ||
        store->slots[first].previous_sibling_slot != NX_STATE_STORE_NO_SLOT ||
        parent->child_count == 0u)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static void nx_state_store_child_attach_locked(
    nx_state_store *store, uint32_t child_slot, uint32_t parent_slot) {
    nx_state_slot *child = &store->slots[child_slot];
    nx_state_slot *parent = &store->slots[parent_slot];
    child->parent_slot = parent_slot;
    child->previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
    child->next_sibling_slot = parent->first_child_slot;
    if (parent->first_child_slot != NX_STATE_STORE_NO_SLOT)
        store->slots[parent->first_child_slot].previous_sibling_slot =
            child_slot;
    parent->first_child_slot = child_slot;
    parent->child_count++;
}

static nx_status nx_state_store_child_detach_locked(
    nx_state_store *store, uint32_t child_slot) {
    nx_state_slot *child = &store->slots[child_slot];
    nx_state_slot *parent;
    uint32_t previous = child->previous_sibling_slot;
    uint32_t next = child->next_sibling_slot;
    if (child->parent_slot == NX_STATE_STORE_NO_SLOT)
        return NX_STATUS_OK;
    if (child->parent_slot >= store->slot_capacity)
        return NX_STATUS_CORRUPT;
    parent = &store->slots[child->parent_slot];
    if (parent->state != NX_SLOT_USED || parent->child_count == 0u)
        return NX_STATUS_CORRUPT;
    if (previous == NX_STATE_STORE_NO_SLOT) {
        if (parent->first_child_slot != child_slot)
            return NX_STATUS_CORRUPT;
    } else if (previous >= store->slot_capacity ||
               store->slots[previous].state != NX_SLOT_USED ||
               store->slots[previous].next_sibling_slot != child_slot) {
        return NX_STATUS_CORRUPT;
    }
    if (next != NX_STATE_STORE_NO_SLOT &&
        (next >= store->slot_capacity ||
         store->slots[next].state != NX_SLOT_USED ||
         store->slots[next].previous_sibling_slot != child_slot))
        return NX_STATUS_CORRUPT;
    if (previous == NX_STATE_STORE_NO_SLOT)
        parent->first_child_slot = next;
    else
        store->slots[previous].next_sibling_slot = next;
    if (next != NX_STATE_STORE_NO_SLOT)
        store->slots[next].previous_sibling_slot = previous;
    parent->child_count--;
    child->parent_slot = NX_STATE_STORE_NO_SLOT;
    child->previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
    child->next_sibling_slot = NX_STATE_STORE_NO_SLOT;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_insert_table_locked(
    nx_state_store *store, uint32_t *table, uint32_t *tombstones,
    uint32_t slot_index, nx_operation_scope *scope) {
    nx_state_slot *slot = &store->slots[slot_index];
    uint32_t mask = store->index_capacity - 1u;
    uint32_t position = (uint32_t)slot->hash & mask;
    uint32_t first_tombstone = NX_STATE_STORE_NO_SLOT;
    uint32_t probes;

    for (probes = 0u; probes < store->index_capacity; ++probes) {
        uint32_t entry;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        entry = table[position];
        if (entry == 0u) {
            uint32_t destination = first_tombstone == NX_STATE_STORE_NO_SLOT
                                       ? position : first_tombstone;
            if (first_tombstone != NX_STATE_STORE_NO_SLOT && *tombstones == 0u)
                return NX_STATUS_CORRUPT;
            table[destination] = slot_index + 1u;
            if (first_tombstone != NX_STATE_STORE_NO_SLOT)
                (*tombstones)--;
            return NX_STATUS_OK;
        }
        if (entry == NX_STATE_STORE_INDEX_TOMBSTONE) {
            if (first_tombstone == NX_STATE_STORE_NO_SLOT)
                first_tombstone = position;
        } else {
            uint32_t candidate = entry - 1u;
            if (candidate >= store->slot_capacity ||
                store->slots[candidate].state != NX_SLOT_USED)
                return NX_STATUS_CORRUPT;
            if (nx_object_id_equal(store->slots[candidate].node.ref.id,
                                   slot->node.ref.id))
                return NX_STATUS_CONFLICT;
        }
        position = (position + 1u) & mask;
    }

    if (first_tombstone != NX_STATE_STORE_NO_SLOT) {
        if (*tombstones == 0u)
            return NX_STATUS_CORRUPT;
        table[first_tombstone] = slot_index + 1u;
        (*tombstones)--;
        return NX_STATUS_OK;
    }
    return NX_STATUS_RESOURCE_EXHAUSTED;
}

static void nx_state_store_maybe_start_rehash_locked(nx_state_store *store) {
    uint32_t threshold = store->index_capacity / 8u;
    if (threshold == 0u)
        threshold = 1u;
    if (store->rehash_state == NX_STATE_STORE_REHASH_IDLE &&
        store->index_tombstones >= threshold) {
        store->rehash_state = NX_STATE_STORE_REHASH_MIGRATING;
        store->rehash_cursor = 0u;
        store->rehash_tombstones = 0u;
    }
}

/* Each call examines at most REHASH_BUCKET_BUDGET buckets. Entry relocation
 * and table cleanup are incremental; no operation clears or rebuilds a full
 * index in one unbounded critical section. */
static nx_status nx_state_store_rehash_migrate_one(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t entry;
    nx_status status = nx_operation_scope_consume(scope, 1u);
    if (status != NX_STATUS_OK)
        return status;
    if (store->rehash_cursor >= store->index_capacity)
        return NX_STATUS_CORRUPT;
    entry = store->index[store->rehash_cursor];
    if (entry != 0u && entry != NX_STATE_STORE_INDEX_TOMBSTONE) {
        uint32_t slot_index = entry - 1u;
        if (slot_index >= store->slot_capacity ||
            store->slots[slot_index].state != NX_SLOT_USED ||
            store->index_tombstones == UINT32_MAX)
            return NX_STATUS_CORRUPT;
        status = nx_state_store_insert_table_locked(
            store, store->rehash_index, &store->rehash_tombstones,
            slot_index, scope);
        if (status != NX_STATUS_OK)
            return status == NX_STATUS_CONFLICT ? NX_STATUS_CORRUPT : status;
        store->index[store->rehash_cursor] = NX_STATE_STORE_INDEX_TOMBSTONE;
        store->index_tombstones++;
    }
    store->rehash_cursor++;
    if (store->rehash_cursor == store->index_capacity) {
        uint32_t *old_index = store->index;
        uint32_t old_tombstones = store->index_tombstones;
        store->index = store->rehash_index;
        store->rehash_index = old_index;
        store->index_tombstones = store->rehash_tombstones;
        store->rehash_tombstones = old_tombstones;
        store->rehash_state = NX_STATE_STORE_REHASH_CLEANING;
        store->rehash_cursor = 0u;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_rehash_clean_one(
    nx_state_store *store, nx_operation_scope *scope) {
    nx_status status;
    if (store->rehash_cursor >= store->index_capacity)
        return NX_STATUS_CORRUPT;
    status = nx_operation_scope_consume(scope, 1u);
    if (status != NX_STATUS_OK)
        return status;
    if (store->rehash_index[store->rehash_cursor] ==
        NX_STATE_STORE_INDEX_TOMBSTONE) {
        if (store->rehash_tombstones == 0u)
            return NX_STATUS_CORRUPT;
        store->rehash_tombstones--;
    } else if (store->rehash_index[store->rehash_cursor] != 0u) {
        return NX_STATUS_CORRUPT;
    }
    store->rehash_index[store->rehash_cursor] = 0u;
    store->rehash_cursor++;
    if (store->rehash_cursor == store->index_capacity) {
        store->rehash_state = NX_STATE_STORE_REHASH_IDLE;
        store->rehash_cursor = 0u;
        store->rehash_tombstones = 0u;
        nx_state_store_maybe_start_rehash_locked(store);
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_rehash_step_locked(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t work;
    nx_status status;
    if (store->rehash_state > NX_STATE_STORE_REHASH_CLEANING ||
        store->rehash_cursor > store->index_capacity ||
        store->index_tombstones > store->index_capacity ||
        store->rehash_tombstones > store->index_capacity)
        return NX_STATUS_CORRUPT;
    if (store->rehash_state == NX_STATE_STORE_REHASH_IDLE &&
        (store->rehash_cursor != 0u || store->rehash_tombstones != 0u))
        return NX_STATUS_CORRUPT;
    nx_state_store_maybe_start_rehash_locked(store);
    for (work = 0u; work < NX_STATE_STORE_REHASH_BUCKET_BUDGET &&
                   store->rehash_state != NX_STATE_STORE_REHASH_IDLE; ++work) {

        if (store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING)
            status = nx_state_store_rehash_migrate_one(store, scope);
        else if (store->rehash_state == NX_STATE_STORE_REHASH_CLEANING)
            status = nx_state_store_rehash_clean_one(store, scope);
        else
            return NX_STATUS_CORRUPT;
        if (status != NX_STATUS_OK)
            return status;
    }

    return NX_STATUS_OK;
}

static nx_status nx_state_store_insert_locked(nx_state_store *store,
                                              uint32_t slot_index,
                                              nx_operation_scope *scope) {
    if (store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING)
        return nx_state_store_insert_table_locked(
            store, store->rehash_index, &store->rehash_tombstones,
            slot_index, scope);
    return nx_state_store_insert_table_locked(
        store, store->index, &store->index_tombstones, slot_index, scope);
}

static nx_status nx_state_store_pop_free_locked(nx_state_store *store,
                                                nx_operation_scope *scope,
                                                uint32_t *slot_index) {
    uint32_t visited;
    for (visited = 0u; visited < store->slot_capacity &&
         store->free_head != NX_STATE_STORE_NO_SLOT; ++visited) {
        uint32_t candidate = store->free_head;
        nx_state_slot *slot;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (candidate >= store->slot_capacity)
            return NX_STATUS_CORRUPT;
        slot = &store->slots[candidate];
        if (slot->state != NX_SLOT_FREE)
            return NX_STATUS_CORRUPT;
        store->free_head = slot->next_free;
        slot->next_free = NX_STATE_STORE_NO_SLOT;
        if (slot->generation == UINT64_MAX) {
            slot->state = NX_SLOT_RETIRED;
            store->retired_count++;
            continue;
        }
        slot->generation++;
        slot->state = NX_SLOT_USED;
        *slot_index = candidate;
        return NX_STATUS_OK;
    }
    return store->free_head == NX_STATE_STORE_NO_SLOT
               ? NX_STATUS_RESOURCE_EXHAUSTED : NX_STATUS_CORRUPT;
}

static void nx_state_store_return_free_locked(nx_state_store *store,
                                              uint32_t slot_index) {
    nx_state_slot *slot = &store->slots[slot_index];
    memset(&slot->node, 0, sizeof(slot->node));
    slot->parent_slot = NX_STATE_STORE_NO_SLOT;
    slot->first_child_slot = NX_STATE_STORE_NO_SLOT;
    slot->previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->next_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->child_count = 0u;
    slot->hash = 0u;
    if (slot->generation == UINT64_MAX) {
        slot->state = NX_SLOT_RETIRED;
        slot->next_free = NX_STATE_STORE_NO_SLOT;
        store->retired_count++;
    } else {
        slot->state = NX_SLOT_FREE;
        slot->next_free = store->free_head;
        store->free_head = slot_index;
    }
}

static nx_status nx_state_store_unlock_scope_finish(
    nx_state_store *store, nx_operation_scope *scope, nx_status status,
    uint64_t completed, uint64_t detail, nx_result *result) {
    nx_state_store_unlock(store);
    return nx_state_store_scope_finish(scope, status, completed, detail,
                                       result);
}

static nx_status nx_state_store_validate_ready(nx_state_store *store,
                                               nx_result *result,
                                               uint64_t operation_id) {
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (store == NULL ||
        atomic_load_explicit(&store->initialized, memory_order_acquire) == 0u) {
        (void)nx_result_init(result, NX_STATUS_INVALID_ARGUMENT,
                             NX_RESULT_DOMAIN_REGISTRY, operation_id, 0u, 0u,
                             NX_CYCLES_UNKNOWN, 0u);
        return NX_STATUS_INVALID_ARGUMENT;
    }
    return NX_STATUS_OK;
}

nx_status nx_state_store_required_index_capacity(uint32_t slot_capacity,
                                                 uint32_t *index_capacity) {
    uint64_t required;
    uint32_t rounded = 1u;
    uint32_t shifts = 0u;

    if (index_capacity == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    *index_capacity = 0u;
    if (slot_capacity == 0u || slot_capacity > (UINT32_MAX / 2u))
        return NX_STATUS_INVALID_ARGUMENT;
    required = (uint64_t)slot_capacity * 2u;
    while ((uint64_t)rounded < required && shifts < 32u) {
        if (rounded > (UINT32_MAX / 2u))
            return NX_STATUS_OVERFLOW;
        rounded <<= 1;
        shifts++;
    }
    if ((uint64_t)rounded < required)
        return NX_STATUS_OVERFLOW;
    *index_capacity = rounded;
    return NX_STATUS_OK;
}

nx_status nx_state_node_spec_validate(const nx_state_node_spec *spec) {
    uint64_t payload_end = 0u;
    const uint32_t known_flags = NX_STATE_NODE_KNOWN_FLAGS;

    if (spec == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (spec->type_id == 0u || spec->schema_id == 0u ||
        spec->schema_version == 0u || spec->reserved != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (spec->lifecycle < NX_STATE_LIFECYCLE_LIVE ||
        spec->lifecycle > NX_STATE_LIFECYCLE_QUARANTINED)
        return NX_STATUS_MALFORMED;
    if ((spec->flags & ~known_flags) != 0u)
        return NX_STATUS_MALFORMED;
    if (nx_object_id_is_zero(spec->parent.id)) {
        if (spec->parent.generation != 0u)
            return NX_STATUS_MALFORMED;
    } else if (!nx_object_ref_is_valid(spec->parent)) {
        return NX_STATUS_MALFORMED;
    }
    if (nx_object_id_is_zero(spec->owner.id)) {
        if (spec->owner.generation != 0u)
            return NX_STATUS_MALFORMED;
    } else if (!nx_object_ref_is_valid(spec->owner)) {
        return NX_STATUS_MALFORMED;
    }
    return nx_u64_add(spec->payload_offset, spec->payload_length,
                      &payload_end);
}

typedef struct {
    size_t slots_bytes;
    size_t index_bytes;
    uint64_t hash_key[2];
} nx_state_store_init_plan;

static nx_status nx_state_store_init_result(nx_result *result,
                                            nx_status status) {
    (void)nx_result_init(result, status, NX_RESULT_DOMAIN_REGISTRY,
                         NX_OP_STATE_STORE_INIT, 0u, 0u,
                         NX_CYCLES_UNKNOWN, 0u);
    return status;
}

static nx_status nx_state_store_init_validate_parameters(
    nx_state_slot *slots, uint32_t slot_capacity, uint32_t *index,
    uint32_t *rehash_index, uint32_t index_capacity, uint64_t store_id,
    const uint64_t hash_key[2], nx_state_store_init_plan *plan) {
    uint32_t required_index;
    nx_status status;
    if (slots == NULL || index == NULL || rehash_index == NULL ||
        hash_key == NULL || slot_capacity == 0u ||
        slot_capacity > UINT32_MAX / 2u || index_capacity == 0u ||
        store_id == 0u ||
        ((uintptr_t)hash_key % (uintptr_t)_Alignof(uint64_t)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    plan->hash_key[0] = hash_key[0];
    plan->hash_key[1] = hash_key[1];
    if (plan->hash_key[0] == 0u && plan->hash_key[1] == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_state_store_required_index_capacity(slot_capacity,
                                                    &required_index);
    if (status != NX_STATUS_OK)
        return status;
    if (index_capacity < required_index ||
        (index_capacity & (index_capacity - 1u)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_size_mul((size_t)slot_capacity, sizeof(*slots),
                         &plan->slots_bytes);
    if (status != NX_STATUS_OK)
        return status;
    return nx_size_mul((size_t)index_capacity, sizeof(*index),
                       &plan->index_bytes);
}

static nx_status nx_state_store_init_validate_alignment(
    nx_state_slot *slots, uint32_t *index, uint32_t *rehash_index) {
    if (((uintptr_t)slots % (uintptr_t)_Alignof(nx_state_slot)) != 0u ||
        ((uintptr_t)index % (uintptr_t)_Alignof(uint32_t)) != 0u ||
        ((uintptr_t)rehash_index % (uintptr_t)_Alignof(uint32_t)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_init_validate_aliases(
    nx_state_store *store, nx_state_slot *slots, uint32_t *index,
    uint32_t *rehash_index, const uint64_t *hash_key,
    const nx_state_store_init_plan *plan) {
    int overlap[10];
    overlap[0] = nx_pointer_ranges_overlap(slots, plan->slots_bytes,
                                          index, plan->index_bytes);
    overlap[1] = nx_pointer_ranges_overlap(store, sizeof(*store),
                                          slots, plan->slots_bytes);
    overlap[2] = nx_pointer_ranges_overlap(store, sizeof(*store),
                                          index, plan->index_bytes);
    overlap[3] = nx_pointer_ranges_overlap(slots, plan->slots_bytes,
                                          rehash_index, plan->index_bytes);
    overlap[4] = nx_pointer_ranges_overlap(store, sizeof(*store),
                                          rehash_index, plan->index_bytes);
    overlap[5] = nx_pointer_ranges_overlap(index, plan->index_bytes,
                                          rehash_index, plan->index_bytes);
    overlap[6] = nx_pointer_ranges_overlap(hash_key, sizeof(plan->hash_key),
                                          store, sizeof(*store));
    overlap[7] = nx_pointer_ranges_overlap(hash_key, sizeof(plan->hash_key),
                                          slots, plan->slots_bytes);
    overlap[8] = nx_pointer_ranges_overlap(hash_key, sizeof(plan->hash_key),
                                          index, plan->index_bytes);
    overlap[9] = nx_pointer_ranges_overlap(hash_key, sizeof(plan->hash_key),
                                          rehash_index, plan->index_bytes);
    for (uint32_t i = 0u; i < 10u; ++i) {
        if (overlap[i] < 0)
            return NX_STATUS_OVERFLOW;
        if (overlap[i] != 0)
            return NX_STATUS_INVALID_ARGUMENT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_init_prepare_runtime(
    nx_state_store *store, uint32_t slot_capacity, uint32_t index_capacity,
    nx_cycle_counter *counter, nx_operation_scope *scope) {
    uint64_t fuel_limit;
    uint32_t metric;
    nx_status status;
    atomic_init(&store->initialized, 0u);
    atomic_init(&store->lock, 0u);
    if (atomic_is_lock_free(&store->lock) == 0)
        return NX_STATUS_NOT_SUPPORTED;
    for (metric = 0u; metric < NX_STATE_STORE_METRIC_COUNT; ++metric)
        nx_operation_metrics_init(&store->metrics[metric],
                                  NX_OP_STATE_STORE_INIT + (uint64_t)metric);
    status = nx_cycle_counter_init(counter);
    if (status != NX_STATUS_OK && status != NX_STATUS_NOT_SUPPORTED)
        return status;
    store->cycle_counter = *counter;
    fuel_limit = (uint64_t)slot_capacity +
                 ((uint64_t)index_capacity * UINT64_C(2)) + UINT64_C(16);
    return nx_operation_scope_begin_with_counter(
        scope, &store->metrics[NX_STATE_STORE_METRIC_INIT], counter,
        NX_OP_STATE_STORE_INIT, fuel_limit, UINT64_MAX);
}

static nx_status nx_state_store_init_populate(
    nx_state_store *store, nx_state_slot *slots, uint32_t slot_capacity,
    uint32_t *index, uint32_t *rehash_index, uint32_t index_capacity,
    uint64_t store_id, const nx_state_store_init_plan *plan,
    nx_operation_scope *scope, nx_result *result) {
    uint32_t slot;
    nx_status status = nx_operation_scope_consume(
        scope, (uint64_t)slot_capacity +
                   ((uint64_t)index_capacity * UINT64_C(2)));
    if (status != NX_STATUS_OK)
        return nx_state_store_scope_finish(scope, status, 0u, 0u, result);
    memset(slots, 0, plan->slots_bytes);
    memset(index, 0, plan->index_bytes);
    memset(rehash_index, 0, plan->index_bytes);
    for (slot = 0u; slot < slot_capacity; ++slot) {
        slots[slot].state = NX_SLOT_FREE;
        slots[slot].parent_slot = NX_STATE_STORE_NO_SLOT;
        slots[slot].first_child_slot = NX_STATE_STORE_NO_SLOT;
        slots[slot].previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
        slots[slot].next_sibling_slot = NX_STATE_STORE_NO_SLOT;
        slots[slot].next_free = slot + 1u < slot_capacity
                                    ? slot + 1u : NX_STATE_STORE_NO_SLOT;
    }
    store->slots = slots;
    store->slot_capacity = slot_capacity;
    store->free_head = 0u;
    store->count = 0u;
    store->retired_count = 0u;
    store->index = index;
    store->rehash_index = rehash_index;
    store->index_capacity = index_capacity;
    store->rehash_cursor = 0u;
    store->index_tombstones = 0u;
    store->rehash_tombstones = 0u;
    store->rehash_state = NX_STATE_STORE_REHASH_IDLE;
    store->store_id = store_id;
    store->next_object_id = 1u;
    store->hash_key[0] = plan->hash_key[0];
    store->hash_key[1] = plan->hash_key[1];
    store->epoch = 0u;
    atomic_store_explicit(&store->initialized, 1u, memory_order_release);
    return nx_state_store_scope_finish(scope, NX_STATUS_OK, 0u, 0u, result);
}

nx_status nx_state_store_init(nx_state_store *store,
                              nx_state_slot *slots,
                              uint32_t slot_capacity,
                              uint32_t *index,
                              uint32_t *rehash_index,
                              uint32_t index_capacity,
                              uint64_t store_id,
                              const uint64_t hash_key[2],
                              nx_result *result) {
    nx_state_store_init_plan plan;
    nx_cycle_counter counter;
    nx_operation_scope scope;
    nx_status status;
    if (result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (store == NULL)
        return nx_state_store_init_result(result, NX_STATUS_INVALID_ARGUMENT);
    status = nx_state_store_init_validate_parameters(
        slots, slot_capacity, index, rehash_index, index_capacity, store_id,
        hash_key, &plan);
    if (status == NX_STATUS_OK)
        status = nx_state_store_init_validate_alignment(slots, index,
                                                        rehash_index);
    if (status == NX_STATUS_OK)
        status = nx_state_store_init_validate_aliases(
            store, slots, index, rehash_index, hash_key, &plan);
    if (status != NX_STATUS_OK)
        return nx_state_store_init_result(result, status);
    status = nx_state_store_init_prepare_runtime(
        store, slot_capacity, index_capacity, &counter, &scope);
    if (status != NX_STATUS_OK)
        return nx_state_store_init_result(result, status);
    return nx_state_store_init_populate(
        store, slots, slot_capacity, index, rehash_index, index_capacity,
        store_id, &plan, &scope, result);
}

static nx_status nx_state_store_create_parent_locked(
    nx_state_store *store, const nx_state_node_spec *spec,
    nx_operation_scope *scope, uint32_t *parent_slot) {
    nx_status status = nx_state_store_rehash_step_locked(store, scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_parent_resolve_locked(
            store, spec->parent, scope, parent_slot);
    if (status != NX_STATUS_OK || *parent_slot == NX_STATE_STORE_NO_SLOT)
        return status;
    if (store->slots[*parent_slot].node.spec.lifecycle != NX_STATE_LIFECYCLE_LIVE)
        return NX_STATUS_ACCESS_DENIED;
    return nx_state_store_child_parent_validate_locked(store, *parent_slot);
}

static nx_status nx_state_store_create_locked(
    nx_state_store *store, const nx_state_node_spec *spec,
    nx_operation_scope *scope, nx_object_ref *created,
    uint64_t *epoch_snapshot) {
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    uint32_t parent_slot = NX_STATE_STORE_NO_SLOT;
    nx_state_slot *slot;
    nx_status status;
    if (store->epoch == UINT64_MAX || store->next_object_id == UINT64_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    status = nx_state_store_create_parent_locked(
        store, spec, scope, &parent_slot);
    if (status == NX_STATUS_OK)
        status = nx_state_store_pop_free_locked(store, scope, &slot_index);
    if (status != NX_STATUS_OK)
        return status;
    slot = &store->slots[slot_index];
    slot->node.ref.id.high = store->store_id;
    slot->node.ref.id.low = store->next_object_id;
    slot->node.ref.generation = slot->generation;
    slot->node.revision = 1u;
    slot->node.spec = *spec;
    slot->parent_slot = NX_STATE_STORE_NO_SLOT;
    slot->first_child_slot = NX_STATE_STORE_NO_SLOT;
    slot->previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->next_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->child_count = 0u;
    slot->hash = nx_siphash_object_id(slot->node.ref.id, store->hash_key);
    status = nx_state_store_insert_locked(store, slot_index, scope);
    if (status != NX_STATUS_OK) {
        nx_state_store_return_free_locked(store, slot_index);
        return status;
    }
    if (parent_slot != NX_STATE_STORE_NO_SLOT)
        nx_state_store_child_attach_locked(store, slot_index, parent_slot);
    store->next_object_id++;
    store->count++;
    store->epoch++;
    *created = slot->node.ref;
    *epoch_snapshot = store->epoch;
    return NX_STATUS_OK;
}


nx_status nx_state_store_create(nx_state_store *store,
                                const nx_state_node_spec *spec,
                                nx_object_ref *created,
                                nx_result *result) {
    nx_operation_scope scope;
    uint64_t operation_fuel;
    uint64_t epoch_snapshot = 0u;
    nx_status status;
    if (created != NULL)
        memset(created, 0, sizeof(*created));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_CREATE);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = (uint64_t)store->slot_capacity +
                     ((uint64_t)store->index_capacity * UINT64_C(8)) +
                     UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_CREATE,
                                        NX_OP_STATE_STORE_CREATE,
                                        operation_fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (created == NULL || spec == NULL)
        return nx_state_store_scope_finish(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, result);
    status = nx_state_node_spec_validate(spec);
    if (status != NX_STATUS_OK)
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(
            &scope, NX_STATUS_BUSY, 0u, 0u, result);
    status = nx_state_store_create_locked(
        store, spec, &scope, created, &epoch_snapshot);
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u,
        epoch_snapshot, result);
}

nx_status nx_state_store_read(nx_state_store *store,
                              nx_object_ref reference,
                              nx_state_node *node,
                              nx_result *result) {
    nx_operation_scope scope;
    nx_status status;
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    uint32_t index_position = NX_STATE_STORE_NO_SLOT;
    uint32_t *index_table = NULL;
    uint64_t operation_fuel;

    if (node != NULL)
        memset(node, 0, sizeof(*node));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_READ);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = ((uint64_t)store->index_capacity * UINT64_C(8)) +
                     UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_READ,
                                        NX_OP_STATE_STORE_READ,
                                        operation_fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (node == NULL || !nx_object_ref_is_valid(reference)) {
        status = NX_STATUS_INVALID_ARGUMENT;
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    }
    if (!nx_state_store_try_lock(store)) {
        status = NX_STATUS_BUSY;
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    }
    status = nx_state_store_rehash_step_locked(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_find_locked(store, reference.id, &scope,
                                            &slot_index, &index_position,
                                            &index_table);
    if (status == NX_STATUS_OK) {
        if (store->slots[slot_index].generation != reference.generation)
            status = NX_STATUS_STALE_REFERENCE;
        else
            *node = store->slots[slot_index].node;
    }
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u, 0u,
        result);
}

static nx_status nx_state_store_child_parent_locked(
    nx_state_store *store, nx_object_ref parent, nx_operation_scope *scope,
    uint32_t *parent_slot) {
    nx_status status = nx_state_store_parent_resolve_locked(
        store, parent, scope, parent_slot);
    if (status == NX_STATUS_OK && *parent_slot == NX_STATE_STORE_NO_SLOT)
        return NX_STATUS_INVALID_ARGUMENT;
    return status;
}

static nx_status nx_state_store_child_result_locked(
    nx_state_store *store, uint32_t child_slot, uint32_t parent_slot,
    nx_object_ref *child) {
    nx_state_slot *slot;
    if (child_slot == NX_STATE_STORE_NO_SLOT)
        return NX_STATUS_NOT_FOUND;
    if (child_slot >= store->slot_capacity)
        return NX_STATUS_CORRUPT;
    slot = &store->slots[child_slot];
    if (slot->state != NX_SLOT_USED || slot->parent_slot != parent_slot ||
        !nx_object_ref_is_valid(slot->node.ref))
        return NX_STATUS_CORRUPT;
    *child = slot->node.ref;
    return NX_STATUS_OK;
}

nx_status nx_state_store_child_first(nx_state_store *store,
                                     nx_object_ref parent,
                                     nx_object_ref *child,
                                     nx_result *result) {
    nx_operation_scope scope;
    uint32_t parent_slot = NX_STATE_STORE_NO_SLOT;
    uint64_t fuel;
    nx_status status;
    if (child != NULL)
        memset(child, 0, sizeof(*child));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_CHILD_FIRST);
    if (status != NX_STATUS_OK)
        return status;
    fuel = (uint64_t)store->index_capacity * UINT64_C(8) + UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_CHILD_FIRST,
                                        NX_OP_STATE_STORE_CHILD_FIRST, fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (child == NULL || !nx_object_ref_is_valid(parent))
        return nx_state_store_scope_finish(&scope, NX_STATUS_INVALID_ARGUMENT,
                                           0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(&scope, NX_STATUS_BUSY, 0u, 0u, result);
    status = nx_state_store_rehash_step_locked(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_child_parent_locked(store, parent, &scope,
                                                    &parent_slot);
    if (status == NX_STATUS_OK &&
        ((store->slots[parent_slot].child_count == 0u) !=
         (store->slots[parent_slot].first_child_slot == NX_STATE_STORE_NO_SLOT)))
        status = NX_STATUS_CORRUPT;
    if (status == NX_STATUS_OK)
        status = nx_state_store_child_result_locked(
            store, store->slots[parent_slot].first_child_slot, parent_slot, child);
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u, 0u, result);
}

nx_status nx_state_store_child_next(nx_state_store *store,
                                    nx_object_ref parent,
                                    nx_object_ref current,
                                    nx_object_ref *next,
                                    nx_result *result) {
    nx_operation_scope scope;
    uint32_t parent_slot = NX_STATE_STORE_NO_SLOT;
    uint32_t current_slot = NX_STATE_STORE_NO_SLOT;
    uint32_t position = NX_STATE_STORE_NO_SLOT;
    uint32_t *table = NULL;
    uint64_t fuel;
    nx_status status;
    if (next != NULL)
        memset(next, 0, sizeof(*next));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_CHILD_NEXT);
    if (status != NX_STATUS_OK)
        return status;
    fuel = (uint64_t)store->index_capacity * UINT64_C(10) + UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_CHILD_NEXT,
                                        NX_OP_STATE_STORE_CHILD_NEXT, fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (next == NULL || !nx_object_ref_is_valid(parent) ||
        !nx_object_ref_is_valid(current))
        return nx_state_store_scope_finish(&scope, NX_STATUS_INVALID_ARGUMENT,
                                           0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(&scope, NX_STATUS_BUSY, 0u, 0u, result);
    status = nx_state_store_rehash_step_locked(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_child_parent_locked(store, parent, &scope,
                                                    &parent_slot);
    if (status == NX_STATUS_OK)
        status = nx_state_store_find_locked(store, current.id, &scope,
                                            &current_slot, &position, &table);
    if (status == NX_STATUS_OK &&
        store->slots[current_slot].generation != current.generation)
        status = NX_STATUS_STALE_REFERENCE;
    if (status == NX_STATUS_OK &&
        store->slots[current_slot].parent_slot != parent_slot)
        status = NX_STATUS_INVALID_ARGUMENT;
    if (status == NX_STATUS_OK)
        status = nx_state_store_child_result_locked(
            store, store->slots[current_slot].next_sibling_slot, parent_slot, next);
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u, 0u, result);
}

nx_status nx_state_store_child_count(nx_state_store *store,
                                     nx_object_ref parent,
                                     uint32_t *count,
                                     nx_result *result) {
    nx_operation_scope scope;
    uint32_t parent_slot = NX_STATE_STORE_NO_SLOT;
    uint64_t fuel;
    nx_status status;
    if (count != NULL)
        *count = 0u;
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_CHILD_COUNT);
    if (status != NX_STATUS_OK)
        return status;
    fuel = (uint64_t)store->index_capacity * UINT64_C(8) + UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_CHILD_COUNT,
                                        NX_OP_STATE_STORE_CHILD_COUNT, fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (count == NULL || !nx_object_ref_is_valid(parent))
        return nx_state_store_scope_finish(&scope, NX_STATUS_INVALID_ARGUMENT,
                                           0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(&scope, NX_STATUS_BUSY, 0u, 0u, result);
    status = nx_state_store_rehash_step_locked(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_child_parent_locked(store, parent, &scope,
                                                    &parent_slot);
    if (status == NX_STATUS_OK &&
        ((store->slots[parent_slot].child_count == 0u) !=
         (store->slots[parent_slot].first_child_slot == NX_STATE_STORE_NO_SLOT)))
        status = NX_STATUS_CORRUPT;
    if (status == NX_STATUS_OK)
        *count = store->slots[parent_slot].child_count;
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u,
        status == NX_STATUS_OK ? *count : 0u, result);
}

static nx_status nx_state_store_update_target_resolve_locked(
    nx_state_store *store, nx_object_ref reference, uint64_t expected_revision,
    nx_operation_scope *scope, uint32_t *slot_index, nx_state_slot **slot) {
    uint32_t position = NX_STATE_STORE_NO_SLOT;
    uint32_t *table = NULL;
    nx_status status = nx_state_store_rehash_step_locked(store, scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_find_locked(
            store, reference.id, scope, slot_index, &position, &table);
    if (status != NX_STATUS_OK)
        return status;
    *slot = &store->slots[*slot_index];
    if ((*slot)->generation != reference.generation)
        return NX_STATUS_STALE_REFERENCE;
    if ((*slot)->node.revision != expected_revision)
        return NX_STATUS_CONFLICT;
    return (*slot)->node.revision == UINT64_MAX
        ? NX_STATUS_RESOURCE_EXHAUSTED : NX_STATUS_OK;
}

static nx_status nx_state_store_update_parent_locked(
    nx_state_store *store, uint32_t slot_index, nx_state_slot *slot,
    nx_object_ref parent, nx_operation_scope *scope) {
    uint32_t parent_slot = NX_STATE_STORE_NO_SLOT;
    nx_status status = nx_state_store_parent_resolve_locked(
        store, parent, scope, &parent_slot);
    if (status != NX_STATUS_OK)
        return status;
    if (parent_slot != NX_STATE_STORE_NO_SLOT) {
        if (store->slots[parent_slot].node.spec.lifecycle != NX_STATE_LIFECYCLE_LIVE)
            return NX_STATUS_ACCESS_DENIED;
        status = nx_state_store_parent_chain_validate_locked(
            store, slot_index, parent_slot, scope);
        if (status != NX_STATUS_OK)
            return status;
        if (parent_slot != slot->parent_slot) {
            status = nx_state_store_child_parent_validate_locked(
                store, parent_slot);
            if (status != NX_STATUS_OK)
                return status;
        }
    }
    if (parent_slot == slot->parent_slot)
        return NX_STATUS_OK;
    status = nx_state_store_child_detach_locked(store, slot_index);
    if (status == NX_STATUS_OK && parent_slot != NX_STATE_STORE_NO_SLOT)
        nx_state_store_child_attach_locked(store, slot_index, parent_slot);
    return status;
}

static nx_status nx_state_store_update_locked(
    nx_state_store *store, nx_object_ref reference,
    uint64_t expected_revision, const nx_state_node_spec *spec,
    nx_operation_scope *scope, nx_state_node *updated,
    uint64_t *epoch_snapshot) {
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    nx_state_slot *slot = NULL;
    nx_status status;
    if (store->epoch == UINT64_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    status = nx_state_store_update_target_resolve_locked(
        store, reference, expected_revision, scope, &slot_index, &slot);
    if (status == NX_STATUS_OK)
        status = nx_state_store_update_parent_locked(
            store, slot_index, slot, spec->parent, scope);
    if (status != NX_STATUS_OK)
        return status;
    slot->node.spec = *spec;
    slot->node.revision++;
    store->epoch++;
    if (updated != NULL)
        *updated = slot->node;
    *epoch_snapshot = store->epoch;
    return NX_STATUS_OK;
}


nx_status nx_state_store_update(nx_state_store *store,
                                nx_object_ref reference,
                                uint64_t expected_revision,
                                const nx_state_node_spec *spec,
                                nx_state_node *updated,
                                nx_result *result) {
    nx_operation_scope scope;
    uint64_t operation_fuel;
    uint64_t epoch_snapshot = 0u;
    nx_status status;
    if (updated != NULL)
        memset(updated, 0, sizeof(*updated));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_UPDATE);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = (uint64_t)store->slot_capacity +
                     ((uint64_t)store->index_capacity * UINT64_C(12)) +
                     UINT64_C(64);
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_UPDATE,
                                        NX_OP_STATE_STORE_UPDATE,
                                        operation_fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (!nx_object_ref_is_valid(reference) || spec == NULL)
        return nx_state_store_scope_finish(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, result);
    status = nx_state_node_spec_validate(spec);
    if (status != NX_STATUS_OK)
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(
            &scope, NX_STATUS_BUSY, 0u, 0u, result);
    status = nx_state_store_update_locked(
        store, reference, expected_revision, spec, &scope, updated,
        &epoch_snapshot);
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u,
        epoch_snapshot, result);
}

static nx_status nx_state_store_delete_resolve_locked(
    nx_state_store *store, nx_object_ref reference,
    uint64_t expected_revision, nx_operation_scope *scope,
    uint32_t *slot_index, uint32_t *index_position, uint32_t **index_table) {
    nx_status status;
    status = nx_state_store_rehash_step_locked(store, scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_find_locked(
            store, reference.id, scope, slot_index, index_position,
            index_table);
    if (status != NX_STATUS_OK)
        return status;
    if (store->slots[*slot_index].generation != reference.generation)
        return NX_STATUS_STALE_REFERENCE;
    if (store->slots[*slot_index].node.revision != expected_revision)
        return NX_STATUS_CONFLICT;
    if (store->count == 0u)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_delete_apply_locked(
    nx_state_store *store, uint32_t slot_index, uint32_t index_position,
    uint32_t *index_table, uint64_t *epoch_snapshot) {
    nx_state_slot *slot = &store->slots[slot_index];
    uint32_t *tombstone_counter;
    if (index_table == store->index)
        tombstone_counter = &store->index_tombstones;
    else if (index_table == store->rehash_index)
        tombstone_counter = &store->rehash_tombstones;
    else
        return NX_STATUS_CORRUPT;
    if (*tombstone_counter == UINT32_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    if (slot->child_count != 0u)
        return NX_STATUS_BUSY;
    {
        nx_status status = nx_state_store_child_detach_locked(store, slot_index);
        if (status != NX_STATUS_OK)
            return status;
    }
    index_table[index_position] = NX_STATE_STORE_INDEX_TOMBSTONE;
    (*tombstone_counter)++;
    memset(&slot->node, 0, sizeof(slot->node));
    slot->parent_slot = NX_STATE_STORE_NO_SLOT;
    slot->first_child_slot = NX_STATE_STORE_NO_SLOT;
    slot->previous_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->next_sibling_slot = NX_STATE_STORE_NO_SLOT;
    slot->child_count = 0u;
    slot->hash = 0u;
    store->count--;
    store->epoch++;
    if (slot->generation == UINT64_MAX) {
        slot->state = NX_SLOT_RETIRED;
        slot->next_free = NX_STATE_STORE_NO_SLOT;
        store->retired_count++;
    } else {
        slot->state = NX_SLOT_FREE;
        slot->next_free = store->free_head;
        store->free_head = slot_index;
    }
    *epoch_snapshot = store->epoch;
    return NX_STATUS_OK;
}

nx_status nx_state_store_delete(nx_state_store *store,
                                nx_object_ref reference,
                                uint64_t expected_revision,
                                nx_result *result) {
    nx_operation_scope scope;
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    uint32_t index_position = NX_STATE_STORE_NO_SLOT;
    uint32_t *index_table = NULL;
    uint64_t epoch_snapshot = 0u;
    uint64_t operation_fuel;
    nx_status status = nx_state_store_validate_ready(
        store, result, NX_OP_STATE_STORE_DELETE);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = (uint64_t)store->slot_capacity +
                     ((uint64_t)store->index_capacity * UINT64_C(8)) +
                     UINT64_C(64);
    status = nx_state_store_scope_begin(
        store, NX_STATE_STORE_METRIC_DELETE,
        NX_OP_STATE_STORE_DELETE, operation_fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (!nx_object_ref_is_valid(reference))
        return nx_state_store_scope_finish(
            &scope, NX_STATUS_INVALID_ARGUMENT, 0u, 0u, result);
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(&scope, NX_STATUS_BUSY,
                                           0u, 0u, result);
    if (store->epoch == UINT64_MAX)
        return nx_state_store_unlock_scope_finish(
            store, &scope, NX_STATUS_RESOURCE_EXHAUSTED, 0u,
            store->epoch, result);
    status = nx_state_store_delete_resolve_locked(
        store, reference, expected_revision, &scope, &slot_index,
        &index_position, &index_table);
    if (status == NX_STATUS_OK)
        status = nx_state_store_delete_apply_locked(
            store, slot_index, index_position, index_table, &epoch_snapshot);
    if (status != NX_STATUS_OK)
        epoch_snapshot = store->epoch;
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, status == NX_STATUS_OK ? 1u : 0u,
        epoch_snapshot, result);
}

static nx_status nx_state_store_validate_tree_node(
    nx_state_store *store, uint32_t index) {
    nx_state_slot *slot = &store->slots[index];
    if (slot->state != NX_SLOT_USED)
        return slot->parent_slot == NX_STATE_STORE_NO_SLOT &&
               slot->first_child_slot == NX_STATE_STORE_NO_SLOT &&
               slot->previous_sibling_slot == NX_STATE_STORE_NO_SLOT &&
               slot->next_sibling_slot == NX_STATE_STORE_NO_SLOT &&
               slot->child_count == 0u ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    if (nx_object_id_is_zero(slot->node.spec.parent.id))
        return slot->parent_slot == NX_STATE_STORE_NO_SLOT &&
               slot->node.spec.parent.generation == 0u &&
               slot->previous_sibling_slot == NX_STATE_STORE_NO_SLOT &&
               slot->next_sibling_slot == NX_STATE_STORE_NO_SLOT
            ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    if (slot->parent_slot >= store->slot_capacity)
        return NX_STATUS_CORRUPT;
    {
        nx_state_slot *parent = &store->slots[slot->parent_slot];
        if (parent->state != NX_SLOT_USED ||
            !nx_object_ref_equal(parent->node.ref, slot->node.spec.parent))
            return NX_STATUS_CORRUPT;
        if (slot->previous_sibling_slot == NX_STATE_STORE_NO_SLOT)
            return parent->first_child_slot == index ? NX_STATUS_OK
                                                     : NX_STATUS_CORRUPT;
        if (slot->previous_sibling_slot >= store->slot_capacity ||
            store->slots[slot->previous_sibling_slot].next_sibling_slot != index)
            return NX_STATUS_CORRUPT;
        if (slot->next_sibling_slot != NX_STATE_STORE_NO_SLOT &&
            (slot->next_sibling_slot >= store->slot_capacity ||
             store->slots[slot->next_sibling_slot].previous_sibling_slot != index))
            return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_child_list(
    nx_state_store *store, uint32_t parent_index, nx_operation_scope *scope) {
    nx_state_slot *parent = &store->slots[parent_index];
    uint32_t child = parent->first_child_slot;
    uint32_t previous = NX_STATE_STORE_NO_SLOT;
    uint32_t visited = 0u;
    while (child != NX_STATE_STORE_NO_SLOT && visited < store->slot_capacity) {
        nx_state_slot *entry;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (child >= store->slot_capacity)
            return NX_STATUS_CORRUPT;
        entry = &store->slots[child];
        if (entry->state != NX_SLOT_USED || entry->parent_slot != parent_index ||
            entry->previous_sibling_slot != previous)
            return NX_STATUS_CORRUPT;
        previous = child;
        child = entry->next_sibling_slot;
        visited++;
    }
    if (child != NX_STATE_STORE_NO_SLOT || visited != parent->child_count)
        return NX_STATUS_CORRUPT;
    return (visited == 0u) ==
           (parent->first_child_slot == NX_STATE_STORE_NO_SLOT)
        ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status nx_state_store_validate_tree_edges(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t index;
    for (index = 0u; index < store->slot_capacity; ++index) {
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status == NX_STATUS_OK)
            status = nx_state_store_validate_tree_node(store, index);
        if (status == NX_STATUS_OK)
            status = nx_state_store_validate_child_list(store, index, scope);
        if (status != NX_STATUS_OK)
            return status;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_tree_cycles(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t start;
    for (start = 0u; start < store->slot_capacity; ++start) {
        uint32_t current = start;
        if (store->slots[start].state != NX_SLOT_USED ||
            store->slots[start].validation_mark == NX_MARK_TREE_DONE)
            continue;
        while (current != NX_STATE_STORE_NO_SLOT) {
            nx_state_slot *slot = &store->slots[current];
            nx_status status = nx_operation_scope_consume(scope, 1u);
            if (status != NX_STATUS_OK)
                return status;
            if (slot->validation_mark == NX_MARK_TREE_DONE)
                current = NX_STATE_STORE_NO_SLOT;
            else if (slot->validation_mark == NX_MARK_TREE_VISITING)
                return NX_STATUS_CORRUPT;
            else {
                slot->validation_mark = NX_MARK_TREE_VISITING;
                current = slot->parent_slot;
            }
        }
        current = start;
        while (current != NX_STATE_STORE_NO_SLOT &&
               store->slots[current].validation_mark == NX_MARK_TREE_VISITING) {
            nx_state_slot *slot = &store->slots[current];
            nx_status status = nx_operation_scope_consume(scope, 1u);
            if (status != NX_STATUS_OK)
                return status;
            slot->validation_mark = NX_MARK_TREE_DONE;
            current = slot->parent_slot;
        }
    }
    {
        nx_status cleanup_status = NX_STATUS_OK;
        for (start = 0u; start < store->slot_capacity; ++start) {
            nx_status step = nx_operation_scope_consume(scope, 1u);
            store->slots[start].validation_mark = 0u;
            if (cleanup_status == NX_STATUS_OK && step != NX_STATUS_OK)
                cleanup_status = step;
        }
        return cleanup_status;
    }
}

static nx_status nx_state_store_validate_tree(
    nx_state_store *store, nx_operation_scope *scope) {
    nx_status status = nx_state_store_validate_tree_edges(store, scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_tree_cycles(store, scope);
    return status;
}


static nx_status nx_state_store_validation_finish(
    nx_state_store *store, nx_operation_scope *scope, nx_status status,
    nx_result *result) {
    uint32_t i;
    uint32_t count_snapshot;
    uint64_t epoch_snapshot;
    for (i = 0u; i < store->slot_capacity; ++i) {
        nx_status cleanup_status = nx_operation_scope_consume(scope, 1u);
        store->slots[i].validation_mark = 0u;
        if (status == NX_STATUS_OK && cleanup_status != NX_STATUS_OK)
            status = cleanup_status;
    }
    count_snapshot = store->count;
    epoch_snapshot = store->epoch;
    return nx_state_store_unlock_scope_finish(
        store, scope, status, status == NX_STATUS_OK ? count_snapshot : 0u,
        epoch_snapshot, result);
}

typedef struct {
    uint32_t used_count;
    uint32_t free_count;
    uint32_t retired_count;
    uint32_t index_count;
    uint32_t index_tombstone_count;
    uint32_t rehash_tombstone_count;
} nx_state_store_validation_counts;

static nx_status nx_state_store_validate_slots(
    nx_state_store *store, nx_operation_scope *scope,
    nx_state_store_validation_counts *counts) {
    uint32_t index;
    for (index = 0u; index < store->slot_capacity; ++index) {
        nx_state_slot *slot = &store->slots[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (slot->validation_mark != 0u)
            return NX_STATUS_CORRUPT;
        if (slot->state == NX_SLOT_USED) {
            if (!nx_object_ref_is_valid(slot->node.ref) ||
                slot->node.ref.generation != slot->generation ||
                slot->hash != nx_siphash_object_id(slot->node.ref.id,
                                                   store->hash_key) ||
                nx_state_node_spec_validate(&slot->node.spec) != NX_STATUS_OK)
                return NX_STATUS_CORRUPT;
            counts->used_count++;
        } else if (slot->state == NX_SLOT_FREE) {
            if (slot->generation == UINT64_MAX)
                return NX_STATUS_CORRUPT;
            counts->free_count++;
        } else if (slot->state == NX_SLOT_RETIRED) {
            if (slot->generation != UINT64_MAX)
                return NX_STATUS_CORRUPT;
            counts->retired_count++;
        } else {
            return NX_STATUS_CORRUPT;
        }
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_index_table(
    nx_state_store *store, nx_operation_scope *scope, uint32_t table_index,
    nx_state_store_validation_counts *counts) {
    uint32_t *table = table_index == 0u ? store->index : store->rehash_index;
    uint32_t mark = table_index == 0u ? NX_MARK_INDEX : NX_MARK_REHASH_INDEX;
    uint32_t index;
    for (index = 0u; index < store->index_capacity; ++index) {
        uint32_t entry;
        uint32_t slot_index;
        nx_state_slot *slot;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        entry = table[index];
        if (entry == 0u)
            continue;
        if (entry == NX_STATE_STORE_INDEX_TOMBSTONE) {
            if (table_index == 0u)
                counts->index_tombstone_count++;
            else
                counts->rehash_tombstone_count++;
            continue;
        }
        if (store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING &&
            table_index == 0u && index < store->rehash_cursor)
            return NX_STATUS_CORRUPT;
        slot_index = entry - 1u;
        if (slot_index >= store->slot_capacity)
            return NX_STATUS_CORRUPT;
        slot = &store->slots[slot_index];
        if (slot->state != NX_SLOT_USED || slot->validation_mark != 0u)
            return NX_STATUS_CORRUPT;
        slot->validation_mark = mark;
        counts->index_count++;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_index_tables(
    nx_state_store *store, nx_operation_scope *scope,
    nx_state_store_validation_counts *counts) {
    uint32_t table_count =
        store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING ? 2u : 1u;
    uint32_t table_index;
    for (table_index = 0u; table_index < table_count; ++table_index) {
        nx_status status = nx_state_store_validate_index_table(
            store, scope, table_index, counts);
        if (status != NX_STATUS_OK)
            return status;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_rehash_tail(
    nx_state_store *store, nx_operation_scope *scope,
    nx_state_store_validation_counts *counts) {
    uint32_t index;
    if (store->rehash_state == NX_STATE_STORE_REHASH_MIGRATING)
        return NX_STATUS_OK;
    for (index = 0u; index < store->index_capacity; ++index) {
        uint32_t entry;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        entry = store->rehash_index[index];
        if (store->rehash_state == NX_STATE_STORE_REHASH_IDLE) {
            if (entry != 0u)
                return NX_STATUS_CORRUPT;
        } else if (index < store->rehash_cursor) {
            if (entry != 0u)
                return NX_STATUS_CORRUPT;
        } else if (entry == NX_STATE_STORE_INDEX_TOMBSTONE) {
            counts->rehash_tombstone_count++;
        } else if (entry != 0u) {
            return NX_STATUS_CORRUPT;
        }
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_index_marks(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t index;
    for (index = 0u; index < store->slot_capacity; ++index) {
        nx_state_slot *slot = &store->slots[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if ((slot->state == NX_SLOT_USED && slot->validation_mark == 0u) ||
            (slot->state != NX_SLOT_USED && slot->validation_mark != 0u))
            return NX_STATUS_CORRUPT;
        slot->validation_mark = 0u;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_free_list(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t free_index = store->free_head;
    uint32_t traversed;
    for (traversed = 0u; free_index != NX_STATE_STORE_NO_SLOT &&
         traversed < store->slot_capacity; ++traversed) {
        nx_state_slot *slot;
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (free_index >= store->slot_capacity)
            return NX_STATUS_CORRUPT;
        slot = &store->slots[free_index];
        if (slot->state != NX_SLOT_FREE || slot->validation_mark != 0u)
            return NX_STATUS_CORRUPT;
        slot->validation_mark = NX_MARK_FREE;
        free_index = slot->next_free;
    }
    return free_index == NX_STATE_STORE_NO_SLOT ? NX_STATUS_OK
                                                : NX_STATUS_CORRUPT;
}

static nx_status nx_state_store_validate_final_marks(
    nx_state_store *store, nx_operation_scope *scope) {
    uint32_t index;
    for (index = 0u; index < store->slot_capacity; ++index) {
        nx_state_slot *slot = &store->slots[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if ((slot->state == NX_SLOT_FREE &&
             slot->validation_mark != NX_MARK_FREE) ||
            (slot->state == NX_SLOT_USED && slot->validation_mark != 0u) ||
            (slot->state == NX_SLOT_RETIRED && slot->validation_mark != 0u))
            return NX_STATUS_CORRUPT;
        slot->validation_mark = 0u;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_validate_accounting(
    const nx_state_store *store,
    const nx_state_store_validation_counts *counts) {
    if (store->rehash_state > NX_STATE_STORE_REHASH_CLEANING ||
        store->rehash_cursor > store->index_capacity ||
        store->index_tombstones > store->index_capacity ||
        store->rehash_tombstones > store->index_capacity ||
        counts->index_tombstone_count != store->index_tombstones ||
        (store->rehash_state != NX_STATE_STORE_REHASH_IDLE &&
         counts->rehash_tombstone_count != store->rehash_tombstones) ||
        (store->rehash_state == NX_STATE_STORE_REHASH_IDLE &&
         (store->rehash_cursor != 0u || store->rehash_tombstones != 0u)) ||
        counts->used_count != store->count ||
        counts->index_count != store->count ||
        counts->retired_count != store->retired_count ||
        (uint64_t)counts->used_count + (uint64_t)counts->free_count +
            (uint64_t)counts->retired_count != (uint64_t)store->slot_capacity)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

nx_status nx_state_store_validate(nx_state_store *store, nx_result *result) {
    nx_operation_scope scope;
    nx_state_store_validation_counts counts = {0u};
    uint64_t operation_fuel;
    nx_status status = nx_state_store_validate_ready(
        store, result, NX_OP_STATE_STORE_VALIDATE);
    if (status != NX_STATUS_OK)
        return status;
    operation_fuel = ((uint64_t)store->slot_capacity * UINT64_C(14)) +
                     ((uint64_t)store->index_capacity * UINT64_C(4)) +
                     UINT64_C(64);
    status = nx_state_store_scope_begin(
        store, NX_STATE_STORE_METRIC_VALIDATE,
        NX_OP_STATE_STORE_VALIDATE, operation_fuel, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (!nx_state_store_try_lock(store))
        return nx_state_store_scope_finish(&scope, NX_STATUS_BUSY,
                                           0u, 0u, result);
    status = nx_state_store_validate_slots(store, &scope, &counts);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_index_tables(store, &scope, &counts);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_rehash_tail(store, &scope, &counts);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_index_marks(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_tree(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_free_list(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_final_marks(store, &scope);
    if (status == NX_STATUS_OK)
        status = nx_state_store_validate_accounting(store, &counts);
    return nx_state_store_validation_finish(store, &scope, status, result);
}

nx_status nx_state_store_stats_read(nx_state_store *store,
                                     nx_state_store_stats *stats,
                                     nx_result *result) {
    nx_operation_scope scope;
    nx_status status;

    if (stats != NULL)
        memset(stats, 0, sizeof(*stats));
    status = nx_state_store_validate_ready(store, result,
                                           NX_OP_STATE_STORE_STATS_READ);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_scope_begin(store, NX_STATE_STORE_METRIC_STATS,
                                        NX_OP_STATE_STORE_STATS_READ, 8u, &scope);
    if (status != NX_STATUS_OK)
        return status;
    if (stats == NULL) {
        status = NX_STATUS_INVALID_ARGUMENT;
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    }
    if (!nx_state_store_try_lock(store)) {
        status = NX_STATUS_BUSY;
        return nx_state_store_scope_finish(&scope, status, 0u, 0u, result);
    }
    if (store->count > store->slot_capacity ||
        store->retired_count > store->slot_capacity - store->count) {
        status = NX_STATUS_CORRUPT;
        return nx_state_store_unlock_scope_finish(
            store, &scope, status, 0u, 0u, result);
    }
    stats->epoch = store->epoch;
    stats->count = store->count;
    stats->slot_capacity = store->slot_capacity;
    stats->index_capacity = store->index_capacity;
    stats->free_slots = store->slot_capacity - store->count -
                        store->retired_count;
    stats->retired_slots = store->retired_count;
    stats->index_tombstones = store->index_tombstones;
    stats->rehash_tombstones = store->rehash_tombstones;
    stats->rehash_cursor = store->rehash_cursor;
    stats->rehash_state = store->rehash_state;
    status = NX_STATUS_OK;
    return nx_state_store_unlock_scope_finish(
        store, &scope, status, stats->count, stats->epoch, result);
}


#define NX_STATE_SNAPSHOT_NODE_RECORD_BYTES 148u

static void nx_snapshot_write_u16(uint8_t *bytes, size_t *offset,
                                  uint16_t value) {
    bytes[*offset] = (uint8_t)value;
    bytes[*offset + 1u] = (uint8_t)(value >> 8u);
    *offset += 2u;
}

static void nx_snapshot_write_u32(uint8_t *bytes, size_t *offset,
                                  uint32_t value) {
    unsigned int i;
    for (i = 0u; i < 4u; ++i)
        bytes[*offset + (size_t)i] = (uint8_t)(value >> (i * 8u));
    *offset += 4u;
}

static void nx_snapshot_write_u64(uint8_t *bytes, size_t *offset,
                                  uint64_t value) {
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        bytes[*offset + (size_t)i] = (uint8_t)(value >> (i * 8u));
    *offset += 8u;
}

static void nx_snapshot_write_ref(uint8_t *bytes, size_t *offset,
                                  nx_object_ref reference) {
    nx_snapshot_write_u64(bytes, offset, reference.id.high);
    nx_snapshot_write_u64(bytes, offset, reference.id.low);
    nx_snapshot_write_u64(bytes, offset, reference.generation);
}

static nx_status nx_state_snapshot_digest_node(
    nx_sha256_context *context, const nx_state_node *node) {
    uint8_t record[NX_STATE_SNAPSHOT_NODE_RECORD_BYTES];
    size_t offset = 0u;
    nx_snapshot_write_ref(record, &offset, node->ref);
    nx_snapshot_write_u64(record, &offset, node->revision);
    nx_snapshot_write_ref(record, &offset, node->spec.parent);
    nx_snapshot_write_ref(record, &offset, node->spec.owner);
    nx_snapshot_write_u64(record, &offset, node->spec.type_id);
    nx_snapshot_write_u32(record, &offset, node->spec.schema_id);
    nx_snapshot_write_u16(record, &offset, node->spec.schema_version);
    nx_snapshot_write_u16(record, &offset, node->spec.lifecycle);
    nx_snapshot_write_u32(record, &offset, node->spec.flags);
    nx_snapshot_write_u64(record, &offset, node->spec.payload_offset);
    nx_snapshot_write_u64(record, &offset, node->spec.payload_length);
    memcpy(record + offset, node->spec.payload_digest.bytes,
           sizeof(node->spec.payload_digest.bytes));
    offset += sizeof(node->spec.payload_digest.bytes);
    if (offset != sizeof(record))
        return NX_STATUS_INTERNAL;
    return nx_sha256_update(context, (nx_const_span){record, sizeof(record)});
}

static nx_status nx_state_snapshot_compute_digest(
    const nx_state_snapshot *snapshot, nx_digest256 *digest) {
    static const char domain[] = "NXOLMO:STATE:SNAPSHOT:V1";
    uint8_t header[24];
    nx_sha256_context context;
    nx_status status;
    size_t offset = 0u;
    uint32_t index;
    if (snapshot == NULL || digest == NULL ||
        (snapshot->node_count != 0u && snapshot->nodes == NULL))
        return NX_STATUS_INVALID_ARGUMENT;
    nx_snapshot_write_u32(header, &offset, snapshot->format_version);
    nx_snapshot_write_u64(header, &offset, snapshot->store_id);
    nx_snapshot_write_u64(header, &offset, snapshot->epoch);
    nx_snapshot_write_u32(header, &offset, snapshot->node_count);
    if (offset != sizeof(header))
        return NX_STATUS_INTERNAL;
    nx_sha256_init(&context);
    status = nx_sha256_update(
        &context, (nx_const_span){(const uint8_t *)(const void *)domain,
                                  sizeof(domain) - 1u});
    if (status == NX_STATUS_OK)
        status = nx_sha256_update(
            &context, (nx_const_span){header, sizeof(header)});
    for (index = 0u; status == NX_STATUS_OK &&
         index < snapshot->node_count; ++index)
        status = nx_state_snapshot_digest_node(
            &context, &snapshot->nodes[index]);
    if (status == NX_STATUS_OK)
        status = nx_sha256_final(&context, digest);
    return status;
}

static int nx_state_snapshot_node_less(const nx_state_node *left,
                                       const nx_state_node *right) {
    if (left->ref.id.high != right->ref.id.high)
        return left->ref.id.high < right->ref.id.high;
    return left->ref.id.low < right->ref.id.low;
}

static void nx_state_snapshot_sift_down(nx_state_node *nodes,
                                        uint32_t root, uint32_t end) {
    while (root < end / 2u) {
        uint32_t child = (root * 2u) + 1u;
        nx_state_node temporary;
        if (child + 1u < end &&
            nx_state_snapshot_node_less(&nodes[child], &nodes[child + 1u]))
            child++;
        if (!nx_state_snapshot_node_less(&nodes[root], &nodes[child]))
            return;
        temporary = nodes[root];
        nodes[root] = nodes[child];
        nodes[child] = temporary;
        root = child;
    }
}

static void nx_state_snapshot_sort(nx_state_node *nodes, uint32_t count) {
    uint32_t start;
    uint32_t end;
    if (count < 2u)
        return;
    start = count / 2u;
    while (start != 0u) {
        start--;
        nx_state_snapshot_sift_down(nodes, start, count);
    }
    end = count;
    while (end > 1u) {
        nx_state_node temporary = nodes[0];
        nodes[0] = nodes[end - 1u];
        nodes[end - 1u] = temporary;
        end--;
        nx_state_snapshot_sift_down(nodes, 0u, end);
    }
}

nx_status nx_state_snapshot_validate(const nx_state_snapshot *snapshot) {
    nx_digest256 computed;
    nx_status status;
    size_t nodes_bytes = 0u;
    uint32_t i;

    if (snapshot == NULL ||
        ((uintptr_t)snapshot % (uintptr_t)_Alignof(nx_state_snapshot)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (snapshot->format_version != NX_STATE_SNAPSHOT_FORMAT_VERSION)
        return NX_STATUS_VERSION_MISMATCH;
    if (snapshot->store_id == 0u ||
        (snapshot->node_count != 0u && snapshot->nodes == NULL))
        return NX_STATUS_MALFORMED;
    if (snapshot->node_count != 0u) {
        if (((uintptr_t)snapshot->nodes %
             (uintptr_t)_Alignof(nx_state_node)) != 0u)
            return NX_STATUS_INVALID_ARGUMENT;
        status = nx_size_mul((size_t)snapshot->node_count,
                             sizeof(*snapshot->nodes), &nodes_bytes);
        if (status != NX_STATUS_OK)
            return status;
        if (nodes_bytes == 0u)
            return NX_STATUS_OVERFLOW;
    }

    for (i = 0u; i < snapshot->node_count; ++i) {
        const nx_state_node *node = &snapshot->nodes[i];
        if (!nx_object_ref_is_valid(node->ref) ||
            node->ref.id.high != snapshot->store_id ||
            node->revision == 0u ||
            nx_state_node_spec_validate(&node->spec) != NX_STATUS_OK)
            return NX_STATUS_CORRUPT;
        if (i != 0u &&
            !nx_state_snapshot_node_less(&snapshot->nodes[i - 1u], node))
            return NX_STATUS_CORRUPT;
    }

    status = nx_state_snapshot_compute_digest(snapshot, &computed);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_crypto_constant_time_equal(computed.bytes,
                                      snapshot->root_digest.bytes,
                                      sizeof(computed.bytes)) == 0)
        return NX_STATUS_INTEGRITY_FAILURE;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_snapshot_finish(
    nx_operation_scope *scope, nx_status status, nx_state_node *storage,
    uint32_t copied, uint32_t captured_count, uint64_t captured_epoch,
    nx_result *result) {
    if (status != NX_STATUS_OK && storage != NULL && copied != 0u)
        memset(storage, 0, (size_t)copied * sizeof(*storage));
    return nx_state_store_scope_finish(
        scope, status, status == NX_STATUS_OK ? captured_count : 0u,
        status == NX_STATUS_OK ? captured_epoch : 0u, result);
}

static nx_status nx_state_store_snapshot_unlock_finish(
    nx_state_store *store, nx_operation_scope *scope, nx_status status,
    nx_state_node *storage, uint32_t copied, uint32_t captured_count,
    uint64_t captured_epoch, nx_result *result) {
    captured_epoch = store->epoch;
    nx_state_store_unlock(store);
    return nx_state_store_snapshot_finish(scope, status, storage, copied,
                                          captured_count, captured_epoch,
                                          result);
}

static nx_status nx_state_store_snapshot_validate_outputs(
    nx_state_store *store, nx_state_snapshot *snapshot, nx_result *result,
    size_t *slots_bytes, size_t *index_bytes) {
    int overlaps[9];
    uint32_t index;
    nx_status status = nx_state_store_validate_ready(
        store, result, NX_OP_STATE_STORE_SNAPSHOT_CAPTURE);
    if (status != NX_STATUS_OK)
        return status;
    if (snapshot == NULL ||
        ((uintptr_t)snapshot % (uintptr_t)_Alignof(nx_state_snapshot)) != 0u ||
        ((uintptr_t)result % (uintptr_t)_Alignof(nx_result)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_size_mul((size_t)store->slot_capacity,
                                   sizeof(*store->slots), slots_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_size_mul((size_t)store->index_capacity,
                         sizeof(*store->index), index_bytes);
    if (status != NX_STATUS_OK)
        return status;
    overlaps[0] = nx_pointer_ranges_overlap(result, sizeof(*result),
                                             store, sizeof(*store));
    overlaps[1] = nx_pointer_ranges_overlap(result, sizeof(*result),
                                             store->slots, *slots_bytes);
    overlaps[2] = nx_pointer_ranges_overlap(result, sizeof(*result),
                                             store->index, *index_bytes);
    overlaps[3] = nx_pointer_ranges_overlap(result, sizeof(*result),
                                             store->rehash_index, *index_bytes);
    overlaps[4] = nx_pointer_ranges_overlap(snapshot, sizeof(*snapshot),
                                             store, sizeof(*store));
    overlaps[5] = nx_pointer_ranges_overlap(snapshot, sizeof(*snapshot),
                                             store->slots, *slots_bytes);
    overlaps[6] = nx_pointer_ranges_overlap(snapshot, sizeof(*snapshot),
                                             store->index, *index_bytes);
    overlaps[7] = nx_pointer_ranges_overlap(snapshot, sizeof(*snapshot),
                                             store->rehash_index, *index_bytes);
    overlaps[8] = nx_pointer_ranges_overlap(snapshot, sizeof(*snapshot),
                                             result, sizeof(*result));
    for (index = 0u; index < 9u; ++index) {
        if (overlaps[index] < 0)
            return NX_STATUS_OVERFLOW;
        if (overlaps[index] != 0)
            return NX_STATUS_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return NX_STATUS_OK;
}

static nx_status nx_state_store_snapshot_validate_storage(
    nx_state_store *store, nx_state_node *storage, uint32_t capacity,
    nx_state_snapshot *snapshot, nx_result *result, size_t slots_bytes,
    size_t index_bytes) {
    int overlaps[6];
    size_t storage_bytes;
    uint32_t index;
    nx_status status;
    if ((storage == NULL && capacity != 0u) ||
        (storage != NULL && capacity == 0u))
        return NX_STATUS_INVALID_ARGUMENT;
    if (storage == NULL)
        return NX_STATUS_OK;
    status = nx_size_mul((size_t)capacity, sizeof(*storage), &storage_bytes);
    if (status != NX_STATUS_OK)
        return status;
    if (((uintptr_t)storage % (uintptr_t)_Alignof(nx_state_node)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    overlaps[0] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             store, sizeof(*store));
    overlaps[1] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             store->slots, slots_bytes);
    overlaps[2] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             store->index, index_bytes);
    overlaps[3] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             store->rehash_index, index_bytes);
    overlaps[4] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             result, sizeof(*result));
    overlaps[5] = nx_pointer_ranges_overlap(storage, storage_bytes,
                                             snapshot, sizeof(*snapshot));
    for (index = 0u; index < 6u; ++index) {
        if (overlaps[index] < 0)
            return NX_STATUS_OVERFLOW;
        if (overlaps[index] != 0)
            return NX_STATUS_INVALID_ARGUMENT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_state_store_snapshot_scope_begin(
    nx_state_store *store, nx_operation_scope *scope) {
    uint64_t fuel = ((uint64_t)store->slot_capacity * UINT64_C(65)) +
                    UINT64_C(128);
    return nx_state_store_scope_begin(
        store, NX_STATE_STORE_METRIC_SNAPSHOT,
        NX_OP_STATE_STORE_SNAPSHOT_CAPTURE, fuel, scope);
}

static nx_status nx_state_store_snapshot_lock(
    nx_state_store *store, uint32_t capacity, uint32_t *captured_count) {
    if (!nx_state_store_try_lock(store))
        return NX_STATUS_BUSY;
    if (store->count > capacity) {
        nx_state_store_unlock(store);
        return NX_STATUS_NO_SPACE;
    }
    *captured_count = store->count;
    return NX_STATUS_OK;
}

static nx_status nx_state_store_snapshot_copy_locked(
    nx_state_store *store, nx_operation_scope *scope, nx_state_node *storage,
    uint32_t captured_count, uint32_t *copied) {
    uint32_t index;
    for (index = 0u; index < store->slot_capacity; ++index) {
        nx_state_slot *slot = &store->slots[index];
        nx_status status = nx_operation_scope_consume(scope, 1u);
        if (status != NX_STATUS_OK)
            return status;
        if (slot->state == NX_SLOT_USED) {
            if (*copied >= captured_count ||
                nx_state_node_spec_validate(&slot->node.spec) != NX_STATUS_OK ||
                !nx_object_ref_is_valid(slot->node.ref) ||
                slot->node.ref.id.high != store->store_id ||
                slot->node.ref.generation != slot->generation ||
                slot->node.revision == 0u)
                return NX_STATUS_CORRUPT;
            storage[*copied] = slot->node;
            (*copied)++;
        } else if (slot->state != NX_SLOT_FREE &&
                   slot->state != NX_SLOT_RETIRED) {
            return NX_STATUS_CORRUPT;
        }
    }
    return *copied == captured_count ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status nx_state_store_snapshot_finalize(
    nx_operation_scope *scope, nx_state_node *storage, uint32_t captured_count,
    uint64_t captured_epoch, uint64_t store_id,
    nx_state_snapshot *snapshot) {
    nx_state_snapshot candidate;
    nx_status status = nx_operation_scope_consume(
        scope, (uint64_t)captured_count * UINT64_C(64));
    if (status != NX_STATUS_OK)
        return status;
    nx_state_snapshot_sort(storage, captured_count);
    memset(&candidate, 0, sizeof(candidate));
    candidate.nodes = captured_count == 0u ? NULL : storage;
    candidate.store_id = store_id;
    candidate.epoch = captured_epoch;
    candidate.node_count = captured_count;
    candidate.format_version = NX_STATE_SNAPSHOT_FORMAT_VERSION;
    status = nx_state_snapshot_compute_digest(&candidate,
                                               &candidate.root_digest);
    if (status == NX_STATUS_OK)
        status = nx_state_snapshot_validate(&candidate);
    if (status == NX_STATUS_OK)
        *snapshot = candidate;
    return status;
}

nx_status nx_state_store_snapshot_capture(nx_state_store *store,
                                          nx_state_node *storage,
                                          uint32_t capacity,
                                          nx_state_snapshot *snapshot,
                                          nx_result *result) {
    nx_operation_scope scope;
    uint64_t captured_epoch = 0u;
    uint32_t captured_count = 0u;
    uint32_t copied = 0u;
    size_t slots_bytes = 0u;
    size_t index_bytes = 0u;
    nx_status status = nx_state_store_snapshot_validate_outputs(
        store, snapshot, result, &slots_bytes, &index_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_snapshot_validate_storage(
        store, storage, capacity, snapshot, result, slots_bytes, index_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_snapshot_scope_begin(store, &scope);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_snapshot_lock(store, capacity, &captured_count);
    if (status != NX_STATUS_OK)
        return nx_state_store_snapshot_finish(
            &scope, status, storage, copied, captured_count, captured_epoch,
            result);
    status = nx_state_store_snapshot_copy_locked(
        store, &scope, storage, captured_count, &copied);
    if (status != NX_STATUS_OK)
        return nx_state_store_snapshot_unlock_finish(
            store, &scope, status, storage, copied, captured_count,
            captured_epoch, result);
    captured_epoch = store->epoch;
    nx_state_store_unlock(store);
    status = nx_state_store_snapshot_finalize(
        &scope, storage, captured_count, captured_epoch, store->store_id,
        snapshot);
    return nx_state_store_snapshot_finish(
        &scope, status, storage, copied, captured_count, captured_epoch,
        result);
}

nx_status nx_state_store_metrics_read(const nx_state_store *store,
                                      nx_state_store_metric metric,
                                      nx_operation_metrics_snapshot *snapshot) {
    if (store == NULL ||
        atomic_load_explicit(&store->initialized, memory_order_acquire) == 0u ||
        snapshot == NULL ||
        (uint32_t)metric >= NX_STATE_STORE_METRIC_COUNT)
        return NX_STATUS_INVALID_ARGUMENT;
    return nx_operation_metrics_read(&store->metrics[(size_t)metric], snapshot);
}
