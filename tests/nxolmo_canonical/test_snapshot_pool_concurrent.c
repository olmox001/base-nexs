/* Product-owned independent NXOLMO regression test port. */
#include "nexs_snapshot_pool.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define THREAD_COUNT UINT32_C(4)
#define ITERATIONS UINT32_C(500)

typedef struct worker_context {
  nx_state_snapshot_pool *pool;
  nx_state_store *store;
  _Atomic uint32_t *errors;
  _Atomic uint32_t *completed;
} worker_context;

static void *snapshot_pool_worker(void *argument) {
  worker_context *context = (worker_context *)argument;
  uint32_t iteration;
  for (iteration = 0u; iteration < ITERATIONS; ++iteration) {
    nx_state_snapshot_lease lease;
    const nx_state_snapshot *snapshot = NULL;
    uint32_t attempts = 0u;
    nx_status status;
    do {
      status =
          nx_state_snapshot_pool_acquire(context->pool, context->store, &lease);
      if (status == NX_STATUS_BUSY || status == NX_STATUS_RESOURCE_EXHAUSTED)
        (void)sched_yield();
      attempts++;
    } while (
        (status == NX_STATUS_BUSY || status == NX_STATUS_RESOURCE_EXHAUSTED) &&
        attempts < UINT32_C(100000));
    if (status != NX_STATUS_OK) {
      (void)atomic_fetch_add_explicit(context->errors, 1u,
                                      memory_order_relaxed);
      return NULL;
    }
    attempts = 0u;
    do {
      status = nx_state_snapshot_pool_view(context->pool, lease, &snapshot);
      if (status == NX_STATUS_BUSY)
        (void)sched_yield();
      attempts++;
    } while (status == NX_STATUS_BUSY && attempts < UINT32_C(100000));
    if (status != NX_STATUS_OK || snapshot == NULL ||
        snapshot->node_count != 1u || snapshot->epoch != 1u ||
        nx_state_snapshot_validate(snapshot) != NX_STATUS_OK) {
      (void)atomic_fetch_add_explicit(context->errors, 1u,
                                      memory_order_relaxed);
      return NULL;
    }
    attempts = 0u;
    do {
      status = nx_state_snapshot_pool_release(context->pool, &lease);
      if (status == NX_STATUS_BUSY)
        (void)sched_yield();
      attempts++;
    } while (status == NX_STATUS_BUSY && attempts < UINT32_C(100000));
    if (status != NX_STATUS_OK) {
      (void)atomic_fetch_add_explicit(context->errors, 1u,
                                      memory_order_relaxed);
      return NULL;
    }
    (void)atomic_fetch_add_explicit(context->completed, 1u,
                                    memory_order_relaxed);
  }
  return NULL;
}

int main(void) {
  nx_state_store store;
  nx_state_slot store_slots[4];
  uint32_t index[8];
  uint32_t rehash[8];
  const uint64_t key[2] = {UINT64_C(0x1020304050607080),
                           UINT64_C(0x8877665544332211)};
  nx_state_node_spec spec;
  nx_object_ref reference;
  nx_result result;
  nx_state_snapshot_pool pool;
  nx_state_snapshot_pool_slot pool_slots[8];
  nx_state_node node_storage[32];
  _Atomic uint32_t errors;
  _Atomic uint32_t completed;
  worker_context context;
  pthread_t threads[THREAD_COUNT];
  uint32_t active = 0u;
  uint32_t retired = 0u;
  uint32_t i;

  memset(&spec, 0, sizeof(spec));
  spec.type_id = 1u;
  spec.schema_id = 1u;
  spec.schema_version = 1u;
  spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
  if (nx_state_store_init(&store, store_slots, 4u, index, rehash, 8u,
                          UINT64_C(0x534e4150504f4f4c), key,
                          &result) != NX_STATUS_OK ||
      nx_state_store_create(&store, &spec, &reference, &result) !=
          NX_STATUS_OK ||
      nx_state_snapshot_pool_init(&pool, pool_slots, 8u, node_storage, 4u,
                                  store.store_id,
                                  UINT64_C(0x504f4f4c5453414e)) != NX_STATUS_OK)
    return 1;

  atomic_init(&errors, 0u);
  atomic_init(&completed, 0u);
  context.pool = &pool;
  context.store = &store;
  context.errors = &errors;
  context.completed = &completed;
  for (i = 0u; i < THREAD_COUNT; ++i) {
    if (pthread_create(&threads[i], NULL, snapshot_pool_worker, &context) != 0)
      return 1;
  }
  for (i = 0u; i < THREAD_COUNT; ++i)
    (void)pthread_join(threads[i], NULL);

  if (atomic_load_explicit(&errors, memory_order_relaxed) != 0u ||
      atomic_load_explicit(&completed, memory_order_relaxed) !=
          THREAD_COUNT * ITERATIONS ||
      nx_state_snapshot_pool_validate(&pool, &active, &retired) !=
          NX_STATUS_OK ||
      active != 0u || retired != 0u)
    return 1;
  (void)printf("registry snapshot-pool concurrency: PASS (%u leases)\n",
               atomic_load_explicit(&completed, memory_order_relaxed));
  return 0;
}
