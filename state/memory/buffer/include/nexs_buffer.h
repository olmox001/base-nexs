/* Product port of pinned NXOLMO external/references/nxolmo/include/nxolmo/buffer/buffer.h; single NX ABI. */
#ifndef NEXS_BUFFER_H
#define NEXS_BUFFER_H

#include "nexs_result.h"
#include "nexs_checked.h"
#include "nexs_types.h"
#include "nexs_state_store.h"
#include "nexs_operation_metrics.h"
#include "nexs_operation_ids.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_BUFFER_SCHEMA_ID UINT32_C(0x31465542)
#define NX_BUFFER_SCHEMA_VERSION UINT16_C(1)
#define NX_BUFFER_TYPE_ID UINT64_C(0x4e584f4c4d4f4255)
#define NX_BUFFER_NO_SLOT UINT32_MAX
#define NX_BUFFER_RING_RETRY_LIMIT UINT64_C(64)
#define NX_BUFFER_METRIC_COUNT 9u

typedef enum nx_buffer_state {
    NX_BUFFER_STATE_FREE = 0,
    NX_BUFFER_STATE_ACTIVE = 1,
    NX_BUFFER_STATE_SEALED = 2,
    NX_BUFFER_STATE_QUARANTINED = 3,
    NX_BUFFER_STATE_RETIRED = 4
} nx_buffer_state;

enum {
    NX_BUFFER_RIGHT_CREATE = UINT64_C(1) << 0,
    NX_BUFFER_RIGHT_READ = UINT64_C(1) << 1,
    NX_BUFFER_RIGHT_WRITE = UINT64_C(1) << 2,
    NX_BUFFER_RIGHT_MAP = UINT64_C(1) << 3,
    NX_BUFFER_RIGHT_DESTROY = UINT64_C(1) << 4,
    NX_BUFFER_RIGHT_SHARE = UINT64_C(1) << 5,
    NX_BUFFER_RIGHT_DMA = UINT64_C(1) << 6
};

enum {
    NX_BUFFER_FLAG_ZERO_ON_ALLOC = UINT32_C(1) << 0,
    NX_BUFFER_FLAG_IMMUTABLE = UINT32_C(1) << 1,
    NX_BUFFER_FLAG_CONTENT_ADDRESSED = UINT32_C(1) << 2
};

typedef enum nx_buffer_metric {
    NX_BUFFER_METRIC_INIT = 0,
    NX_BUFFER_METRIC_CREATE = 1,
    NX_BUFFER_METRIC_ACQUIRE = 2,
    NX_BUFFER_METRIC_COMMIT = 3,
    NX_BUFFER_METRIC_RELEASE = 4,
    NX_BUFFER_METRIC_VERIFY = 5,
    NX_BUFFER_METRIC_DESTROY = 6,
    NX_BUFFER_METRIC_VALIDATE = 7,
    NX_BUFFER_METRIC_STATS = 8
} nx_buffer_metric;

typedef nx_status (*nx_buffer_authorize_fn)(
    void *context, nx_object_ref principal, nx_object_ref target,
    uint64_t requested_rights, uint64_t authority_epoch);

typedef struct nx_buffer_authority {
    nx_object_ref principal;
    uint64_t epoch;
    nx_buffer_authorize_fn authorize;
    void *context;
} nx_buffer_authority;

typedef struct nx_buffer_slot {
    nx_object_ref reference;
    nx_object_ref owner;
    uint64_t arena_offset;
    uint64_t capacity;
    uint64_t allocation_bytes;
    uint64_t logical_length;
    uint64_t checksum;
    nx_digest256 digest;
    uint64_t lease_count;
    uint64_t pinned_bytes;
    uint64_t validation_lease_count;
    uint64_t validation_pinned_bytes;
    uint32_t validation_writer_count;
    uint64_t allocation_granules;
    uint64_t registry_revision;
    uint32_t state;
    uint32_t flags;
} nx_buffer_slot;

typedef struct nx_buffer_lease_slot {
    nx_object_ref buffer;
    nx_object_ref principal;
    uint64_t lease_id;
    uint64_t offset;
    uint64_t length;
    uint32_t buffer_slot;
    uint32_t rights;
    uint32_t active;
    uint32_t reserved;
} nx_buffer_lease_slot;

typedef struct nx_buffer_manager {
    nx_state_store *registry;
    nx_buffer_slot *slots;
    uint32_t slot_capacity;
    nx_buffer_lease_slot *leases;
    uint32_t lease_capacity;
    uint8_t *arena;
    uint64_t arena_size;
    uint64_t granule_size;
    uint64_t granule_count;
    uint8_t *allocation_bitmap;
    size_t allocation_bitmap_size;
    uint64_t next_lease_id;
    nx_cycle_counter cycle_counter;
    _Atomic uint32_t lock;
    _Atomic uint32_t initialized;
    nx_operation_metrics metrics[NX_BUFFER_METRIC_COUNT];
} nx_buffer_manager;

