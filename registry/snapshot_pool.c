/* NXOLMO registry algorithm port; one product-owned implementation. */
#include "nexs_snapshot_pool.h"

#include "nexs_checked.h"

#include <stddef.h>
#include <string.h>

static int pool_try_lock(nx_state_snapshot_pool *pool) {
  uint32_t expected = 0u;
  return atomic_compare_exchange_strong_explicit(
      &pool->lock, &expected, 1u, memory_order_acquire, memory_order_relaxed);
}

static void pool_unlock(nx_state_snapshot_pool *pool) {
  atomic_store_explicit(&pool->lock, 0u, memory_order_release);
}

static nx_status pool_output_validate(const nx_state_snapshot_pool *pool,
                                      const void *output, size_t output_size,
                                      size_t alignment) {
  size_t slots_bytes = 0u;
  size_t node_count = 0u;
  size_t nodes_bytes = 0u;
  if (output == NULL || ((uintptr_t)output % alignment) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (nx_size_mul((size_t)pool->slot_capacity, sizeof(*pool->slots),
                  &slots_bytes) != NX_STATUS_OK ||
      nx_size_mul((size_t)pool->slot_capacity, (size_t)pool->nodes_per_slot,
                  &node_count) != NX_STATUS_OK ||
      nx_size_mul(node_count, sizeof(*pool->node_storage), &nodes_bytes) !=
          NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(pool, sizeof(*pool), output, output_size) !=
          0 ||
      nx_pointer_ranges_overlap(pool->slots, slots_bytes, output,
                                output_size) != 0 ||
      nx_pointer_ranges_overlap(pool->node_storage, nodes_bytes, output,
                                output_size) != 0)
    return NX_STATUS_INVALID_ARGUMENT;
  return NX_STATUS_OK;
}

static nx_status pool_store_output_validate(const nx_state_store *store,
                                            const void *output,
                                            size_t output_size) {
  size_t slots_bytes = 0u;
  size_t index_bytes = 0u;
  if (store == NULL ||
      ((uintptr_t)store % _Alignof(nx_state_store)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (nx_size_mul((size_t)store->slot_capacity, sizeof(*store->slots),
                  &slots_bytes) != NX_STATUS_OK ||
      nx_size_mul((size_t)store->index_capacity, sizeof(uint32_t),
                  &index_bytes) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(store, sizeof(*store), output, output_size) !=
          0 ||
      nx_pointer_ranges_overlap(store->slots, slots_bytes, output,
                                output_size) != 0 ||
      nx_pointer_ranges_overlap(store->index, index_bytes, output,
                                output_size) != 0 ||
      nx_pointer_ranges_overlap(store->rehash_index, index_bytes, output,
                                output_size) != 0)
    return NX_STATUS_INVALID_ARGUMENT;
  return NX_STATUS_OK;
}

static nx_status pool_acquire_rollback(
    nx_state_snapshot_pool *pool, nx_state_snapshot_pool_slot *slot,
    uint32_t slot_index, nx_status failure) {
  size_t node_offset = 0u;
  size_t node_bytes = 0u;
  if (atomic_load_explicit(&slot->state, memory_order_acquire) !=
      NX_STATE_SNAPSHOT_POOL_RESERVED)
    return failure;
  if (slot_index >= pool->slot_capacity ||
      nx_size_mul((size_t)slot_index, (size_t)pool->nodes_per_slot,
                  &node_offset) != NX_STATUS_OK ||
      nx_size_mul((size_t)pool->nodes_per_slot, sizeof(*pool->node_storage),
                  &node_bytes) != NX_STATUS_OK) {
    atomic_store_explicit(&slot->state, NX_STATE_SNAPSHOT_POOL_RETIRED,
                          memory_order_release);
    return NX_STATUS_CORRUPT;
  }
  memset(&pool->node_storage[node_offset], 0, node_bytes);
  memset(&slot->snapshot, 0, sizeof(slot->snapshot));
  atomic_store_explicit(&slot->state, NX_STATE_SNAPSHOT_POOL_FREE,
                        memory_order_release);
  return failure;
}

static nx_status pool_validate_store_binding(const nx_state_snapshot_pool *pool,
                                             const nx_state_store *store) {
  size_t slots_bytes = 0u;
  size_t index_bytes = 0u;
  size_t nodes_bytes = 0u;
  size_t pool_node_count = 0u;
  size_t pool_nodes_bytes = 0u;
  if (store == NULL ||
      ((uintptr_t)store % _Alignof(nx_state_store)) != 0u ||
      atomic_load_explicit(&store->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (store->store_id != pool->store_id)
    return NX_STATUS_CONFLICT;
  if (nx_size_mul((size_t)pool->slot_capacity, sizeof(*pool->slots),
                  &slots_bytes) != NX_STATUS_OK ||
      nx_size_mul((size_t)store->slot_capacity, sizeof(*store->slots),
                  &nodes_bytes) != NX_STATUS_OK ||
      nx_size_mul((size_t)store->index_capacity, sizeof(uint32_t),
                  &index_bytes) != NX_STATUS_OK ||
      nx_size_mul((size_t)pool->slot_capacity, (size_t)pool->nodes_per_slot,
                  &pool_node_count) != NX_STATUS_OK ||
      nx_size_mul(pool_node_count, sizeof(*pool->node_storage),
                  &pool_nodes_bytes) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(pool, sizeof(*pool), store, sizeof(*store)) !=
          0 ||
      nx_pointer_ranges_overlap(pool, sizeof(*pool), store->slots,
                                nodes_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool, sizeof(*pool), store->index,
                                index_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool, sizeof(*pool), store->rehash_index,
                                index_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->slots, slots_bytes, store,
                                sizeof(*store)) != 0 ||
      nx_pointer_ranges_overlap(pool->slots, slots_bytes, store->slots,
                                nodes_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->slots, slots_bytes, store->index,
                                index_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->slots, slots_bytes, store->rehash_index,
                                index_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->node_storage, pool_nodes_bytes, store,
                                sizeof(*store)) != 0 ||
      nx_pointer_ranges_overlap(pool->node_storage, pool_nodes_bytes,
                                store->slots, nodes_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->node_storage, pool_nodes_bytes,
                                store->index, index_bytes) != 0 ||
      nx_pointer_ranges_overlap(pool->node_storage, pool_nodes_bytes,
                                store->rehash_index, index_bytes) != 0)
    return NX_STATUS_INVALID_ARGUMENT;
  return NX_STATUS_OK;
}

static nx_status pool_init_validate(
    nx_state_snapshot_pool *pool, nx_state_snapshot_pool_slot *slots,
    uint32_t slot_capacity, nx_state_node *node_storage,
    uint32_t nodes_per_slot, uint64_t store_id, uint64_t pool_id,
    size_t *slots_bytes, size_t *nodes_bytes) {
    size_t node_count;
    if (pool == NULL || slots == NULL || slot_capacity == 0u ||
        node_storage == NULL || nodes_per_slot == 0u || store_id == 0u ||
        pool_id == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
        ((uintptr_t)slots % _Alignof(nx_state_snapshot_pool_slot)) != 0u ||
        ((uintptr_t)node_storage % _Alignof(nx_state_node)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_size_mul((size_t)slot_capacity, sizeof(*slots), slots_bytes) !=
            NX_STATUS_OK ||
        nx_size_mul((size_t)slot_capacity, (size_t)nodes_per_slot,
                    &node_count) != NX_STATUS_OK ||
        nx_size_mul(node_count, sizeof(*node_storage), nodes_bytes) !=
            NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    if (nx_pointer_ranges_overlap(pool, sizeof(*pool), slots, *slots_bytes) != 0 ||
        nx_pointer_ranges_overlap(pool, sizeof(*pool), node_storage,
                                  *nodes_bytes) != 0 ||
        nx_pointer_ranges_overlap(slots, *slots_bytes, node_storage,
                                  *nodes_bytes) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status pool_init_publish(
    nx_state_snapshot_pool *pool, nx_state_snapshot_pool_slot *slots,
    uint32_t slot_capacity, nx_state_node *node_storage,
    uint32_t nodes_per_slot, uint64_t store_id, uint64_t pool_id,
    size_t slots_bytes, size_t nodes_bytes) {
    uint32_t index;
    memset(pool, 0, sizeof(*pool));
    memset(slots, 0, slots_bytes);
    memset(node_storage, 0, nodes_bytes);
    pool->slots = slots;
    pool->node_storage = node_storage;
    pool->slot_capacity = slot_capacity;
    pool->nodes_per_slot = nodes_per_slot;
    pool->store_id = store_id;
    pool->pool_id = pool_id;
    atomic_init(&pool->lock, 0u);
    atomic_init(&pool->initialized, 1u);
    if (atomic_is_lock_free(&pool->lock) == 0) {
        atomic_store_explicit(&pool->initialized, 0u, memory_order_release);
        return NX_STATUS_NOT_SUPPORTED;
    }
    for (index = 0u; index < slot_capacity; ++index) {
        atomic_init(&slots[index].state, NX_STATE_SNAPSHOT_POOL_FREE);
        if (atomic_is_lock_free(&slots[index].state) == 0) {
            atomic_store_explicit(&pool->initialized, 0u,
                                  memory_order_release);
            return NX_STATUS_NOT_SUPPORTED;
        }
    }
    return NX_STATUS_OK;
}

static nx_status snapshot_pool_init_impl(nx_state_snapshot_pool *pool,
                                      nx_state_snapshot_pool_slot *slots,
                                      uint32_t slot_capacity,
                                      nx_state_node *node_storage,
                                      uint32_t nodes_per_slot,
                                      uint64_t store_id, uint64_t pool_id) {
    size_t slots_bytes = 0u;
    size_t nodes_bytes = 0u;
    nx_status status = pool_init_validate(
        pool, slots, slot_capacity, node_storage, nodes_per_slot,
        store_id, pool_id, &slots_bytes, &nodes_bytes);
    if (status != NX_STATUS_OK)
        return status;
    return pool_init_publish(pool, slots, slot_capacity, node_storage,
                             nodes_per_slot, store_id, pool_id,
                             slots_bytes, nodes_bytes);
}

static nx_status pool_acquire_reserve_slot(
    nx_state_snapshot_pool *pool, nx_state_store *store,
    uint32_t *slot_index, uint64_t *generation) {
    uint32_t index;
    if (pool->store == NULL)
        pool->store = store;
    else if (pool->store != store)
        return NX_STATUS_CONFLICT;
    for (index = 0u; index < pool->slot_capacity; ++index) {
        nx_state_snapshot_pool_slot *slot = &pool->slots[index];
        uint32_t state = atomic_load_explicit(&slot->state,
                                              memory_order_relaxed);
        if (state == NX_STATE_SNAPSHOT_POOL_FREE) {
            if (slot->generation == UINT64_MAX) {
                atomic_store_explicit(&slot->state,
                                      NX_STATE_SNAPSHOT_POOL_RETIRED,
                                      memory_order_release);
                continue;
            }
            *slot_index = index;
            slot->generation++;
            atomic_store_explicit(&slot->state,
                                  NX_STATE_SNAPSHOT_POOL_RESERVED,
                                  memory_order_release);
            *generation = slot->generation;
            return NX_STATUS_OK;
        }
        if (state > NX_STATE_SNAPSHOT_POOL_RETIRED)
            return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_RESOURCE_EXHAUSTED;
}

static nx_status pool_acquire_capture_reserved(
    nx_state_snapshot_pool *pool, nx_state_store *store,
    uint32_t slot_index, nx_state_snapshot_pool_slot *slot) {
    nx_result result;
    size_t offset;
    nx_state_node *storage;
    nx_status status = nx_size_mul(
        (size_t)slot_index, (size_t)pool->nodes_per_slot, &offset);
    if (status != NX_STATUS_OK)
        return pool_acquire_rollback(pool, slot, slot_index, status);
    storage = &pool->node_storage[offset];
    status = nx_state_store_snapshot_capture(
        store, storage, pool->nodes_per_slot, &slot->snapshot, &result);
    if (status != NX_STATUS_OK)
        return pool_acquire_rollback(pool, slot, slot_index, status);
    if (slot->snapshot.store_id != pool->store_id ||
        atomic_load_explicit(&slot->state, memory_order_acquire) !=
            NX_STATE_SNAPSHOT_POOL_RESERVED)
        return pool_acquire_rollback(pool, slot, slot_index,
                                     NX_STATUS_CORRUPT);
    atomic_store_explicit(&slot->state, NX_STATE_SNAPSHOT_POOL_ACTIVE,
                          memory_order_release);
    return NX_STATUS_OK;
}

static nx_status snapshot_pool_acquire_impl(nx_state_snapshot_pool *pool,
                                         nx_state_store *store,
                                         nx_state_snapshot_lease *lease) {
    uint32_t slot_index = NX_STATE_STORE_NO_SLOT;
    uint64_t generation = 0u;
    nx_state_snapshot_pool_slot *slot;
    nx_status status;
    if (pool == NULL || lease == NULL ||
        ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
        atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = pool_output_validate(pool, lease, sizeof(*lease),
                                  _Alignof(nx_state_snapshot_lease));
    if (status == NX_STATUS_OK)
        status = pool_validate_store_binding(pool, store);
    if (status == NX_STATUS_OK)
        status = pool_store_output_validate(store, lease, sizeof(*lease));
    if (status != NX_STATUS_OK)
        return status;
    memset(lease, 0, sizeof(*lease));
    if (!pool_try_lock(pool))
        return NX_STATUS_BUSY;
    status = pool_acquire_reserve_slot(
        pool, store, &slot_index, &generation);
    pool_unlock(pool);
    if (status != NX_STATUS_OK)
        return status;
    slot = &pool->slots[slot_index];
    status = pool_acquire_capture_reserved(pool, store, slot_index, slot);
    if (status != NX_STATUS_OK)
        return status;
    lease->slot_index = slot_index;
    lease->generation = generation;
    lease->store_id = pool->store_id;
    lease->pool_id = pool->pool_id;
    return NX_STATUS_OK;
}

static nx_status snapshot_pool_view_impl(nx_state_snapshot_pool *pool,
                                      nx_state_snapshot_lease lease,
                                      const nx_state_snapshot **snapshot) {
  nx_state_snapshot_pool_slot *slot;
  if (pool == NULL || snapshot == NULL ||
      ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
      atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u ||
      lease.reserved != 0u || lease.store_id != pool->store_id ||
      lease.pool_id != pool->pool_id ||
      lease.slot_index >= pool->slot_capacity || lease.generation == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (pool_output_validate(pool, snapshot, sizeof(*snapshot),
                           _Alignof(const nx_state_snapshot *)) != NX_STATUS_OK)
    return NX_STATUS_INVALID_ARGUMENT;
  if (!pool_try_lock(pool))
    return NX_STATUS_BUSY;
  if (pool->store == NULL ||
      pool_store_output_validate(pool->store, snapshot, sizeof(*snapshot)) !=
          NX_STATUS_OK) {
    pool_unlock(pool);
    return NX_STATUS_INVALID_ARGUMENT;
  }
  *snapshot = NULL;
  slot = &pool->slots[lease.slot_index];
  if (slot->state != NX_STATE_SNAPSHOT_POOL_ACTIVE ||
      slot->generation != lease.generation) {
    pool_unlock(pool);
    return NX_STATUS_STALE_REFERENCE;
  }
  *snapshot = &slot->snapshot;
  pool_unlock(pool);
  return NX_STATUS_OK;
}

static nx_status snapshot_pool_release_impl(nx_state_snapshot_pool *pool,
                                         nx_state_snapshot_lease *lease) {
  nx_state_snapshot_pool_slot *slot;
  if (pool == NULL || lease == NULL ||
      atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (pool_output_validate(pool, lease, sizeof(*lease),
                           _Alignof(nx_state_snapshot_lease)) != NX_STATUS_OK)
    return NX_STATUS_INVALID_ARGUMENT;
  if (!pool_try_lock(pool))
    return NX_STATUS_BUSY;
  if (pool->store == NULL ||
      pool_store_output_validate(pool->store, lease, sizeof(*lease)) !=
          NX_STATUS_OK) {
    pool_unlock(pool);
    return NX_STATUS_INVALID_ARGUMENT;
  }
  if (lease->reserved != 0u || lease->store_id != pool->store_id ||
      lease->pool_id != pool->pool_id ||
      lease->slot_index >= pool->slot_capacity || lease->generation == 0u) {
    pool_unlock(pool);
    return NX_STATUS_INVALID_ARGUMENT;
  }
  slot = &pool->slots[lease->slot_index];
  if (slot->state != NX_STATE_SNAPSHOT_POOL_ACTIVE ||
      slot->generation != lease->generation) {
    pool_unlock(pool);
    return NX_STATUS_STALE_REFERENCE;
  }
  {
    size_t node_bytes = 0u;
    size_t node_offset =
        (size_t)lease->slot_index * (size_t)pool->nodes_per_slot;
    if (nx_size_mul((size_t)pool->nodes_per_slot, sizeof(*pool->node_storage),
                    &node_bytes) != NX_STATUS_OK) {
      pool_unlock(pool);
      return NX_STATUS_CORRUPT;
    }
    memset(&pool->node_storage[node_offset], 0, node_bytes);
  }
  memset(&slot->snapshot, 0, sizeof(slot->snapshot));
  atomic_store_explicit(&slot->state, NX_STATE_SNAPSHOT_POOL_FREE,
                        memory_order_release);
  memset(lease, 0, sizeof(*lease));
  pool_unlock(pool);
  return NX_STATUS_OK;
}

static nx_status pool_validate_slot(
    const nx_state_snapshot_pool *pool, uint32_t index,
    uint32_t *active, uint32_t *retired) {
    const nx_state_snapshot_pool_slot *slot = &pool->slots[index];
    uint32_t state = atomic_load_explicit(&slot->state, memory_order_acquire);
    if (state == NX_STATE_SNAPSHOT_POOL_ACTIVE) {
        size_t node_offset;
        if (nx_size_mul((size_t)index, (size_t)pool->nodes_per_slot,
                        &node_offset) != NX_STATUS_OK ||
            slot->reserved != 0u || slot->generation == 0u ||
            slot->snapshot.nodes != &pool->node_storage[node_offset] ||
            slot->snapshot.node_count > pool->nodes_per_slot ||
            slot->snapshot.store_id != pool->store_id ||
            nx_state_snapshot_validate(&slot->snapshot) != NX_STATUS_OK)
            return NX_STATUS_CORRUPT;
        (*active)++;
        return NX_STATUS_OK;
    }
    if (state == NX_STATE_SNAPSHOT_POOL_RETIRED) {
        if (slot->generation != UINT64_MAX)
            return NX_STATUS_CORRUPT;
        (*retired)++;
        return NX_STATUS_OK;
    }
    if (state == NX_STATE_SNAPSHOT_POOL_FREE)
        return slot->snapshot.nodes == NULL &&
               slot->snapshot.node_count == 0u &&
               slot->snapshot.store_id == 0u
                   ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    return state == NX_STATE_SNAPSHOT_POOL_RESERVED
               ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status snapshot_pool_validate_impl(nx_state_snapshot_pool *pool,
                                          uint32_t *active_count,
                                          uint32_t *retired_count) {
    uint32_t active = 0u;
    uint32_t retired = 0u;
    uint32_t index;
    nx_status status;
    if (pool == NULL || active_count == NULL || retired_count == NULL ||
        ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
        atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (pool_output_validate(pool, active_count, sizeof(*active_count),
                             _Alignof(uint32_t)) != NX_STATUS_OK ||
        pool_output_validate(pool, retired_count, sizeof(*retired_count),
                             _Alignof(uint32_t)) != NX_STATUS_OK ||
        nx_pointer_ranges_overlap(active_count, sizeof(*active_count),
                                  retired_count, sizeof(*retired_count)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (!pool_try_lock(pool))
        return NX_STATUS_BUSY;
    if (pool->store != NULL &&
        (pool_store_output_validate(pool->store, active_count,
                                    sizeof(*active_count)) != NX_STATUS_OK ||
         pool_store_output_validate(pool->store, retired_count,
                                    sizeof(*retired_count)) != NX_STATUS_OK)) {
        pool_unlock(pool);
        return NX_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < pool->slot_capacity; ++index) {
        status = pool_validate_slot(pool, index, &active, &retired);
        if (status != NX_STATUS_OK) {
            pool_unlock(pool);
            return status;
        }
    }
    pool_unlock(pool);
    *active_count = active;
    *retired_count = retired;
    return NX_STATUS_OK;
}


/* Rule 11 scopes for the snapshot pool. Every reservation is derived from
 * caller-provided pool/store dimensions and checked before any mutation. */
enum snapshot_pool_metric_slot {
  SNAPSHOT_POOL_METRIC_INIT = 0,
  SNAPSHOT_POOL_METRIC_ACQUIRE,
  SNAPSHOT_POOL_METRIC_VIEW,
  SNAPSHOT_POOL_METRIC_RELEASE,
  SNAPSHOT_POOL_METRIC_VALIDATE
};

static nx_status snapshot_pool_scope_begin(
    nx_state_snapshot_pool *pool, uint32_t metric_slot, uint64_t operation_id,
    uint64_t bound, nx_operation_scope *scope) {
  nx_operation_metrics *metrics = NULL;
  nx_status status;
  if (pool != NULL &&
      ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) == 0u &&
      atomic_load_explicit(&pool->initialized, memory_order_acquire) != 0u &&
      metric_slot < NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT)
    metrics = &pool->operation_metrics[metric_slot];
  status = nx_operation_scope_begin(scope, metrics, operation_id, bound,
                                    UINT64_MAX);
  if (status == NX_STATUS_OK) {
    status = nx_operation_scope_consume(scope, bound);
    if (status != NX_STATUS_OK)
      (void)nx_operation_scope_finish(scope, status,
                                      NX_RESULT_DOMAIN_REGISTRY, 0u, 0u, NULL);
  }
  return status;
}

static nx_status snapshot_pool_scope_finish(nx_operation_scope *scope,
                                            nx_status operation_status) {
  return nx_operation_scope_finish(scope, operation_status,
                                   NX_RESULT_DOMAIN_REGISTRY, 0u, 0u, NULL);
}

static nx_status snapshot_pool_bound_add(uint64_t *total, uint64_t value) {
  return nx_u64_add(*total, value, total);
}

nx_status nx_state_snapshot_pool_init(
    nx_state_snapshot_pool *pool, nx_state_snapshot_pool_slot *slots,
    uint32_t slot_capacity, nx_state_node *node_storage,
    uint32_t nodes_per_slot, uint64_t store_id, uint64_t pool_id) {
  nx_operation_scope scope;
  nx_operation_measurement measurement;
  uint64_t slot_bytes;
  uint64_t node_count;
  uint64_t node_bytes;
  uint64_t bound = 16u;
  nx_status status;
  if (nx_u64_mul(slot_capacity, (uint64_t)sizeof(*slots), &slot_bytes) !=
          NX_STATUS_OK ||
      nx_u64_mul(slot_capacity, nodes_per_slot, &node_count) != NX_STATUS_OK ||
      nx_u64_mul(node_count, (uint64_t)sizeof(*node_storage), &node_bytes) !=
          NX_STATUS_OK ||
      snapshot_pool_bound_add(&bound, slot_bytes) != NX_STATUS_OK ||
      snapshot_pool_bound_add(&bound, node_bytes) != NX_STATUS_OK ||
      snapshot_pool_bound_add(&bound, slot_capacity) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  memset(&scope, 0, sizeof(scope));
  status = nx_operation_scope_begin(&scope, NULL, NX_OP_STATE_SNAPSHOT_POOL_INIT,
                                    bound, UINT64_MAX);
  if (status == NX_STATUS_OK)
    status = nx_operation_scope_consume(&scope, bound);
  if (status != NX_STATUS_OK) {
    if (scope.active != 0u)
      (void)nx_operation_scope_finish(&scope, status,
                                      NX_RESULT_DOMAIN_REGISTRY, 0u, 0u, NULL);
    return status;
  }
  status = snapshot_pool_init_impl(pool, slots, slot_capacity, node_storage,
                                   nodes_per_slot, store_id, pool_id);
  if (nx_operation_budget_finish(&scope.budget, &measurement) == NX_STATUS_OK &&
      status == NX_STATUS_OK) {
    static const uint64_t operation_ids[
        NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT] = {
        NX_OP_STATE_SNAPSHOT_POOL_INIT,
        NX_OP_STATE_SNAPSHOT_POOL_ACQUIRE,
        NX_OP_STATE_SNAPSHOT_POOL_VIEW,
        NX_OP_STATE_SNAPSHOT_POOL_RELEASE,
        NX_OP_STATE_SNAPSHOT_POOL_VALIDATE};
    uint32_t index;
    for (index = 0u; index < NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT;
         ++index)
      nx_operation_metrics_init(&pool->operation_metrics[index],
                                operation_ids[index]);
    measurement.bytes_used = slot_bytes + node_bytes;
    nx_operation_metrics_record(
        &pool->operation_metrics[SNAPSHOT_POOL_METRIC_INIT], status,
        &measurement);
  }
  scope.active = 0u;
  return status;
}

nx_status nx_state_snapshot_pool_acquire(nx_state_snapshot_pool *pool,
                                         nx_state_store *store,
                                         nx_state_snapshot_lease *lease) {
  nx_operation_scope scope;
  uint64_t bound = 24u;
  nx_status status;
  if (pool != NULL &&
      ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) == 0u &&
      atomic_load_explicit(&pool->initialized, memory_order_acquire) != 0u) {
    status = nx_u64_add(bound, pool->slot_capacity, &bound);
    if (status == NX_STATUS_OK)
      status = nx_u64_add(bound, pool->nodes_per_slot, &bound);
  } else {
    status = NX_STATUS_OK;
  }
  if (status == NX_STATUS_OK && store != NULL &&
      ((uintptr_t)store % _Alignof(nx_state_store)) == 0u &&
      atomic_load_explicit(&store->initialized, memory_order_acquire) != 0u) {
    uint64_t store_capture_bound;
    status = nx_u64_mul(store->slot_capacity, 65u, &store_capture_bound);
    if (status == NX_STATUS_OK)
      status = nx_u64_add(store_capture_bound, 128u, &store_capture_bound);
    if (status == NX_STATUS_OK)
      status = nx_u64_add(bound, store_capture_bound, &bound);
  }
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_ACQUIRE,
                                     NX_OP_STATE_SNAPSHOT_POOL_ACQUIRE,
                                     bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_acquire_impl(pool, store, lease);
  return snapshot_pool_scope_finish(&scope, status);
}

nx_status nx_state_snapshot_pool_view(nx_state_snapshot_pool *pool,
                                      nx_state_snapshot_lease lease,
                                      const nx_state_snapshot **snapshot) {
  nx_operation_scope scope;
  nx_status status;
  status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_VIEW,
                                     NX_OP_STATE_SNAPSHOT_POOL_VIEW, 32u,
                                     &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_view_impl(pool, lease, snapshot);
  return snapshot_pool_scope_finish(&scope, status);
}

nx_status nx_state_snapshot_pool_release(nx_state_snapshot_pool *pool,
                                         nx_state_snapshot_lease *lease) {
  nx_operation_scope scope;
  uint64_t bound = 24u;
  uint64_t node_bytes;
  nx_status status;
  if (pool == NULL ||
      ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
      atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u) {
    status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_RELEASE,
                                       NX_OP_STATE_SNAPSHOT_POOL_RELEASE,
                                       bound, &scope);
    if (status != NX_STATUS_OK)
      return status;
    status = snapshot_pool_release_impl(pool, lease);
    return snapshot_pool_scope_finish(&scope, status);
  }
  status = nx_u64_mul(pool->nodes_per_slot,
                      (uint64_t)sizeof(*pool->node_storage), &node_bytes);
  if (status == NX_STATUS_OK)
    status = snapshot_pool_bound_add(&bound, node_bytes);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_RELEASE,
                                     NX_OP_STATE_SNAPSHOT_POOL_RELEASE,
                                     bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_release_impl(pool, lease);
  return snapshot_pool_scope_finish(&scope, status);
}

nx_status nx_state_snapshot_pool_validate(nx_state_snapshot_pool *pool,
                                          uint32_t *active_count,
                                          uint32_t *retired_count) {
  nx_operation_scope scope;
  uint64_t per_slot;
  uint64_t bound = 16u;
  nx_status status;
  if (pool == NULL ||
      ((uintptr_t)pool % _Alignof(nx_state_snapshot_pool)) != 0u ||
      atomic_load_explicit(&pool->initialized, memory_order_acquire) == 0u) {
    status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_VALIDATE,
                                       NX_OP_STATE_SNAPSHOT_POOL_VALIDATE,
                                       bound, &scope);
    if (status != NX_STATUS_OK)
      return status;
    status = snapshot_pool_validate_impl(pool, active_count, retired_count);
    return snapshot_pool_scope_finish(&scope, status);
  }
  status = nx_u64_add(pool->nodes_per_slot, 8u, &per_slot);
  if (status == NX_STATUS_OK)
    status = nx_u64_mul(pool->slot_capacity, per_slot, &bound);
  if (status == NX_STATUS_OK)
    status = nx_u64_add(bound, 16u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_scope_begin(pool, SNAPSHOT_POOL_METRIC_VALIDATE,
                                     NX_OP_STATE_SNAPSHOT_POOL_VALIDATE,
                                     bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = snapshot_pool_validate_impl(pool, active_count, retired_count);
  return snapshot_pool_scope_finish(&scope, status);
}
