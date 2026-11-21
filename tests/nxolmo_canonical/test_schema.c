/* Product-owned independent NXOLMO regression test port. */
#include "nexs_schema.h"

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


static int check_schema_metric(const nx_state_schema_registry *registry,
                               uint32_t slot, uint64_t operation_id) {
  nx_operation_metrics_snapshot snapshot;
  if (slot >= NX_STATE_SCHEMA_OPERATION_METRIC_COUNT ||
      nx_operation_metrics_read(&registry->operation_metrics[slot],
                                &snapshot) != NX_STATUS_OK ||
      snapshot.operation_id != operation_id || snapshot.calls == 0u ||
      snapshot.fuel_total == 0u ||
      snapshot.unknown_elapsed_samples > snapshot.calls ||
      snapshot.known_elapsed_samples + snapshot.unknown_elapsed_samples !=
          snapshot.calls)
    return 0;
  return 1;
}

static nx_state_schema_definition definition_a(void) {
  nx_state_schema_definition definition;
  memset(&definition, 0, sizeof(definition));
  definition.type_id = 100u;
  definition.schema_id = 1u;
  definition.min_supported_version = 1u;
  definition.min_compatible_version = 3u;
  definition.current_version = 5u;
  definition.allowed_node_flags =
      NX_STATE_NODE_FLAG_IMMUTABLE | NX_STATE_NODE_FLAG_SPARSE;
  definition.required_node_flags = NX_STATE_NODE_FLAG_IMMUTABLE;
  definition.max_payload_bytes = 4096u;
  return definition;
}

static nx_state_schema_migration migration(uint16_t from, uint16_t to,
                                           uint64_t id) {
  nx_state_schema_migration value;
  memset(&value, 0, sizeof(value));
  value.type_id = 100u;
  value.schema_id = 1u;
  value.from_version = from;
  value.to_version = to;
  value.migration_id = id;
  value.program_digest.bytes[0] = (uint8_t)(id >> 8u);
  value.program_digest.bytes[31] = (uint8_t)id;
  value.fuel_limit = 1000u;
  value.max_input_bytes = 4096u;
  value.max_output_bytes = 4096u;
  return value;
}

static nx_state_node_spec node_spec(uint16_t version) {
  nx_state_node_spec spec;
  memset(&spec, 0, sizeof(spec));
  spec.type_id = 100u;
  spec.schema_id = 1u;
  spec.schema_version = version;
  spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
  spec.flags = NX_STATE_NODE_FLAG_IMMUTABLE;
  spec.payload_length = 32u;
  return spec;
}

