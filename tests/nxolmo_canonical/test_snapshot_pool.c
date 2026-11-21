/* Product-owned independent NXOLMO regression test port. */
#include "nexs_snapshot_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #expression);                                              \
      return 1;                                                                \
    }                                                                          \
  } while (0)


static int check_pool_metric(const nx_state_snapshot_pool *pool,
                             uint32_t slot, uint64_t operation_id) {
  nx_operation_metrics_snapshot snapshot;
  if (slot >= NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT ||
      nx_operation_metrics_read(&pool->operation_metrics[slot], &snapshot) !=
          NX_STATUS_OK ||
      snapshot.operation_id != operation_id || snapshot.calls == 0u ||
      snapshot.fuel_total == 0u ||
      snapshot.known_elapsed_samples + snapshot.unknown_elapsed_samples !=
          snapshot.calls)
    return 0;
  return 1;
}


static nx_state_node_spec make_spec(uint64_t type_id) {
  nx_state_node_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type_id = type_id;
  spec.schema_id = 1u;
  spec.schema_version = 1u;
  spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
  spec.payload_offset = type_id * UINT64_C(8);
  spec.payload_length = 8u;
  spec.payload_digest.bytes[0] = (uint8_t)type_id;
  return spec;
}

static int init_store(nx_state_store *store, nx_state_slot *slots,
                      uint32_t *index, uint32_t *rehash, nx_result *result) {
  const uint64_t key[2] = {UINT64_C(0x1020304050607080),
                           UINT64_C(0x8877665544332211)};
  return (int)nx_state_store_init(store, slots, 4u, index, rehash, 8u,
                                  UINT64_C(0x534e4150504f4f4c), key, result);
}

static int test_retention_and_reclamation(void) {
  nx_state_store store;
  nx_state_slot store_slots[4];
  uint32_t index[8];
  uint32_t rehash[8];
  nx_state_store other_store;
  nx_state_slot other_slots[4];
  uint32_t other_index[8];
  uint32_t other_rehash[8];
  nx_result other_result;
  nx_result result;
  nx_state_node_spec spec = make_spec(1u);
  nx_object_ref reference;
  nx_state_snapshot_pool pool;
  nx_state_snapshot_pool_slot pool_slots[2];
  nx_state_node node_storage[8];
  nx_state_snapshot_lease first;
  nx_state_snapshot_lease second;
  nx_state_snapshot_lease stale;
  nx_state_snapshot_lease third;
  const nx_state_snapshot *view = NULL;
  uint32_t active = 0u;
  uint32_t retired = 0u;
  uint8_t first_digest[32];

  CHECK(init_store(&store, store_slots, index, rehash, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_store_create(&store, &spec, &reference, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_init(
            &pool, pool_slots, 2u, node_storage, 4u, store.store_id,
            UINT64_C(0x504f4f4c30303031)) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(
            &pool, &store, (nx_state_snapshot_lease *)(void *)&pool) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &first) == NX_STATUS_OK);
  CHECK(init_store(&other_store, other_slots, other_index, other_rehash,
                   &other_result) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &other_store, &third) ==
        NX_STATUS_CONFLICT);
  CHECK(nx_state_snapshot_pool_view(
            &pool, first, (const nx_state_snapshot **)(void *)&store) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_snapshot_pool_release(
            &pool, (nx_state_snapshot_lease *)(void *)&store) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_snapshot_pool_validate(&pool, (uint32_t *)(void *)&store,
                                        &retired) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_store_validate(&store, &result) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_view(&pool, first, &view) == NX_STATUS_OK);
  CHECK(view != NULL && view->node_count == 1u && view->epoch == 1u);
  CHECK(view->nodes[0].revision == 1u);
  memcpy(first_digest, view->root_digest.bytes, sizeof(first_digest));
  stale = first;

  spec = make_spec(2u);
  CHECK(nx_state_store_update(&store, reference, 1u, &spec, NULL, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &second) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_view(&pool, second, &view) == NX_STATUS_OK);
  CHECK(view->epoch == 2u && view->nodes[0].revision == 2u);
  CHECK(nx_state_snapshot_pool_view(&pool, first, &view) == NX_STATUS_OK);
  CHECK(view->epoch == 1u && view->nodes[0].revision == 1u);
  CHECK(memcmp(view->root_digest.bytes, first_digest, sizeof(first_digest)) ==
        0);

  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &third) ==
        NX_STATUS_RESOURCE_EXHAUSTED);
  CHECK(nx_state_snapshot_pool_validate(&pool, &active, &retired) ==
        NX_STATUS_OK);
  CHECK(active == 2u && retired == 0u);

  CHECK(nx_state_snapshot_pool_release(&pool, &first) == NX_STATUS_OK);
  CHECK(first.slot_index == 0u && first.generation == 0u);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &third) == NX_STATUS_OK);
  CHECK(third.slot_index == stale.slot_index);
  CHECK(third.generation > stale.generation);
  CHECK(nx_state_snapshot_pool_view(&pool, stale, &view) ==
        NX_STATUS_STALE_REFERENCE);
  CHECK(view == NULL);
  CHECK(nx_state_snapshot_pool_release(&pool, &stale) ==
        NX_STATUS_STALE_REFERENCE);
  CHECK(nx_state_snapshot_pool_release(&pool, &second) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_release(&pool, &third) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_validate(&pool, &active, &retired) ==
        NX_STATUS_OK);
  CHECK(active == 0u && retired == 0u);
  CHECK(check_pool_metric(&pool, 0u, NX_OP_STATE_SNAPSHOT_POOL_INIT));
  CHECK(check_pool_metric(&pool, 1u, NX_OP_STATE_SNAPSHOT_POOL_ACQUIRE));
  CHECK(check_pool_metric(&pool, 2u, NX_OP_STATE_SNAPSHOT_POOL_VIEW));
  CHECK(check_pool_metric(&pool, 3u, NX_OP_STATE_SNAPSHOT_POOL_RELEASE));
  CHECK(check_pool_metric(&pool, 4u, NX_OP_STATE_SNAPSHOT_POOL_VALIDATE));
  return 0;
}