typedef struct nx_buffer_view {
    nx_object_ref buffer;
    uint64_t lease_id;
    uint64_t offset;
    uint64_t length;
    uint32_t slot_index;
    uint32_t rights;
    uint32_t active;
    uint32_t reserved;
    nx_const_span read_bytes;
    nx_mut_span write_bytes;
} nx_buffer_view;

typedef struct nx_buffer_stats {
    uint64_t arena_bytes;
    uint64_t allocated_bytes;
    uint64_t logical_bytes;
    uint64_t pinned_bytes;
    uint64_t active_buffers;
    uint64_t active_leases;
    uint64_t free_granules;
    uint64_t quarantined_buffers;
} nx_buffer_stats;

typedef enum nx_buffer_message_operation {
    NX_BUFFER_MESSAGE_READ = 1,
    NX_BUFFER_MESSAGE_WRITE = 2,
    NX_BUFFER_MESSAGE_COMMIT = 3,
    NX_BUFFER_MESSAGE_RELEASE = 4,
    NX_BUFFER_MESSAGE_CANCEL = 5
} nx_buffer_message_operation;

typedef struct nx_buffer_message {
    uint64_t request_id;
    uint64_t request_generation;
    nx_object_ref buffer;
    uint64_t offset;
    uint64_t length;
    uint64_t lease_id;
    uint64_t expected_revision;
    uint64_t checksum;
    uint32_t operation;
    uint32_t rights;
    uint32_t flags;
    uint32_t reserved;
} nx_buffer_message;

typedef struct nx_buffer_ring_cell {
    _Atomic uint64_t sequence;
    nx_buffer_message message;
} nx_buffer_ring_cell;

typedef struct nx_buffer_ring {
    nx_buffer_ring_cell *cells;
    uint64_t capacity;
    uint64_t mask;
    _Atomic uint64_t enqueue_position;
    _Atomic uint64_t dequeue_position;
    _Atomic uint32_t initialized;
    nx_cycle_counter cycle_counter;
    nx_operation_metrics init_metrics;
    nx_operation_metrics enqueue_metrics;
    nx_operation_metrics dequeue_metrics;
} nx_buffer_ring;

nx_status nx_buffer_crc64_ecma(nx_const_span bytes, uint64_t *checksum);
nx_status nx_buffer_manager_init(
    nx_buffer_manager *manager, nx_state_store *registry,
    nx_buffer_slot *slots, uint32_t slot_capacity,
    nx_buffer_lease_slot *leases, uint32_t lease_capacity,
    uint8_t *arena, uint64_t arena_size, uint64_t granule_size,
    uint8_t *allocation_bitmap, size_t allocation_bitmap_size,
    nx_result *result);
nx_status nx_buffer_create(nx_buffer_manager *manager,
                           const nx_buffer_authority *authority,
                           uint64_t capacity, uint64_t alignment,
                           uint32_t flags, nx_object_ref *created,
                           nx_result *result);
/* `view` must be zero-initialized and inactive; failed acquire leaves it intact. */
nx_status nx_buffer_acquire(nx_buffer_manager *manager,
                            const nx_buffer_authority *authority,
                            nx_object_ref reference, uint64_t offset,
                            uint64_t length, uint32_t rights,
                            nx_buffer_view *view, nx_result *result);
nx_status nx_buffer_view_commit(nx_buffer_manager *manager,
                                const nx_buffer_authority *authority,
                                nx_buffer_view *view, uint64_t new_length,
                                nx_result *result);
nx_status nx_buffer_view_release(nx_buffer_manager *manager,
                                 const nx_buffer_authority *authority,
                                 nx_buffer_view *view, nx_result *result);
nx_status nx_buffer_view_abort(nx_buffer_manager *manager,
                               const nx_buffer_authority *authority,
                               nx_buffer_view *view, nx_result *result);
nx_status nx_buffer_verify(nx_buffer_manager *manager,
                           const nx_buffer_authority *authority,
                           nx_object_ref reference, nx_result *result);
nx_status nx_buffer_destroy(nx_buffer_manager *manager,
                            const nx_buffer_authority *authority,
                            nx_object_ref reference, nx_result *result);
nx_status nx_buffer_manager_validate(nx_buffer_manager *manager,
                                     nx_result *result);
nx_status nx_buffer_stats_read(nx_buffer_manager *manager,
                                nx_buffer_stats *stats, nx_result *result);
nx_status nx_buffer_metrics_read(const nx_buffer_manager *manager,
                                 nx_buffer_metric metric,
                                 nx_operation_metrics_snapshot *snapshot);

nx_status nx_buffer_message_validate(const nx_buffer_message *message);
nx_status nx_buffer_ring_init(nx_buffer_ring *ring,
                               nx_buffer_ring_cell *cells,
                               uint64_t capacity, nx_result *result);
nx_status nx_buffer_ring_enqueue(nx_buffer_ring *ring,
                                 const nx_buffer_message *message,
                                 nx_result *result);
nx_status nx_buffer_ring_dequeue(nx_buffer_ring *ring,
                                 nx_buffer_message *message,
                                 nx_result *result);

#ifdef __cplusplus
}
#endif

#endif