static int test_catalog_and_migration_paths(void) {
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[4];
  nx_state_schema_migration migrations[4];
  nx_state_schema_registry reordered_registry;
  nx_state_schema_definition reordered_definitions[4];
  nx_state_schema_migration reordered_migrations[4];
  nx_digest256 manifest_digest;
  nx_digest256 reordered_digest;
  static const uint8_t expected_manifest[32] = {
      0xfbu, 0xb3u, 0x8au, 0xbdu, 0x8cu, 0x37u, 0x2au, 0xcfu,
      0x93u, 0x36u, 0x50u, 0x98u, 0xd6u, 0xc5u, 0xa4u, 0x34u,
      0x90u, 0xb0u, 0x5du, 0xe7u, 0x74u, 0xafu, 0x12u, 0x2cu,
      0x62u, 0x7fu, 0x0fu, 0xebu, 0x6cu, 0x99u, 0x50u, 0x0bu};
  nx_state_schema_definition definition = definition_a();
  nx_state_schema_definition second = definition_a();
  nx_state_schema_migration first = migration(1u, 2u, 1001u);
  nx_state_schema_migration second_edge = migration(2u, 5u, 1002u);
  const nx_state_schema_definition *found = NULL;
  const nx_state_schema_migration *path[2] = {NULL, NULL};
  nx_state_node_spec spec;
  nx_state_schema_compatibility compatibility = 0;
  uint32_t path_count = 0u;

  second.type_id = 200u;
  second.schema_id = 2u;
  second.min_supported_version = 1u;
  second.min_compatible_version = 1u;
  second.current_version = 1u;
  second.allowed_node_flags = 0u;
  second.required_node_flags = 0u;

  CHECK(nx_state_schema_registry_init(&registry, definitions, 4u, migrations,
                                      4u, UINT64_C(0x534348454d413031)) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &second) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_manifest_digest(&registry, &manifest_digest) ==
        NX_STATUS_UNAVAILABLE);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_CONFLICT);
  CHECK(nx_state_schema_registry_add_migration(
            &registry, &(nx_state_schema_migration){.type_id = 999u,
                                                    .schema_id = 1u,
                                                    .from_version = 1u,
                                                    .to_version = 2u,
                                                    .migration_id = 1u,
                                                    .fuel_limit = 1u,
                                                    .max_input_bytes = 1u,
                                                    .max_output_bytes = 1u}) ==
        NX_STATUS_NOT_FOUND);
  CHECK(nx_state_schema_registry_add_migration(&registry, &second_edge) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_migration(&registry, &first) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_migration(&registry, &first) ==
        NX_STATUS_CONFLICT);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_validate(&registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_manifest_digest(&registry, &manifest_digest) ==
        NX_STATUS_OK);
  CHECK(memcmp(manifest_digest.bytes, expected_manifest,
               sizeof(expected_manifest)) == 0);
  CHECK(nx_state_schema_registry_manifest_digest(
            &registry, (nx_digest256 *)(void *)&registry) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_schema_registry_init(
            &reordered_registry, reordered_definitions, 4u,
            reordered_migrations, 4u,
            UINT64_C(0x534348454d413099)) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&reordered_registry,
                                                &definition) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&reordered_registry, &second) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_migration(&reordered_registry, &first) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_migration(&reordered_registry,
                                               &second_edge) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_seal(&reordered_registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_manifest_digest(
            &reordered_registry, &reordered_digest) == NX_STATUS_OK);
  CHECK(memcmp(manifest_digest.bytes, reordered_digest.bytes,
               sizeof(manifest_digest.bytes)) == 0);
  reordered_definitions[0].max_payload_bytes++;
  CHECK(nx_state_schema_registry_validate(&reordered_registry) ==
        NX_STATUS_CORRUPT);
  reordered_definitions[0].max_payload_bytes--;
  CHECK(nx_state_schema_registry_validate(&reordered_registry) == NX_STATUS_OK);
  reordered_migrations[0].program_digest.bytes[0] ^= 1u;
  CHECK(nx_state_schema_registry_validate(&reordered_registry) ==
        NX_STATUS_CORRUPT);
  reordered_migrations[0].program_digest.bytes[0] ^= 1u;
  CHECK(nx_state_schema_registry_validate(&reordered_registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_ACCESS_DENIED);

  CHECK(nx_state_schema_registry_lookup(&registry, 100u, 1u, &found) ==
        NX_STATUS_OK);
  CHECK(found != NULL && found->current_version == 5u);
  CHECK(nx_state_schema_registry_lookup(&registry, 999u, 1u, &found) ==
        NX_STATUS_NOT_FOUND);
  CHECK(found == NULL);

  spec = node_spec(5u);
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_OK);
  CHECK(compatibility == NX_STATE_SCHEMA_EXACT);
  CHECK(nx_state_schema_registry_validate_write(&registry, &spec) ==
        NX_STATUS_OK);

  spec.schema_version = 4u;
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_OK);
  CHECK(compatibility == NX_STATE_SCHEMA_COMPATIBLE);
  CHECK(nx_state_schema_registry_validate_write(&registry, &spec) ==
        NX_STATUS_VERSION_MISMATCH);

  spec.schema_version = 1u;
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_OK);
  CHECK(compatibility == NX_STATE_SCHEMA_MIGRATION_REQUIRED);
  CHECK(nx_state_schema_registry_validate_write(&registry, &spec) ==
        NX_STATUS_VERSION_MISMATCH);
  CHECK(nx_state_schema_registry_resolve_migration(&registry, 100u, 1u, 1u,
                                                   path, 1u, &path_count) ==
        NX_STATUS_NO_SPACE);
  CHECK(path_count == 2u);
  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, 100u, 1u, 1u, path, 2u, &path_count) == NX_STATUS_OK);
  CHECK(path_count == 2u && path[0]->migration_id == 1001u &&
        path[1]->migration_id == 1002u);
  CHECK(path[0]->from_version == 1u && path[0]->to_version == 2u);
  CHECK(path[1]->from_version == 2u && path[1]->to_version == 5u);

  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, 100u, 1u, 4u, path, 2u, &path_count) == NX_STATUS_OK);
  CHECK(path_count == 0u);
  CHECK(nx_state_schema_registry_resolve_migration(&registry, 100u, 1u, 0u,
                                                   path, 2u, &path_count) ==
        NX_STATUS_VERSION_MISMATCH);
  CHECK(nx_state_schema_registry_resolve_migration(&registry, 999u, 1u, 1u,
                                                   path, 2u, &path_count) ==
        NX_STATUS_NOT_FOUND);

  spec = node_spec(5u);
  spec.payload_length = 4097u;
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_LIMIT_EXCEEDED);
  spec = node_spec(5u);
  spec.flags |= NX_STATE_NODE_FLAG_EXECUTABLE;
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_NOT_SUPPORTED);
  spec = node_spec(6u);
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_VERSION_MISMATCH);
  CHECK(nx_state_schema_registry_lookup(
            &registry, 100u, 1u,
            (const nx_state_schema_definition **)(void *)&registry) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_schema_registry_classify(
            &registry, &spec,
            (nx_state_schema_compatibility *)(void *)&registry) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, 100u, 1u, 1u,
            (const nx_state_schema_migration **)(void *)&registry, 2u,
            &path_count) == NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, 100u, 1u, 1u, path, 2u, (uint32_t *)(void *)&registry) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_schema_registry_validate(&registry) == NX_STATUS_OK);
  CHECK(check_schema_metric(&registry, 0u, NX_OP_STATE_SCHEMA_REGISTRY_INIT));
  CHECK(check_schema_metric(&registry, 1u,
                            NX_OP_STATE_SCHEMA_REGISTRY_ADD_DEFINITION));
  CHECK(check_schema_metric(&registry, 2u,
                            NX_OP_STATE_SCHEMA_REGISTRY_ADD_MIGRATION));
  CHECK(check_schema_metric(&registry, 3u, NX_OP_STATE_SCHEMA_REGISTRY_SEAL));
  CHECK(check_schema_metric(&registry, 4u,
                            NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE));
  CHECK(check_schema_metric(&registry, 5u,
                            NX_OP_STATE_SCHEMA_REGISTRY_MANIFEST_DIGEST));
  CHECK(check_schema_metric(&registry, 6u,
                            NX_OP_STATE_SCHEMA_REGISTRY_LOOKUP));
  CHECK(check_schema_metric(&registry, 7u,
                            NX_OP_STATE_SCHEMA_REGISTRY_CLASSIFY));
  CHECK(check_schema_metric(&registry, 8u,
                            NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE_WRITE));
  CHECK(check_schema_metric(&registry, 11u,
                            NX_OP_STATE_SCHEMA_REGISTRY_RESOLVE_MIGRATION));
  return 0;
}

