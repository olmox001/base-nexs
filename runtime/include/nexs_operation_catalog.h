/* NXOLMO runtime contract ported to ONE Base-NEXS product ABI. */
#ifndef NEXS_OPERATION_CATALOG_H
#define NEXS_OPERATION_CATALOG_H

#include "nexs_checked.h"
#include "nexs_operation_ids.h"

#include <stddef.h>
#include <stdint.h>

#define NX_OPERATION_CATALOG_VERSION UINT32_C(2)

typedef enum nx_operation_work_model {
    NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED = 1,
    NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY = 2,
    NX_OPERATION_WORK_LINEAR_BYTES = 3,
    NX_OPERATION_WORK_KEYS_AND_TOKEN_BYTES = 4,
    NX_OPERATION_WORK_RESOURCE_RANGES = 5,
    NX_OPERATION_WORK_BLOCKS_AND_BYTES = 6,
    NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS = 7,
    NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS = 8,
    NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES = 9,
    NX_OPERATION_WORK_FRAME_BYTES = 10,
    NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS = 11,
    NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES = 12,
    NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE = 13
} nx_operation_work_model;

enum {
    NX_OPERATION_CONTRACT_EXACT_FORMULA = UINT32_C(1) << 0,
    NX_OPERATION_CONTRACT_METERED = UINT32_C(1) << 1,
    NX_OPERATION_CONTRACT_OPEN = UINT32_C(1) << 2
};

typedef struct nx_operation_work_input {
    uint64_t units;
    uint64_t bytes;
    uint64_t resources;
    uint64_t auxiliary;
} nx_operation_work_input;

typedef struct nx_operation_contract {
    uint64_t operation_id;
    const char *name;
    nx_operation_work_model work_model;
    uint32_t flags;
    uint64_t base_work;
    uint64_t per_unit_work;
    uint64_t per_byte_work;
    uint64_t per_resource_work;
    uint64_t per_auxiliary_work;
} nx_operation_contract;

/* The table is immutable and sorted by operation_id. */
size_t nx_operation_catalog_count(void);
const nx_operation_contract *nx_operation_catalog_at(size_t index);
const nx_operation_contract *nx_operation_catalog_find(uint64_t operation_id);
const nx_operation_contract *nx_operation_catalog_find_name(const char *name);
nx_status nx_operation_cost_evaluate(
    const nx_operation_contract *contract,
    const nx_operation_work_input *input, uint64_t *work);
nx_status nx_operation_cost_evaluate_id(
    uint64_t operation_id, const nx_operation_work_input *input,
    uint64_t *work);
nx_status nx_operation_catalog_validate(void);
size_t nx_operation_catalog_open_count(void);

#endif