static int test_generation_retirement_and_pool_identity(void) {
  nx_state_store store;
  nx_state_slot store_slots[4];
  uint32_t index[8];
  uint32_t rehash[8];
  nx_result result;
  nx_state_node_spec spec = make_spec(1u);
  nx_object_ref reference;
  nx_state_snapshot_pool pool;
  nx_state_snapshot_pool_slot pool_slots[1];
  nx_state_node node_storage[2];
  nx_state_snapshot_lease old_lease;
  const nx_state_snapshot *view = NULL;
  uint32_t active = 0u;
  uint32_t retired = 0u;

  CHECK(init_store(&store, store_slots, index, rehash, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_store_create(&store, &spec, &reference, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_init(
            &pool, pool_slots, 1u, node_storage, 2u, store.store_id,
            UINT64_C(0x504f4f4c30303033)) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &old_lease) ==
        NX_STATUS_OK);
  {
    nx_state_snapshot_lease stale_lease = old_lease;
    CHECK(nx_state_snapshot_pool_release(&pool, &old_lease) == NX_STATUS_OK);
    CHECK(nx_state_snapshot_pool_init(
              &pool, pool_slots, 1u, node_storage, 2u, store.store_id,
              UINT64_C(0x504f4f4c30303034)) == NX_STATUS_OK);
    CHECK(nx_state_snapshot_pool_view(&pool, stale_lease, &view) ==
          NX_STATUS_INVALID_ARGUMENT);
  }

  pool_slots[0].generation = UINT64_MAX;
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &old_lease) ==
        NX_STATUS_RESOURCE_EXHAUSTED);
  CHECK(nx_state_snapshot_pool_validate(&pool, &active, &retired) ==
        NX_STATUS_OK);
  CHECK(active == 0u && retired == 1u);
  return 0;
}

static int test_failed_capture_reclaims_reservation(void) {
  nx_state_store store;
  nx_state_slot store_slots[4];
  uint32_t index[8];
  uint32_t rehash[8];
  nx_result result;
  nx_state_node_spec spec = make_spec(1u);
  nx_object_ref reference;
  nx_state_snapshot_pool pool;
  nx_state_snapshot_pool_slot pool_slots[1];
  nx_state_node node_storage[1];
  nx_state_snapshot_lease lease;
  uint32_t active = 0u;
  uint32_t retired = 0u;

  CHECK(init_store(&store, store_slots, index, rehash, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_store_create(&store, &spec, &reference, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_store_create(&store, &spec, &reference, &result) ==
        NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_init(
            &pool, pool_slots, 1u, node_storage, 1u, store.store_id,
            UINT64_C(0x504f4f4c30303031)) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &lease) ==
        NX_STATUS_NO_SPACE);
  CHECK(nx_state_snapshot_pool_validate(&pool, &active, &retired) ==
        NX_STATUS_OK);
  CHECK(active == 0u && retired == 0u);
  CHECK(nx_state_store_delete(&store, reference, 1u, &result) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_acquire(&pool, &store, &lease) == NX_STATUS_OK);
  CHECK(nx_state_snapshot_pool_release(&pool, &lease) == NX_STATUS_OK);
  return 0;
}

int main(void) {
  CHECK(test_retention_and_reclamation() == 0);
  CHECK(test_generation_retirement_and_pool_identity() == 0);
  CHECK(test_failed_capture_reclaims_reservation() == 0);
  (void)puts("registry retained snapshot pool tests: PASS");
  return 0;
}