static int test_missing_migration_rejected_at_seal(void) {
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_schema_migration migrations[1];
  nx_state_schema_definition definition = definition_a();

  CHECK(nx_state_schema_registry_init(&registry, definitions, 1u, migrations,
                                      1u, UINT64_C(0x534348454d413032)) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_VERSION_MISMATCH);
  CHECK(nx_state_schema_registry_validate(&registry) == NX_STATUS_UNAVAILABLE);
  return 0;
}

static int test_schema_without_migration_arena(void) {
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_schema_definition definition = definition_a();
  nx_state_node_spec spec = node_spec(1u);
  nx_state_schema_compatibility compatibility = 0u;
  uint32_t path_count = UINT32_MAX;

  definition.min_supported_version = 1u;
  definition.min_compatible_version = 1u;
  definition.current_version = 1u;
  definition.max_payload_bytes = 0u;
  definition.allowed_node_flags = 0u;
  definition.required_node_flags = 0u;
  spec.flags = 0u;
  spec.payload_length = 0u;
  CHECK(nx_state_schema_registry_init(&registry, definitions, 1u, NULL, 0u,
                                      UINT64_C(0x534348454d413034)) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_classify(&registry, &spec, &compatibility) ==
        NX_STATUS_OK);
  CHECK(compatibility == NX_STATE_SCHEMA_EXACT);
  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, 100u, 1u, 1u, NULL, 0u, &path_count) == NX_STATUS_OK);
  CHECK(path_count == 0u);
  return 0;
}

static int test_definition_and_migration_bounds(void) {
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_schema_migration migrations[1];
  nx_state_schema_definition definition = definition_a();
  nx_state_schema_migration edge = migration(1u, 5u, 3001u);

  CHECK(nx_state_schema_registry_init(&registry, definitions, 1u, migrations,
                                      1u, UINT64_C(0x534348454d413033)) ==
        NX_STATUS_OK);
  definition.required_node_flags = NX_STATE_NODE_FLAG_EXECUTABLE;
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_INVALID_ARGUMENT);
  definition = definition_a();
  definition.min_compatible_version = 2u;
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  edge.max_output_bytes = 4097u;
  CHECK(nx_state_schema_registry_add_migration(&registry, &edge) ==
        NX_STATUS_VERSION_MISMATCH);
  edge = migration(1u, 5u, 3001u);
  CHECK(nx_state_schema_registry_add_migration(&registry, &edge) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_CONFLICT);
  CHECK(nx_state_schema_registry_add_migration(&registry, &edge) ==
        NX_STATUS_CONFLICT);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_OK);
  return 0;
}

