/* NXOLMO runtime source ported in-place against Base-NEXS canonical ABI. */
#include "nexs_operation_catalog.h"

#include "nexs_checked.h"

#include <string.h>

static const nx_operation_contract nx_operation_contracts[] = {
    {UINT64_C(0x00010001), "nx_state_store_init", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010002), "nx_state_store_create", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010003), "nx_state_store_read", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010004), "nx_state_store_update", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010005), "nx_state_store_delete", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010006), "nx_state_store_validate", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010007), "nx_state_store_stats_read", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010008), "nx_state_store_snapshot_capture", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010009), "nx_state_store_child_first", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001000a), "nx_state_store_child_next", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001000b), "nx_state_store_child_count", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001000c), "nx_state_migration_execute", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010100), "nx_state_node_spec_validate", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010101), "nx_state_schema_registry_add_definition", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010102), "nx_state_schema_registry_add_migration", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010103), "nx_state_schema_registry_classify", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010104), "nx_state_schema_registry_init", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010105), "nx_state_schema_registry_lookup", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010106), "nx_state_schema_registry_manifest_digest", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010107), "nx_state_schema_registry_resolve_migration", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010108), "nx_state_schema_registry_seal", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010109), "nx_state_schema_registry_validate", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010a), "nx_state_schema_registry_validate_write", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010b), "nx_state_snapshot_pool_acquire", NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010c), "nx_state_snapshot_pool_init", NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010d), "nx_state_snapshot_pool_release", NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010e), "nx_state_snapshot_pool_validate", NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0001010f), "nx_state_snapshot_pool_view", NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010110), "nx_state_snapshot_validate", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010111), "nx_state_store_create_with_schema", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010112), "nx_state_store_metrics_read", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010113), "nx_state_store_required_index_capacity", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 1u, 0u, 0u, 0u},
    {UINT64_C(0x00010114), "nx_state_store_test_clear_faults", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010115), "nx_state_store_test_fail_rehash_after", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00010116), "nx_state_store_update_with_schema", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020001), "nx_buffer_manager_init", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020002), "nx_buffer_create", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020003), "nx_buffer_acquire", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020004), "nx_buffer_view_commit", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020005), "nx_buffer_view_release", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020006), "nx_buffer_verify", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020007), "nx_buffer_destroy", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020008), "nx_buffer_manager_validate", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020009), "nx_buffer_stats_read", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020101), "nx_buffer_ring_init", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020102), "nx_buffer_ring_enqueue", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020103), "nx_buffer_ring_dequeue", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020201), "nx_buffer_capability_authorize", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020202), "nx_buffer_crc64_ecma", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 1u, 0u, 0u},
    {UINT64_C(0x00020203), "nx_buffer_message_validate", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020204), "nx_buffer_metrics_read", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00020205), "nx_buffer_view_abort", NX_OPERATION_WORK_ARENA_GRANULES_AND_SLOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030001), "nx_const_span_validate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030002), "nx_load_be16", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030003), "nx_load_be32", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030004), "nx_load_be64", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030005), "nx_load_le16", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030006), "nx_load_le32", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030007), "nx_load_le64", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030008), "nx_mut_span_validate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030009), "nx_object_id_equal", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000a), "nx_object_id_is_zero", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000b), "nx_object_ref_equal", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 3u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000c), "nx_object_ref_is_valid", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 3u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000d), "nx_pointer_ranges_overlap", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 5u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000e), "nx_range_validate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 3u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003000f), "nx_result_init", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 10u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030010), "nx_result_validate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 10u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030011), "nx_size_add", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030012), "nx_size_mul", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030013), "nx_store_be16", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030014), "nx_store_be32", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030015), "nx_store_be64", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030016), "nx_store_le16", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030017), "nx_store_le32", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030018), "nx_store_le64", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00030019), "nx_u64_add", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0003001a), "nx_u64_mul", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040001), "nx_cycle_counter_init", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040002), "nx_cycle_counter_read", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040003), "nx_monotonic_clock_resolution_ns", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040004), "nx_monotonic_time_ns", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040005), "nx_operation_budget_consume", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 3u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040006), "nx_operation_budget_finish", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 8u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040007), "nx_operation_budget_init", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040008), "nx_operation_budget_init_with_counter", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 14u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040009), "nx_operation_budget_poll", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0004000a), "nx_operation_catalog_at", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0004000b), "nx_operation_catalog_count", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0004000c), "nx_operation_catalog_find", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 1u, 0u, 0u, 0u},
    {UINT64_C(0x0004000d), "nx_operation_catalog_find_name", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 1u, 1u, 0u},
    {UINT64_C(0x0004000e), "nx_operation_catalog_open_count", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 1u, 0u},
    {UINT64_C(0x0004000f), "nx_operation_catalog_validate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 0u, 8u, 0u},
    {UINT64_C(0x00040010), "nx_operation_cost_evaluate", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 32u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040011), "nx_operation_cost_evaluate_id", NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED, NX_OPERATION_CONTRACT_EXACT_FORMULA, 40u, 1u, 0u, 0u, 0u},
    {UINT64_C(0x00040012), "nx_operation_metrics_init", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040013), "nx_operation_metrics_read", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 18u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040014), "nx_operation_metrics_record", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1024u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040015), "nx_operation_scope_begin", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 10u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040016), "nx_operation_scope_begin_with_counter", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040017), "nx_operation_scope_consume", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040018), "nx_operation_scope_finish", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 10u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00040019), "nx_operation_scope_finish_ex", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0004001a), "nx_operation_scope_poll", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_EXACT_FORMULA, 3u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00050001), "nx_crc32c", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 1u, 0u, 0u},
    {UINT64_C(0x00050002), "nx_crc64_ecma", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 1u, 0u, 0u},
    {UINT64_C(0x00050003), "nx_crc64_ecma_bitwise", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 2u, 0u, 8u, 0u, 0u},
    {UINT64_C(0x00050004), "nx_crypto_constant_time_equal", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 1u, 0u, 1u, 0u, 0u},
    {UINT64_C(0x00050005), "nx_hmac_sha256", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 256u, 0u, 1u, 0u, 128u},
    {UINT64_C(0x00050006), "nx_sha256_compute", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 144u, 0u, 1u, 0u, 64u},
    {UINT64_C(0x00050007), "nx_sha256_final", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 128u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00050008), "nx_sha256_init", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00050009), "nx_sha256_update", NX_OPERATION_WORK_LINEAR_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 64u, 0u, 1u, 0u, 64u},
    {UINT64_C(0x00060001), "nx_capability_attenuate", NX_OPERATION_WORK_KEYS_AND_TOKEN_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00060002), "nx_capability_issue", NX_OPERATION_WORK_KEYS_AND_TOKEN_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00060003), "nx_capability_keyring_validate", NX_OPERATION_WORK_KEYS_AND_TOKEN_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00060004), "nx_capability_verify", NX_OPERATION_WORK_KEYS_AND_TOKEN_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00070001), "nx_hardware_dma_validate", NX_OPERATION_WORK_RESOURCE_RANGES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00070002), "nx_hardware_grant_from_token", NX_OPERATION_WORK_RESOURCE_RANGES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00070003), "nx_hardware_grant_validate", NX_OPERATION_WORK_RESOURCE_RANGES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00070004), "nx_hardware_range_validate", NX_OPERATION_WORK_RESOURCE_RANGES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00070005), "nx_hardware_resource_validate", NX_OPERATION_WORK_RESOURCE_RANGES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080001), "nx_block_device_flush", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080002), "nx_block_device_read", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080003), "nx_block_device_validate", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080004), "nx_block_device_write", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080005), "nx_block_memory_crash", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 0u, 1u, 1u, 0u},
    {UINT64_C(0x00080006), "nx_block_memory_device_fail_operation", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080007), "nx_block_memory_device_init", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 1u, 0u, 0u, 0u},
    {UINT64_C(0x00080008), "nx_block_memory_fail_next", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00080009), "nx_block_memory_init", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 0u, 2u, 1u, 0u},
    {UINT64_C(0x0008000a), "nx_block_memory_poll", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 1u, 0u, 1u, 0u},
    {UINT64_C(0x0008000b), "nx_block_memory_pump", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 1u, 1u, 1u, 0u},
    {UINT64_C(0x0008000c), "nx_block_memory_submit", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 12u, 1u, 0u, 1u, 0u},
    {UINT64_C(0x0008000d), "nx_block_memory_validate", NX_OPERATION_WORK_BLOCKS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 16u, 1u, 0u, 1u, 0u},
    {UINT64_C(0x00090001), "nx_journal_abort", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090002), "nx_journal_begin", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090003), "nx_journal_commit", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090004), "nx_journal_format", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090005), "nx_journal_init", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090006), "nx_journal_mount", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090007), "nx_journal_recover", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090008), "nx_journal_root_read", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x00090009), "nx_journal_validate", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x0009000a), "nx_journal_write", NX_OPERATION_WORK_LOG_RECORDS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0001), "nx_ext4_bitmap_assign", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0002), "nx_ext4_bitmap_checksum_compute", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0003), "nx_ext4_bitmap_initialize", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0004), "nx_ext4_bitmap_test", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0005), "nx_ext4_block_bitmap_read", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0006), "nx_ext4_directory_block_validate", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0007), "nx_ext4_directory_entry_next", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0008), "nx_ext4_directory_lookup", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0009), "nx_ext4_directory_lookup_workspace_required", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000a), "nx_ext4_group_descriptor_table_read", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000b), "nx_ext4_htree_hash_name", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000c), "nx_ext4_htree_index_entry_at", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000d), "nx_ext4_htree_index_select_range", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000e), "nx_ext4_htree_index_validate", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a000f), "nx_ext4_inode_bitmap_read", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0010), "nx_ext4_inode_checksum_compute", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0011), "nx_ext4_inode_read", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0012), "nx_ext4_map_block", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0013), "nx_ext4_superblock_decode", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0014), "nx_ext4_superblock_read", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000a0015), "nx_ext4_superblock_validate_volume", NX_OPERATION_WORK_IMAGE_GROUPS_AND_RECORDS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000b0001), "nx_9p_frame_decode", NX_OPERATION_WORK_FRAME_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000b0002), "nx_9p_frame_encode", NX_OPERATION_WORK_FRAME_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 7u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0001), "nx_vm_allocate", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0002), "nx_vm_config_validate", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 128u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0003), "nx_vm_driver_init", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0004), "nx_vm_driver_validate", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0005), "nx_vm_fetch", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0006), "nx_vm_free", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0007), "nx_vm_get_stats", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0008), "nx_vm_list_mappings", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0009), "nx_vm_page_query", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000a), "nx_vm_pointer_add", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000b), "nx_vm_pointer_difference", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_EXACT_FORMULA, 4u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000c), "nx_vm_pointer_fetch", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000d), "nx_vm_pointer_make", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000e), "nx_vm_pointer_read", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c000f), "nx_vm_pointer_write", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0010), "nx_vm_protect", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0011), "nx_vm_read", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0012), "nx_vm_snapshot", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0013), "nx_vm_verify", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000c0014), "nx_vm_write", NX_OPERATION_WORK_PAGES_MAPPINGS_AND_BYTES, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0001), "nxreg_client_object_read", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0002), "nxreg_client_child_first", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0003), "nxreg_client_child_next", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0004), "nxreg_client_child_count", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0005), "nxreg_api_validate", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0006), "nxreg_client_bind", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0007), "nxreg_state_store_adapter_init", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0008), "nxreg_dispatch", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d0009), "nxreg_opcode_encode", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d000a), "nxreg_opcode_decode", NX_OPERATION_WORK_FIXED_OR_BOUNDED_RETRY, NX_OPERATION_CONTRACT_OPEN, 0u, 0u, 0u, 0u, 0u},
    {UINT64_C(0x000d000b), "nxreg_client_parent_read", NX_OPERATION_WORK_STORE_INDEX_CHILDREN_AND_SNAPSHOTS, NX_OPERATION_CONTRACT_METERED, 0u, 0u, 0u, 0u, 0u},
};

