/* Canonical NXOLMO state model, source-qualified product port. */
#ifndef NEXS_REGISTRY_SNAPSHOT_POOL_H
#define NEXS_REGISTRY_SNAPSHOT_POOL_H

#include "nexs_state_store.h"
#include "nexs_operation_metrics.h"

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_STATE_SNAPSHOT_POOL_FREE UINT32_C(0)
#define NX_STATE_SNAPSHOT_POOL_RESERVED UINT32_C(1)
#define NX_STATE_SNAPSHOT_POOL_ACTIVE UINT32_C(2)
#define NX_STATE_SNAPSHOT_POOL_RETIRED UINT32_C(3)

typedef struct nx_state_snapshot_lease {
  uint32_t slot_index;
  uint32_t reserved;
  uint64_t generation;
  uint64_t store_id;
  uint64_t pool_id;
} nx_state_snapshot_lease;

typedef struct nx_state_snapshot_pool_slot {
  nx_state_snapshot snapshot;
  uint64_t generation;
  _Atomic uint32_t state;
  uint32_t reserved;
} nx_state_snapshot_pool_slot;

enum { NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT = 5u };

typedef struct nx_state_snapshot_pool {
  nx_operation_metrics operation_metrics[NX_STATE_SNAPSHOT_POOL_OPERATION_METRIC_COUNT];
  nx_state_snapshot_pool_slot *slots;
  nx_state_node *node_storage;
  uint32_t slot_capacity;
  uint32_t nodes_per_slot;
  uint64_t store_id;
  uint64_t pool_id;
  const nx_state_store *store;
  _Atomic uint32_t lock;
  _Atomic uint32_t initialized;
} nx_state_snapshot_pool;

/* All memory is caller-owned. The bound store must outlive the pool; reinit
 * is permitted only after all leases are released and requires a fresh pool_id.
 * Each lease has one owner: do not copy/share it or release during view access.
 * Per-operation metrics are retained in operation_metrics and readable with
 * nx_operation_metrics_read; their slot order is private ABI data. */
nx_status nx_state_snapshot_pool_init(nx_state_snapshot_pool *pool,
                                      nx_state_snapshot_pool_slot *slots,
                                      uint32_t slot_capacity,
                                      nx_state_node *node_storage,
                                      uint32_t nodes_per_slot,
                                      uint64_t store_id, uint64_t pool_id);
nx_status nx_state_snapshot_pool_acquire(nx_state_snapshot_pool *pool,
                                         nx_state_store *store,
                                         nx_state_snapshot_lease *lease);
nx_status nx_state_snapshot_pool_view(nx_state_snapshot_pool *pool,
                                      nx_state_snapshot_lease lease,
                                      const nx_state_snapshot **snapshot);
nx_status nx_state_snapshot_pool_release(nx_state_snapshot_pool *pool,
                                         nx_state_snapshot_lease *lease);
nx_status nx_state_snapshot_pool_validate(nx_state_snapshot_pool *pool,
                                          uint32_t *active_count,
                                          uint32_t *retired_count);

#ifdef __cplusplus
}
#endif

#endif
