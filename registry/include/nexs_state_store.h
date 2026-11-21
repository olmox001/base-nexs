/* Canonical NXOLMO state model, source-qualified product port. */
#ifndef NEXS_REGISTRY_STATE_STORE_H
#define NEXS_REGISTRY_STATE_STORE_H

#include "nexs_result.h"
#include "nexs_types.h"
#include "nexs_operation_metrics.h"
#include "nexs_operation_ids.h"

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_STATE_STORE_NO_SLOT UINT32_MAX
#define NX_STATE_STORE_INDEX_TOMBSTONE UINT32_MAX
#define NX_STATE_STORE_METRIC_COUNT 11u
#define NX_STATE_STORE_REHASH_BUCKET_BUDGET 4u
#define NX_STATE_STORE_REHASH_IDLE 0u
#define NX_STATE_STORE_REHASH_MIGRATING 1u
#define NX_STATE_STORE_REHASH_CLEANING 2u

typedef enum nx_state_lifecycle {
    NX_STATE_LIFECYCLE_LIVE = 1,
    NX_STATE_LIFECYCLE_FROZEN = 2,
    NX_STATE_LIFECYCLE_QUARANTINED = 3
} nx_state_lifecycle;

enum {
    NX_STATE_NODE_FLAG_IMMUTABLE = UINT32_C(1) << 0,
    NX_STATE_NODE_FLAG_SPARSE = UINT32_C(1) << 1,
    NX_STATE_NODE_FLAG_CONTENT_ADDRESSED = UINT32_C(1) << 2,
    NX_STATE_NODE_FLAG_EXECUTABLE = UINT32_C(1) << 3
};

typedef struct nx_state_node_spec {
    nx_object_ref parent;
    nx_object_ref owner;
    uint64_t type_id;
    uint32_t schema_id;
    uint16_t schema_version;
    uint16_t lifecycle;
    uint32_t flags;
    uint32_t reserved;
    uint64_t payload_offset;
    uint64_t payload_length;
    nx_digest256 payload_digest;
} nx_state_node_spec;

typedef struct nx_state_node {
    nx_object_ref ref;
    uint64_t revision;
    nx_state_node_spec spec;
} nx_state_node;

typedef struct nx_state_slot {
    nx_state_node node;
    uint64_t hash;
    uint64_t generation;
    uint32_t next_free;
    uint32_t state;
    uint32_t validation_mark;
    uint32_t parent_slot;
    uint32_t first_child_slot;
    uint32_t previous_sibling_slot;
    uint32_t next_sibling_slot;
    uint32_t child_count;
    uint32_t reserved;
} nx_state_slot;

typedef enum nx_state_store_metric {
    NX_STATE_STORE_METRIC_INIT = 0,
    NX_STATE_STORE_METRIC_CREATE = 1,
    NX_STATE_STORE_METRIC_READ = 2,
    NX_STATE_STORE_METRIC_UPDATE = 3,
    NX_STATE_STORE_METRIC_DELETE = 4,
    NX_STATE_STORE_METRIC_VALIDATE = 5,
    NX_STATE_STORE_METRIC_STATS = 6,
    NX_STATE_STORE_METRIC_SNAPSHOT = 7,
    NX_STATE_STORE_METRIC_CHILD_FIRST = 8,
    NX_STATE_STORE_METRIC_CHILD_NEXT = 9,
    NX_STATE_STORE_METRIC_CHILD_COUNT = 10
} nx_state_store_metric;

typedef struct nx_state_store {
    nx_state_slot *slots;
    uint32_t slot_capacity;
    uint32_t free_head;
    uint32_t count;
    uint32_t retired_count;
    uint32_t *index;
    uint32_t *rehash_index;
    uint32_t index_capacity;
    uint32_t rehash_cursor;
    uint32_t index_tombstones;
    uint32_t rehash_tombstones;
    uint32_t rehash_state;
    _Atomic uint32_t initialized;
    uint64_t store_id;
    uint64_t next_object_id;
    uint64_t hash_key[2];
    uint64_t epoch;
    nx_cycle_counter cycle_counter;
    _Atomic uint32_t lock;
    nx_operation_metrics metrics[NX_STATE_STORE_METRIC_COUNT];
} nx_state_store;