size_t nx_operation_catalog_count(void) {
    return sizeof(nx_operation_contracts) / sizeof(nx_operation_contracts[0]);
}

const nx_operation_contract *nx_operation_catalog_at(size_t index) {
    return index < nx_operation_catalog_count()
        ? &nx_operation_contracts[index] : NULL;
}

const nx_operation_contract *nx_operation_catalog_find(uint64_t operation_id) {
    size_t lower = 0u;
    size_t upper = nx_operation_catalog_count();
    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2u);
        uint64_t candidate = nx_operation_contracts[middle].operation_id;
        if (candidate < operation_id)
            lower = middle + 1u;
        else
            upper = middle;
    }
    if (lower < nx_operation_catalog_count() &&
        nx_operation_contracts[lower].operation_id == operation_id)
        return &nx_operation_contracts[lower];
    return NULL;
}

const nx_operation_contract *nx_operation_catalog_find_name(const char *name) {
    size_t index;
    if (name == NULL)
        return NULL;
    for (index = 0u; index < nx_operation_catalog_count(); ++index)
        if (strcmp(nx_operation_contracts[index].name, name) == 0)
            return &nx_operation_contracts[index];
    return NULL;
}

nx_status nx_operation_cost_evaluate(
    const nx_operation_contract *contract,
    const nx_operation_work_input *input, uint64_t *work) {
    uint64_t total, term;
    nx_status status;
    if (contract == NULL || input == NULL || work == NULL ||
        ((uintptr_t)contract % (uintptr_t)_Alignof(nx_operation_contract)) != 0u ||
        ((uintptr_t)input % (uintptr_t)_Alignof(nx_operation_work_input)) != 0u ||
        ((uintptr_t)work % (uintptr_t)_Alignof(uint64_t)) != 0u ||
        nx_pointer_ranges_overlap(contract, sizeof(*contract), input, sizeof(*input)) != 0 ||
        nx_pointer_ranges_overlap(contract, sizeof(*contract), work, sizeof(*work)) != 0 ||
        nx_pointer_ranges_overlap(input, sizeof(*input), work, sizeof(*work)) != 0) {
        return NX_STATUS_INVALID_ARGUMENT;
    }
    *work = 0u;
    if (nx_operation_catalog_find(contract->operation_id) != contract)
        return NX_STATUS_INVALID_ARGUMENT;
    if (contract->flags != NX_OPERATION_CONTRACT_EXACT_FORMULA)
        return NX_STATUS_NOT_SUPPORTED;
    total = contract->base_work;
    status = nx_u64_mul(input->units, contract->per_unit_work, &term);
    if (status == NX_STATUS_OK) status = nx_u64_add(total, term, &total);
    if (status == NX_STATUS_OK) status = nx_u64_mul(input->bytes, contract->per_byte_work, &term);
    if (status == NX_STATUS_OK) status = nx_u64_add(total, term, &total);
    if (status == NX_STATUS_OK) status = nx_u64_mul(input->resources, contract->per_resource_work, &term);
    if (status == NX_STATUS_OK) status = nx_u64_add(total, term, &total);
    if (status == NX_STATUS_OK) status = nx_u64_mul(input->auxiliary, contract->per_auxiliary_work, &term);
    if (status == NX_STATUS_OK) status = nx_u64_add(total, term, &total);
    if (status == NX_STATUS_OK) *work = total;
    return status;
}

