/* Canonical NXOLMO state model, source-qualified product port. */
#ifndef NEXS_REGISTRY_SCHEMA_H
#define NEXS_REGISTRY_SCHEMA_H

#include "nexs_state_store.h"
#include "nexs_operation_metrics.h"

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_state_schema_compatibility {
  NX_STATE_SCHEMA_EXACT = 1,
  NX_STATE_SCHEMA_COMPATIBLE = 2,
  NX_STATE_SCHEMA_MIGRATION_REQUIRED = 3
} nx_state_schema_compatibility;

typedef struct nx_state_schema_definition {
  uint64_t type_id;
  uint32_t schema_id;
  uint16_t min_supported_version;
  uint16_t min_compatible_version;
  uint16_t current_version;
  uint16_t reserved16;
  uint32_t allowed_node_flags;
  uint32_t required_node_flags;
  uint64_t max_payload_bytes;
  uint32_t reserved32;
} nx_state_schema_definition;

typedef struct nx_state_schema_migration {
  uint64_t type_id;
  uint32_t schema_id;
  uint16_t from_version;
  uint16_t to_version;
  uint64_t migration_id;
  nx_digest256 program_digest;
  uint64_t fuel_limit;
  uint64_t max_input_bytes;
  uint64_t max_output_bytes;
  uint32_t flags;
  uint32_t reserved;
} nx_state_schema_migration;

enum { NX_STATE_SCHEMA_OPERATION_METRIC_COUNT = 12u };

typedef struct nx_state_schema_registry {
  nx_operation_metrics operation_metrics[NX_STATE_SCHEMA_OPERATION_METRIC_COUNT];
  nx_state_schema_definition *definitions;
  nx_state_schema_migration *migrations;
  uint32_t definition_capacity;
  uint32_t definition_count;
  uint32_t migration_capacity;
  uint32_t migration_count;
  uint64_t registry_id;
  nx_digest256 manifest_digest;
  _Atomic uint32_t initialized;
  _Atomic uint32_t sealed;
} nx_state_schema_registry;

/* Builder operations are single-threaded. After seal succeeds, the registry
 * is versioned; lookup/classification/path resolution are side-effect free.
 * Per-operation metrics are retained in operation_metrics and can be read
 * with nx_operation_metrics_read; the metric slot order is private ABI data. */
nx_status nx_state_schema_registry_init(nx_state_schema_registry *registry,
                                        nx_state_schema_definition *definitions,
                                        uint32_t definition_capacity,
                                        nx_state_schema_migration *migrations,
                                        uint32_t migration_capacity,
                                        uint64_t registry_id);
nx_status nx_state_schema_registry_add_definition(
    nx_state_schema_registry *registry,
    const nx_state_schema_definition *definition);
nx_status nx_state_schema_registry_add_migration(
    nx_state_schema_registry *registry,
    const nx_state_schema_migration *migration);
nx_status nx_state_schema_registry_seal(nx_state_schema_registry *registry);
nx_status
nx_state_schema_registry_validate(const nx_state_schema_registry *registry);
nx_status nx_state_schema_registry_manifest_digest(
    const nx_state_schema_registry *registry, nx_digest256 *digest);
nx_status
nx_state_schema_registry_lookup(const nx_state_schema_registry *registry,
                                uint64_t type_id, uint32_t schema_id,
                                const nx_state_schema_definition **definition);
nx_status
nx_state_schema_registry_classify(const nx_state_schema_registry *registry,
                                  const nx_state_node_spec *spec,
                                  nx_state_schema_compatibility *compatibility);
nx_status nx_state_schema_registry_validate_write(
    const nx_state_schema_registry *registry, const nx_state_node_spec *spec);
/* Explicit schema-gated wrappers. Legacy State Store APIs remain available;
 * callers that require schema enforcement must use these entry points. A
 * schema preflight rejection leaves the store and output storage untouched;
 * output values are defined only when the operation succeeds. */
nx_status nx_state_store_create_with_schema(
    const nx_state_schema_registry *registry, nx_state_store *store,
    const nx_state_node_spec *spec, nx_object_ref *created, nx_result *result);
nx_status nx_state_store_update_with_schema(
    const nx_state_schema_registry *registry, nx_state_store *store,
    nx_object_ref reference, uint64_t expected_revision,
    const nx_state_node_spec *spec, nx_state_node *updated, nx_result *result);
nx_status nx_state_schema_registry_resolve_migration(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, uint16_t from_version,
    const nx_state_schema_migration **path, uint32_t path_capacity,
    uint32_t *path_count);

#ifdef __cplusplus
}
#endif

#endif