typedef struct nx_state_store_stats {
    uint64_t epoch;
    uint32_t count;
    uint32_t slot_capacity;
    uint32_t index_capacity;
    uint32_t free_slots;
    uint32_t retired_slots;
    uint32_t index_tombstones;
    uint32_t rehash_tombstones;
    uint32_t rehash_cursor;
    uint32_t rehash_state;
} nx_state_store_stats;

#define NX_STATE_SNAPSHOT_FORMAT_VERSION UINT32_C(1)

typedef struct nx_state_snapshot {
    const nx_state_node *nodes;
    uint64_t store_id;
    uint64_t epoch;
    uint32_t node_count;
    uint32_t format_version;
    nx_digest256 root_digest;
} nx_state_snapshot;

/* Provider-backed growth, preserving every generational identity and revision.
 * Old arrays stay caller-owned; after OK the caller must release all old
 * storage. On failure, neither the store nor new storage ownership changes.
 * Requires a larger capacity and fresh, disjoint caller-owned storage. */
nx_status nx_state_store_grow(nx_state_store *store,
                              nx_state_slot *new_slots,
                              uint32_t new_slot_capacity,
                              uint32_t *new_index,
                              uint32_t *new_rehash_index,
                              uint32_t new_index_capacity);

nx_status nx_state_store_required_index_capacity(uint32_t slot_capacity,
                                                 uint32_t *index_capacity);
nx_status nx_state_node_spec_validate(const nx_state_node_spec *spec);

/* Caller owns all three arrays for the entire lifetime of the store.
 * Both index arrays have index_capacity entries and must not overlap. */
nx_status nx_state_store_init(nx_state_store *store,
                              nx_state_slot *slots,
                              uint32_t slot_capacity,
                              uint32_t *index,
                              uint32_t *rehash_index,
                              uint32_t index_capacity,
                              uint64_t store_id,
                              const uint64_t hash_key[2],
                              nx_result *result);

nx_status nx_state_store_create(nx_state_store *store,
                                const nx_state_node_spec *spec,
                                nx_object_ref *created,
                                nx_result *result);
nx_status nx_state_store_read(nx_state_store *store,
                              nx_object_ref reference,
                              nx_state_node *node,
                              nx_result *result);
/* Parent/child traversal uses canonical object references. The cursor is a
 * copied generation-safe reference; callers never receive slot pointers. */
nx_status nx_state_store_child_first(nx_state_store *store,
                                     nx_object_ref parent,
                                     nx_object_ref *child,
                                     nx_result *result);
nx_status nx_state_store_child_next(nx_state_store *store,
                                    nx_object_ref parent,
                                    nx_object_ref current,
                                    nx_object_ref *next,
                                    nx_result *result);
nx_status nx_state_store_child_count(nx_state_store *store,
                                     nx_object_ref parent,
                                     uint32_t *count,
                                     nx_result *result);

nx_status nx_state_store_update(nx_state_store *store,
                                nx_object_ref reference,
                                uint64_t expected_revision,
                                const nx_state_node_spec *spec,
                                nx_state_node *updated,
                                nx_result *result);
nx_status nx_state_store_delete(nx_state_store *store,
                                nx_object_ref reference,
                                uint64_t expected_revision,
                                nx_result *result);
nx_status nx_state_store_validate(nx_state_store *store, nx_result *result);
nx_status nx_state_store_stats_read(nx_state_store *store,
                                     nx_state_store_stats *stats,
                                     nx_result *result);
/* Captures a detached, caller-owned immutable copy; capacity is checked
 * atomically with the captured node count. The caller retains/releases storage. */
nx_status nx_state_store_snapshot_capture(nx_state_store *store,
                                          nx_state_node *storage,
                                          uint32_t capacity,
                                          nx_state_snapshot *snapshot,
                                          nx_result *result);
nx_status nx_state_snapshot_validate(const nx_state_snapshot *snapshot);
nx_status nx_state_store_metrics_read(const nx_state_store *store,
                                      nx_state_store_metric metric,
                                      nx_operation_metrics_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