nx_status nx_operation_cost_evaluate_id(
    uint64_t operation_id, const nx_operation_work_input *input,
    uint64_t *work) {
    const nx_operation_contract *contract = nx_operation_catalog_find(operation_id);
    if (contract == NULL) return NX_STATUS_NOT_FOUND;
    return nx_operation_cost_evaluate(contract, input, work);
}

nx_status nx_operation_catalog_validate(void) {
    size_t index;
    for (index = 0u; index < nx_operation_catalog_count(); ++index) {
        const nx_operation_contract *entry = &nx_operation_contracts[index];
        if (entry->operation_id == 0u ||
            (entry->operation_id >> 32u) != 0u ||
            ((entry->operation_id >> 16u) & UINT64_C(0xffff)) == 0u ||
            (entry->operation_id & UINT64_C(0xffff)) == 0u ||
            entry->name == NULL ||
            entry->name[0] == '\0' ||
            entry->work_model < NX_OPERATION_WORK_EXACT_OR_INPUT_DERIVED ||
            entry->work_model > NX_OPERATION_WORK_POOL_SLOTS_NODES_AND_SNAPSHOT_CAPTURE ||
            (entry->flags & ~(uint32_t)(NX_OPERATION_CONTRACT_EXACT_FORMULA |
                                        NX_OPERATION_CONTRACT_METERED |
                                        NX_OPERATION_CONTRACT_OPEN)) != 0u ||
            (entry->flags != NX_OPERATION_CONTRACT_EXACT_FORMULA &&
             entry->flags != NX_OPERATION_CONTRACT_METERED &&
             entry->flags != NX_OPERATION_CONTRACT_OPEN) ||
            ((entry->flags == NX_OPERATION_CONTRACT_EXACT_FORMULA) &&
             entry->base_work == 0u && entry->per_unit_work == 0u &&
             entry->per_byte_work == 0u && entry->per_resource_work == 0u &&
             entry->per_auxiliary_work == 0u) ||
            ((entry->flags != NX_OPERATION_CONTRACT_EXACT_FORMULA) &&
             (entry->base_work != 0u || entry->per_unit_work != 0u ||
              entry->per_byte_work != 0u || entry->per_resource_work != 0u ||
              entry->per_auxiliary_work != 0u)) ||
            (index != 0u && nx_operation_contracts[index - 1u].operation_id >=
                              entry->operation_id))
            return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_OK;
}

size_t nx_operation_catalog_open_count(void) {
    size_t index;
    size_t count = 0u;
    for (index = 0u; index < nx_operation_catalog_count(); ++index)
        if ((nx_operation_contracts[index].flags & NX_OPERATION_CONTRACT_OPEN) != 0u)
            count++;
    return count;
}