static int test_schema_gated_store_writes(void) {
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_store store;
  nx_state_slot slots[4];
  uint32_t index[8];
  uint32_t rehash_index[8];
  const uint64_t key[2] = {UINT64_C(0x0f1e2d3c4b5a6978),
                           UINT64_C(0x8877665544332211)};
  nx_state_schema_definition definition = definition_a();
  nx_state_node_spec spec = node_spec(4u);
  nx_state_node_spec current_spec = node_spec(5u);
  nx_state_node_spec compatible_spec = node_spec(4u);
  nx_object_ref created;
  nx_object_ref created_sentinel;
  nx_state_node updated;
  nx_state_node updated_sentinel;
  nx_state_node readback;
  nx_state_store_stats stats;
  nx_result result;

  definition.min_supported_version = 4u;
  definition.min_compatible_version = 4u;
  definition.current_version = 5u;
  CHECK(nx_state_schema_registry_init(&registry, definitions, 1u, NULL, 0u,
                                      UINT64_C(0x534348454d413035)) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_OK);
  CHECK(nx_state_store_init(&store, slots, 4u, index, rehash_index, 8u,
                            UINT64_C(0x4e584f4c4d4f), key,
                            &result) == NX_STATUS_OK);

  memset(&created_sentinel, 0xa5, sizeof(created_sentinel));
  created = created_sentinel;
  CHECK(nx_state_store_create_with_schema(&registry, &store, &spec, &created,
                                          &result) ==
        NX_STATUS_VERSION_MISMATCH);
  CHECK(nx_result_validate(&result) == NX_STATUS_OK);
  CHECK(result.operation_id == NX_OP_STATE_STORE_CREATE);
  CHECK(memcmp(&created, &created_sentinel, sizeof(created)) == 0);
  CHECK(nx_state_store_stats_read(&store, &stats, &result) == NX_STATUS_OK);
  CHECK(stats.count == 0u && stats.epoch == 0u);

  CHECK(nx_state_store_create_with_schema(&registry, &store, &current_spec,
                                          &created, &result) == NX_STATUS_OK);
  CHECK(nx_object_ref_is_valid(created) != 0);
  CHECK(nx_state_store_read(&store, created, &readback, &result) ==
        NX_STATUS_OK);
  CHECK(readback.spec.schema_version == 5u && readback.revision == 1u);

  memset(&updated_sentinel, 0xa5, sizeof(updated_sentinel));
  updated = updated_sentinel;
  CHECK(nx_state_store_update_with_schema(
            &registry, &store, created, 1u, &compatible_spec, &updated,
            &result) == NX_STATUS_VERSION_MISMATCH);
  CHECK(memcmp(&updated, &updated_sentinel, sizeof(updated)) == 0);
  CHECK(nx_result_validate(&result) == NX_STATUS_OK);
  CHECK(result.operation_id == NX_OP_STATE_STORE_UPDATE);
  CHECK(nx_state_store_read(&store, created, &readback, &result) ==
        NX_STATUS_OK);
  CHECK(readback.spec.schema_version == 5u && readback.revision == 1u);

  current_spec.lifecycle = NX_STATE_LIFECYCLE_FROZEN;
  CHECK(nx_state_store_update_with_schema(&registry, &store, created, 1u,
                                          &current_spec, &updated,
                                          &result) == NX_STATUS_OK);
  CHECK(updated.revision == 2u);
  CHECK(updated.spec.lifecycle == NX_STATE_LIFECYCLE_FROZEN);

  /* The write gate revalidates the sealed manifest before touching the store.
   */
  definitions[0].max_payload_bytes++;
  current_spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
  CHECK(nx_state_store_update_with_schema(&registry, &store, created, 2u,
                                          &current_spec, &updated,
                                          &result) == NX_STATUS_CORRUPT);
  CHECK(nx_state_store_read(&store, created, &readback, &result) ==
        NX_STATUS_OK);
  CHECK(readback.revision == 2u);
  CHECK(check_schema_metric(&registry, 9u,
                            NX_OP_STATE_STORE_CREATE_WITH_SCHEMA));
  CHECK(check_schema_metric(&registry, 10u,
                            NX_OP_STATE_STORE_UPDATE_WITH_SCHEMA));
  return 0;
}

int main(void) {
  CHECK(test_catalog_and_migration_paths() == 0);
  CHECK(test_missing_migration_rejected_at_seal() == 0);
  CHECK(test_schema_without_migration_arena() == 0);
  CHECK(test_definition_and_migration_bounds() == 0);
  CHECK(test_schema_gated_store_writes() == 0);
  (void)puts("registry schema catalog/migration descriptor tests: PASS");
  return 0;
}
