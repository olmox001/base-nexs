/* NXOLMO registry algorithm port; one product-owned implementation. */
#include "nexs_schema.h"

#include "nexs_checked.h"
#include "nexs_crypto.h"

#include <stddef.h>
#include <string.h>

#define NX_SCHEMA_KNOWN_NODE_FLAGS                                             \
  ((uint32_t)(NX_STATE_NODE_FLAG_IMMUTABLE | NX_STATE_NODE_FLAG_SPARSE |       \
              NX_STATE_NODE_FLAG_CONTENT_ADDRESSED |                           \
              NX_STATE_NODE_FLAG_EXECUTABLE))

static nx_status schema_hash_update(nx_sha256_context *context,
                                    const uint8_t *bytes, size_t length) {
  nx_const_span span;
  span.data = bytes;
  span.size = length;
  return nx_sha256_update(context, span);
}

static nx_status schema_hash_u16(nx_sha256_context *context, uint16_t value) {
  uint8_t bytes[2];
  bytes[0] = (uint8_t)(value >> 8u);
  bytes[1] = (uint8_t)value;
  return schema_hash_update(context, bytes, sizeof(bytes));
}

static nx_status schema_hash_u32(nx_sha256_context *context, uint32_t value) {
  uint8_t bytes[4];
  bytes[0] = (uint8_t)(value >> 24u);
  bytes[1] = (uint8_t)(value >> 16u);
  bytes[2] = (uint8_t)(value >> 8u);
  bytes[3] = (uint8_t)value;
  return schema_hash_update(context, bytes, sizeof(bytes));
}

static nx_status schema_hash_u64(nx_sha256_context *context, uint64_t value) {
  uint8_t bytes[8];
  bytes[0] = (uint8_t)(value >> 56u);
  bytes[1] = (uint8_t)(value >> 48u);
  bytes[2] = (uint8_t)(value >> 40u);
  bytes[3] = (uint8_t)(value >> 32u);
  bytes[4] = (uint8_t)(value >> 24u);
  bytes[5] = (uint8_t)(value >> 16u);
  bytes[6] = (uint8_t)(value >> 8u);
  bytes[7] = (uint8_t)value;
  return schema_hash_update(context, bytes, sizeof(bytes));
}

static nx_status schema_manifest_hash_definition(
    nx_sha256_context *context, const nx_state_schema_definition *definition) {
  nx_status status = schema_hash_u64(context, definition->type_id);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(context, definition->schema_id);
  if (status == NX_STATUS_OK)
    status = schema_hash_u16(context, definition->min_supported_version);
  if (status == NX_STATUS_OK)
    status = schema_hash_u16(context, definition->min_compatible_version);
  if (status == NX_STATUS_OK)
    status = schema_hash_u16(context, definition->current_version);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(context, definition->allowed_node_flags);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(context, definition->required_node_flags);
  if (status == NX_STATUS_OK)
    status = schema_hash_u64(context, definition->max_payload_bytes);
  return status;
}

static nx_status schema_manifest_hash_migration(
    nx_sha256_context *context, const nx_state_schema_migration *migration) {
  nx_status status = schema_hash_u64(context, migration->type_id);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(context, migration->schema_id);
  if (status == NX_STATUS_OK)
    status = schema_hash_u16(context, migration->from_version);
  if (status == NX_STATUS_OK)
    status = schema_hash_u16(context, migration->to_version);
  if (status == NX_STATUS_OK)
    status = schema_hash_u64(context, migration->migration_id);
  if (status == NX_STATUS_OK)
    status = schema_hash_update(context, migration->program_digest.bytes,
                                sizeof(migration->program_digest.bytes));
  if (status == NX_STATUS_OK)
    status = schema_hash_u64(context, migration->fuel_limit);
  if (status == NX_STATUS_OK)
    status = schema_hash_u64(context, migration->max_input_bytes);
  if (status == NX_STATUS_OK)
    status = schema_hash_u64(context, migration->max_output_bytes);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(context, migration->flags);
  return status;
}

static nx_status
schema_manifest_compute(const nx_state_schema_registry *registry,
                        nx_digest256 *digest) {
  static const uint8_t domain[] = "NXOLMO-SCHEMA-MANIFEST-v2";
  nx_sha256_context context;
  nx_status status;
  uint32_t index;
  nx_sha256_init(&context);
  status = schema_hash_update(&context, domain, sizeof(domain) - 1u);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(&context, registry->definition_count);
  if (status == NX_STATUS_OK)
    status = schema_hash_u32(&context, registry->migration_count);
  for (index = 0u; status == NX_STATUS_OK &&
       index < registry->definition_count; ++index)
    status = schema_manifest_hash_definition(
        &context, &registry->definitions[index]);
  for (index = 0u; status == NX_STATUS_OK &&
       index < registry->migration_count; ++index)
    status = schema_manifest_hash_migration(
        &context, &registry->migrations[index]);
  return status == NX_STATUS_OK ? nx_sha256_final(&context, digest) : status;
}

static nx_status schema_array_bytes(uint32_t capacity, size_t element_size,
                                    size_t *bytes) {
  if (bytes == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  return nx_size_mul((size_t)capacity, element_size, bytes);
}

static int schema_definition_less(const nx_state_schema_definition *a,
                                  const nx_state_schema_definition *b) {
  if (a->type_id != b->type_id)
    return a->type_id < b->type_id;
  return a->schema_id < b->schema_id;
}

static int schema_migration_less(const nx_state_schema_migration *a,
                                 const nx_state_schema_migration *b) {
  if (a->type_id != b->type_id)
    return a->type_id < b->type_id;
  if (a->schema_id != b->schema_id)
    return a->schema_id < b->schema_id;
  return a->from_version < b->from_version;
}

static nx_status
schema_definition_validate(const nx_state_schema_definition *definition) {
  if (definition == NULL || definition->type_id == 0u ||
      definition->schema_id == 0u || definition->min_supported_version == 0u ||
      definition->min_compatible_version < definition->min_supported_version ||
      definition->current_version < definition->min_compatible_version ||
      definition->reserved16 != 0u || definition->reserved32 != 0u ||
      (definition->allowed_node_flags & ~NX_SCHEMA_KNOWN_NODE_FLAGS) != 0u ||
      (definition->required_node_flags & ~definition->allowed_node_flags) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  return NX_STATUS_OK;
}

static nx_status
schema_migration_validate(const nx_state_schema_migration *migration) {
  if (migration == NULL || migration->type_id == 0u ||
      migration->schema_id == 0u || migration->from_version == 0u ||
      migration->to_version <= migration->from_version ||
      migration->migration_id == 0u || migration->fuel_limit == 0u ||
      migration->max_input_bytes == 0u || migration->max_output_bytes == 0u ||
      migration->flags != 0u || migration->reserved != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  return NX_STATUS_OK;
}

static uint32_t
schema_find_definition_linear(const nx_state_schema_registry *registry,
                              uint64_t type_id, uint32_t schema_id) {
  uint32_t i;
  for (i = 0u; i < registry->definition_count; ++i) {
    const nx_state_schema_definition *definition = &registry->definitions[i];
    if (definition->type_id == type_id && definition->schema_id == schema_id)
      return i;
  }
  return UINT32_MAX;
}

static const nx_state_schema_migration *
schema_find_migration(const nx_state_schema_registry *registry,
                      uint64_t type_id, uint32_t schema_id,
                      uint16_t from_version) {
  uint32_t i;
  for (i = 0u; i < registry->migration_count; ++i) {
    const nx_state_schema_migration *migration = &registry->migrations[i];
    if (migration->type_id == type_id && migration->schema_id == schema_id &&
        migration->from_version == from_version)
      return migration;
  }
  return NULL;
}

static nx_status
schema_validate_migration_paths(const nx_state_schema_registry *registry) {
  uint32_t i;
  for (i = 0u; i < registry->migration_count; ++i) {
    const nx_state_schema_migration *migration = &registry->migrations[i];
    uint32_t definition_index = schema_find_definition_linear(
        registry, migration->type_id, migration->schema_id);
    const nx_state_schema_definition *definition;
    if (definition_index == UINT32_MAX)
      return NX_STATUS_NOT_FOUND;
    definition = &registry->definitions[definition_index];
    if (migration->from_version < definition->min_supported_version ||
        migration->from_version >= definition->min_compatible_version ||
        migration->to_version > definition->current_version ||
        migration->max_input_bytes > definition->max_payload_bytes ||
        migration->max_output_bytes > definition->max_payload_bytes)
      return NX_STATUS_VERSION_MISMATCH;
  }

  for (i = 0u; i < registry->definition_count; ++i) {
    const nx_state_schema_definition *definition = &registry->definitions[i];
    uint32_t version;
    for (version = (uint32_t)definition->min_supported_version;
         version < (uint32_t)definition->min_compatible_version; ++version) {
      uint16_t current = (uint16_t)version;
      uint32_t hops = 0u;
      while (current != definition->current_version) {
        const nx_state_schema_migration *migration = schema_find_migration(
            registry, definition->type_id, definition->schema_id, current);
        if (migration == NULL)
          return NX_STATUS_VERSION_MISMATCH;
        if (migration->to_version <= current ||
            migration->to_version > definition->current_version)
          return NX_STATUS_CORRUPT;
        current = migration->to_version;
        hops++;
        if (hops > registry->migration_count)
          return NX_STATUS_CORRUPT;
      }
    }
  }
  return NX_STATUS_OK;
}

static nx_status
schema_registry_validate_internal(const nx_state_schema_registry *registry) {
  uint32_t i;
  if (registry->definitions == NULL || registry->definition_capacity == 0u ||
      registry->definition_count == 0u ||
      registry->definition_count > registry->definition_capacity ||
      registry->migration_count > registry->migration_capacity ||
      (registry->migration_capacity != 0u && registry->migrations == NULL) ||
      registry->registry_id == 0u)
    return NX_STATUS_CORRUPT;
  for (i = 0u; i < registry->definition_count; ++i) {
    if (schema_definition_validate(&registry->definitions[i]) != NX_STATUS_OK)
      return NX_STATUS_CORRUPT;
    if (i != 0u && !schema_definition_less(&registry->definitions[i - 1u],
                                           &registry->definitions[i]))
      return NX_STATUS_CORRUPT;
  }
  for (i = 0u; i < registry->migration_count; ++i) {
    const nx_state_schema_migration *migration = &registry->migrations[i];
    uint32_t j;
    if (schema_migration_validate(migration) != NX_STATUS_OK)
      return NX_STATUS_CORRUPT;
    if (i != 0u &&
        !schema_migration_less(&registry->migrations[i - 1u], migration))
      return NX_STATUS_CORRUPT;
    for (j = 0u; j < i; ++j) {
      if (registry->migrations[j].migration_id == migration->migration_id)
        return NX_STATUS_CONFLICT;
    }
  }
  return schema_validate_migration_paths(registry);
}

static nx_status schema_registry_init_impl(nx_state_schema_registry *registry,
                                        nx_state_schema_definition *definitions,
                                        uint32_t definition_capacity,
                                        nx_state_schema_migration *migrations,
                                        uint32_t migration_capacity,
                                        uint64_t registry_id) {
  size_t definition_bytes = 0u;
  size_t migration_bytes = 0u;
  if (registry == NULL || definitions == NULL || definition_capacity == 0u ||
      (migration_capacity != 0u && migrations == NULL) || registry_id == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (((uintptr_t)registry % _Alignof(nx_state_schema_registry)) != 0u ||
      ((uintptr_t)definitions % _Alignof(nx_state_schema_definition)) != 0u ||
      (migration_capacity != 0u &&
       ((uintptr_t)migrations % _Alignof(nx_state_schema_migration)) != 0u))
    return NX_STATUS_INVALID_ARGUMENT;
  if (schema_array_bytes(definition_capacity, sizeof(*definitions),
                         &definition_bytes) != NX_STATUS_OK ||
      schema_array_bytes(migration_capacity, sizeof(*migrations),
                         &migration_bytes) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(registry, sizeof(*registry), definitions,
                                definition_bytes) != 0 ||
      (migration_capacity != 0u &&
       (nx_pointer_ranges_overlap(registry, sizeof(*registry), migrations,
                                  migration_bytes) != 0 ||
        nx_pointer_ranges_overlap(definitions, definition_bytes, migrations,
                                  migration_bytes) != 0)))
    return NX_STATUS_INVALID_ARGUMENT;
  memset(registry, 0, sizeof(*registry));
  memset(definitions, 0, definition_bytes);
  if (migration_capacity != 0u)
    memset(migrations, 0, migration_bytes);
  registry->definitions = definitions;
  registry->migrations = migrations;
  registry->definition_capacity = definition_capacity;
  registry->migration_capacity = migration_capacity;
  registry->registry_id = registry_id;
  atomic_init(&registry->initialized, 1u);
  atomic_init(&registry->sealed, 0u);
  if (atomic_is_lock_free(&registry->initialized) == 0 ||
      atomic_is_lock_free(&registry->sealed) == 0) {
    atomic_store_explicit(&registry->initialized, 0u, memory_order_release);
    return NX_STATUS_NOT_SUPPORTED;
  }
  return NX_STATUS_OK;
}

static nx_status schema_registry_add_definition_impl(
    nx_state_schema_registry *registry,
    const nx_state_schema_definition *definition) {
  nx_state_schema_definition copy;
  nx_status status;
  if (registry == NULL || definition == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (atomic_load_explicit(&registry->sealed, memory_order_acquire) != 0u)
    return NX_STATUS_ACCESS_DENIED;
  if (((uintptr_t)definition % _Alignof(nx_state_schema_definition)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (registry->definitions == NULL ||
      registry->definition_count > registry->definition_capacity)
    return NX_STATUS_CORRUPT;
  copy = *definition;
  status = schema_definition_validate(&copy);
  if (status != NX_STATUS_OK)
    return status;
  if (schema_find_definition_linear(registry, copy.type_id, copy.schema_id) !=
      UINT32_MAX)
    return NX_STATUS_CONFLICT;
  if (registry->definition_count >= registry->definition_capacity)
    return NX_STATUS_RESOURCE_EXHAUSTED;
  registry->definitions[registry->definition_count++] = copy;
  return NX_STATUS_OK;
}

static nx_status schema_registry_add_migration_impl(
    nx_state_schema_registry *registry,
    const nx_state_schema_migration *migration) {
  nx_state_schema_migration copy;
  uint32_t definition_index;
  uint32_t i;
  const nx_state_schema_definition *definition;
  nx_status status;
  if (registry == NULL || migration == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (atomic_load_explicit(&registry->sealed, memory_order_acquire) != 0u)
    return NX_STATUS_ACCESS_DENIED;
  if (((uintptr_t)migration % _Alignof(nx_state_schema_migration)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (registry->definitions == NULL ||
      registry->definition_count > registry->definition_capacity ||
      registry->migration_count > registry->migration_capacity ||
      (registry->migration_capacity != 0u && registry->migrations == NULL))
    return NX_STATUS_CORRUPT;
  copy = *migration;
  status = schema_migration_validate(&copy);
  if (status != NX_STATUS_OK)
    return status;
  definition_index =
      schema_find_definition_linear(registry, copy.type_id, copy.schema_id);
  if (definition_index == UINT32_MAX)
    return NX_STATUS_NOT_FOUND;
  definition = &registry->definitions[definition_index];
  if (copy.from_version < definition->min_supported_version ||
      copy.from_version >= definition->min_compatible_version ||
      copy.to_version > definition->current_version ||
      copy.max_input_bytes > definition->max_payload_bytes ||
      copy.max_output_bytes > definition->max_payload_bytes)
    return NX_STATUS_VERSION_MISMATCH;
  for (i = 0u; i < registry->migration_count; ++i) {
    const nx_state_schema_migration *existing = &registry->migrations[i];
    if (existing->migration_id == copy.migration_id ||
        (existing->type_id == copy.type_id &&
         existing->schema_id == copy.schema_id &&
         existing->from_version == copy.from_version))
      return NX_STATUS_CONFLICT;
  }
  if (registry->migration_count >= registry->migration_capacity)
    return NX_STATUS_RESOURCE_EXHAUSTED;
  registry->migrations[registry->migration_count++] = copy;
  return NX_STATUS_OK;
}

static nx_status schema_registry_seal_impl(nx_state_schema_registry *registry) {
  uint32_t i;
  nx_status status;
  if (registry == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (atomic_load_explicit(&registry->sealed, memory_order_acquire) != 0u)
    return NX_STATUS_CONFLICT;
  if (registry->definitions == NULL || registry->definition_capacity == 0u ||
      registry->definition_count == 0u ||
      registry->definition_count > registry->definition_capacity ||
      registry->migration_count > registry->migration_capacity ||
      (registry->migration_capacity != 0u && registry->migrations == NULL))
    return NX_STATUS_CORRUPT;
  for (i = 1u; i < registry->definition_count; ++i) {
    nx_state_schema_definition value = registry->definitions[i];
    uint32_t j = i;
    while (j > 0u &&
           schema_definition_less(&value, &registry->definitions[j - 1u])) {
      registry->definitions[j] = registry->definitions[j - 1u];
      --j;
    }
    registry->definitions[j] = value;
  }
  for (i = 1u; i < registry->migration_count; ++i) {
    nx_state_schema_migration value = registry->migrations[i];
    uint32_t j = i;
    while (j > 0u &&
           schema_migration_less(&value, &registry->migrations[j - 1u])) {
      registry->migrations[j] = registry->migrations[j - 1u];
      --j;
    }
    registry->migrations[j] = value;
  }
  nx_digest256 manifest_digest;
  status = schema_registry_validate_internal(registry);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_manifest_compute(registry, &manifest_digest);
  if (status != NX_STATUS_OK)
    return status;
  registry->manifest_digest = manifest_digest;
  atomic_store_explicit(&registry->sealed, 1u, memory_order_release);
  return NX_STATUS_OK;
}

static nx_status
schema_registry_validate_impl(const nx_state_schema_registry *registry) {
  nx_digest256 computed;
  nx_status status;
  if (registry == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) ==
          0u ||
      atomic_load_explicit(&registry->sealed, memory_order_acquire) == 0u)
    return NX_STATUS_UNAVAILABLE;
  status = schema_registry_validate_internal(registry);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_manifest_compute(registry, &computed);
  if (status != NX_STATUS_OK)
    return status;
  return nx_crypto_constant_time_equal(computed.bytes,
                                       registry->manifest_digest.bytes,
                                       sizeof(computed.bytes)) != 0
             ? NX_STATUS_OK
             : NX_STATUS_CORRUPT;
}

static nx_status schema_registry_manifest_digest_impl(
    const nx_state_schema_registry *registry, nx_digest256 *digest) {
  size_t definitions_bytes = 0u;
  size_t migrations_bytes = 0u;
  nx_status status;
  if (registry == NULL || digest == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) ==
          0u ||
      atomic_load_explicit(&registry->sealed, memory_order_acquire) == 0u)
    return NX_STATUS_UNAVAILABLE;
  if (((uintptr_t)digest % _Alignof(nx_digest256)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (schema_array_bytes(registry->definition_capacity,
                         sizeof(*registry->definitions),
                         &definitions_bytes) != NX_STATUS_OK ||
      schema_array_bytes(registry->migration_capacity,
                         sizeof(*registry->migrations),
                         &migrations_bytes) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(registry, sizeof(*registry), digest,
                                sizeof(*digest)) != 0 ||
      nx_pointer_ranges_overlap(registry->definitions, definitions_bytes,
                                digest, sizeof(*digest)) != 0 ||
      (registry->migration_capacity != 0u &&
       nx_pointer_ranges_overlap(registry->migrations, migrations_bytes, digest,
                                 sizeof(*digest)) != 0))
    return NX_STATUS_INVALID_ARGUMENT;
  status = nx_state_schema_registry_validate(registry);
  if (status != NX_STATUS_OK)
    return status;
  *digest = registry->manifest_digest;
  return NX_STATUS_OK;
}

static nx_status
schema_registry_lookup_impl(const nx_state_schema_registry *registry,
                                uint64_t type_id, uint32_t schema_id,
                                const nx_state_schema_definition **definition) {
  uint32_t low = 0u;
  uint32_t high;
  if (registry == NULL || definition == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) ==
          0u ||
      atomic_load_explicit(&registry->sealed, memory_order_acquire) == 0u ||
      type_id == 0u || schema_id == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (((uintptr_t)definition % _Alignof(const nx_state_schema_definition *)) !=
          0u ||
      nx_pointer_ranges_overlap(registry, sizeof(*registry), definition,
                                sizeof(*definition)) != 0 ||
      nx_pointer_ranges_overlap(registry->definitions,
                                (size_t)registry->definition_capacity *
                                    sizeof(*registry->definitions),
                                definition, sizeof(*definition)) != 0 ||
      (registry->migration_capacity != 0u &&
       nx_pointer_ranges_overlap(registry->migrations,
                                 (size_t)registry->migration_capacity *
                                     sizeof(*registry->migrations),
                                 definition, sizeof(*definition)) != 0))
    return NX_STATUS_INVALID_ARGUMENT;
  *definition = NULL;
  high = registry->definition_count;
  while (low < high) {
    uint32_t middle = low + (high - low) / 2u;
    const nx_state_schema_definition *candidate =
        &registry->definitions[middle];
    if (candidate->type_id < type_id ||
        (candidate->type_id == type_id && candidate->schema_id < schema_id))
      low = middle + 1u;
    else
      high = middle;
  }
  if (low >= registry->definition_count ||
      registry->definitions[low].type_id != type_id ||
      registry->definitions[low].schema_id != schema_id)
    return NX_STATUS_NOT_FOUND;
  *definition = &registry->definitions[low];
  return NX_STATUS_OK;
}

static nx_status schema_registry_classify_impl(
    const nx_state_schema_registry *registry, const nx_state_node_spec *spec,
    nx_state_schema_compatibility *compatibility) {
  const nx_state_schema_definition *definition = NULL;
  nx_status status;
  if (registry == NULL || spec == NULL || compatibility == NULL ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) ==
          0u ||
      atomic_load_explicit(&registry->sealed, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (((uintptr_t)compatibility % _Alignof(nx_state_schema_compatibility)) !=
          0u ||
      nx_pointer_ranges_overlap(spec, sizeof(*spec), compatibility,
                                sizeof(*compatibility)) != 0 ||
      nx_pointer_ranges_overlap(registry, sizeof(*registry), compatibility,
                                sizeof(*compatibility)) != 0 ||
      nx_pointer_ranges_overlap(registry->definitions,
                                (size_t)registry->definition_capacity *
                                    sizeof(*registry->definitions),
                                compatibility, sizeof(*compatibility)) != 0 ||
      (registry->migration_capacity != 0u &&
       nx_pointer_ranges_overlap(registry->migrations,
                                 (size_t)registry->migration_capacity *
                                     sizeof(*registry->migrations),
                                 compatibility, sizeof(*compatibility)) != 0))
    return NX_STATUS_INVALID_ARGUMENT;
  status = nx_state_node_spec_validate(spec);
  if (status != NX_STATUS_OK)
    return status;
  status = nx_state_schema_registry_lookup(registry, spec->type_id,
                                           spec->schema_id, &definition);
  if (status != NX_STATUS_OK)
    return status;
  if (spec->schema_version < definition->min_supported_version ||
      spec->schema_version > definition->current_version)
    return NX_STATUS_VERSION_MISMATCH;
  if (spec->payload_length > definition->max_payload_bytes)
    return NX_STATUS_LIMIT_EXCEEDED;
  if ((spec->flags & ~definition->allowed_node_flags) != 0u ||
      (spec->flags & definition->required_node_flags) !=
          definition->required_node_flags)
    return NX_STATUS_NOT_SUPPORTED;
  if (spec->schema_version == definition->current_version)
    *compatibility = NX_STATE_SCHEMA_EXACT;
  else if (spec->schema_version >= definition->min_compatible_version)
    *compatibility = NX_STATE_SCHEMA_COMPATIBLE;
  else
    *compatibility = NX_STATE_SCHEMA_MIGRATION_REQUIRED;
  return NX_STATUS_OK;
}

static nx_status schema_registry_validate_write_impl(
    const nx_state_schema_registry *registry, const nx_state_node_spec *spec) {
  nx_state_schema_compatibility compatibility;
  nx_status status = nx_state_schema_registry_validate(registry);
  if (status != NX_STATUS_OK)
    return status;
  status = nx_state_schema_registry_classify(registry, spec, &compatibility);
  if (status != NX_STATUS_OK)
    return status;
  return compatibility == NX_STATE_SCHEMA_EXACT ? NX_STATUS_OK
                                                : NX_STATUS_VERSION_MISMATCH;
}

static nx_status schema_write_rejected(nx_status status, uint64_t operation_id,
                                       nx_result *result) {
  (void)nx_result_init(result, status, NX_RESULT_DOMAIN_REGISTRY, operation_id,
                       0u, 0u, NX_CYCLES_UNKNOWN, 0u);
  return status;
}

static nx_status state_store_create_with_schema_impl(
    const nx_state_schema_registry *registry, nx_state_store *store,
    const nx_state_node_spec *spec, nx_object_ref *created, nx_result *result) {
  nx_state_node_spec spec_copy;
  nx_status status;
  if (result == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  if (spec == NULL)
    return schema_write_rejected(NX_STATUS_INVALID_ARGUMENT,
                                 NX_OP_STATE_STORE_CREATE, result);
  spec_copy = *spec;
  status = nx_state_schema_registry_validate_write(registry, &spec_copy);
  if (status != NX_STATUS_OK)
    return schema_write_rejected(status, NX_OP_STATE_STORE_CREATE, result);
  return nx_state_store_create(store, &spec_copy, created, result);
}

static nx_status state_store_update_with_schema_impl(
    const nx_state_schema_registry *registry, nx_state_store *store,
    nx_object_ref reference, uint64_t expected_revision,
    const nx_state_node_spec *spec, nx_state_node *updated, nx_result *result) {
  nx_state_node_spec spec_copy;
  nx_status status;
  if (result == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  if (spec == NULL)
    return schema_write_rejected(NX_STATUS_INVALID_ARGUMENT,
                                 NX_OP_STATE_STORE_UPDATE, result);
  spec_copy = *spec;
  status = nx_state_schema_registry_validate_write(registry, &spec_copy);
  if (status != NX_STATUS_OK)
    return schema_write_rejected(status, NX_OP_STATE_STORE_UPDATE, result);
  return nx_state_store_update(store, reference, expected_revision, &spec_copy,
                               updated, result);
}

static nx_status schema_resolve_validate_outputs(
    const nx_state_schema_registry *registry,
    const nx_state_schema_migration **path, uint32_t path_capacity,
    uint32_t *path_count, size_t *path_bytes) {
  size_t definitions_bytes = 0u;
  size_t migrations_bytes = 0u;
  if (registry == NULL || path_count == NULL ||
      (path_capacity != 0u && path == NULL) ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) == 0u ||
      atomic_load_explicit(&registry->sealed, memory_order_acquire) == 0u ||
      ((uintptr_t)path_count % _Alignof(uint32_t)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (schema_array_bytes(path_capacity, sizeof(*path), path_bytes) !=
          NX_STATUS_OK ||
      schema_array_bytes(registry->definition_capacity,
                         sizeof(*registry->definitions),
                         &definitions_bytes) != NX_STATUS_OK ||
      schema_array_bytes(registry->migration_capacity,
                         sizeof(*registry->migrations),
                         &migrations_bytes) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (nx_pointer_ranges_overlap(registry, sizeof(*registry), path_count,
                                sizeof(*path_count)) != 0 ||
      nx_pointer_ranges_overlap(registry->definitions, definitions_bytes,
                                path_count, sizeof(*path_count)) != 0 ||
      (registry->migration_capacity != 0u &&
       nx_pointer_ranges_overlap(registry->migrations, migrations_bytes,
                                 path_count, sizeof(*path_count)) != 0))
    return NX_STATUS_INVALID_ARGUMENT;
  if (path_capacity != 0u &&
      (((uintptr_t)path % _Alignof(const nx_state_schema_migration *)) != 0u ||
       nx_pointer_ranges_overlap(registry, sizeof(*registry), path,
                                 *path_bytes) != 0 ||
       nx_pointer_ranges_overlap(registry->definitions, definitions_bytes,
                                 path, *path_bytes) != 0 ||
       (registry->migration_capacity != 0u &&
        nx_pointer_ranges_overlap(registry->migrations, migrations_bytes,
                                  path, *path_bytes) != 0) ||
       nx_pointer_ranges_overlap(path, *path_bytes, path_count,
                                 sizeof(*path_count)) != 0))
    return NX_STATUS_INVALID_ARGUMENT;
  *path_count = 0u;
  return NX_STATUS_OK;
}

static nx_status schema_resolve_count_hops(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, uint16_t from_version,
    const nx_state_schema_definition *definition, uint32_t *hops) {
  uint16_t current = from_version;
  *hops = 0u;
  while (current != definition->current_version) {
    const nx_state_schema_migration *migration =
        schema_find_migration(registry, type_id, schema_id, current);
    if (migration == NULL)
      return NX_STATUS_CORRUPT;
    current = migration->to_version;
    (*hops)++;
    if (*hops > registry->migration_count)
      return NX_STATUS_CORRUPT;
  }
  return NX_STATUS_OK;
}

static nx_status schema_resolve_fill_path(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, uint16_t from_version, uint32_t hops,
    const nx_state_schema_migration **path) {
  uint16_t current = from_version;
  uint32_t index;
  for (index = 0u; index < hops; ++index) {
    path[index] = schema_find_migration(
        registry, type_id, schema_id, current);
    if (path[index] == NULL)
      return NX_STATUS_CORRUPT;
    current = path[index]->to_version;
  }
  return NX_STATUS_OK;
}

static nx_status schema_registry_resolve_migration_impl(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, uint16_t from_version,
    const nx_state_schema_migration **path, uint32_t path_capacity,
    uint32_t *path_count) {
  const nx_state_schema_definition *definition = NULL;
  uint32_t hops = 0u;
  size_t path_bytes = 0u;
  nx_status status = schema_resolve_validate_outputs(
      registry, path, path_capacity, path_count, &path_bytes);
  (void)path_bytes;
  if (status != NX_STATUS_OK)
    return status;
  status = nx_state_schema_registry_lookup(
      registry, type_id, schema_id, &definition);
  if (status != NX_STATUS_OK)
    return status;
  if (from_version < definition->min_supported_version ||
      from_version > definition->current_version)
    return NX_STATUS_VERSION_MISMATCH;
  if (from_version >= definition->min_compatible_version)
    return NX_STATUS_OK;
  status = schema_resolve_count_hops(
      registry, type_id, schema_id, from_version, definition, &hops);
  if (status != NX_STATUS_OK)
    return status;
  *path_count = hops;
  if (path_capacity < hops)
    return NX_STATUS_NO_SPACE;
  return schema_resolve_fill_path(
      registry, type_id, schema_id, from_version, hops, path);
}


/* Rule 11 accounting for schema operations. Bounds are derived from the
 * caller-owned catalog dimensions; no fixed schema-count ceiling is imposed. */
enum schema_metric_slot {
  SCHEMA_METRIC_INIT = 0,
  SCHEMA_METRIC_ADD_DEFINITION,
  SCHEMA_METRIC_ADD_MIGRATION,
  SCHEMA_METRIC_SEAL,
  SCHEMA_METRIC_VALIDATE,
  SCHEMA_METRIC_MANIFEST,
  SCHEMA_METRIC_LOOKUP,
  SCHEMA_METRIC_CLASSIFY,
  SCHEMA_METRIC_VALIDATE_WRITE,
  SCHEMA_METRIC_CREATE_WITH_SCHEMA,
  SCHEMA_METRIC_UPDATE_WITH_SCHEMA,
  SCHEMA_METRIC_RESOLVE_MIGRATION
};

static nx_status schema_bound_add(uint64_t *total, uint64_t value) {
  return nx_u64_add(*total, value, total);
}

static nx_status schema_bound_product(uint64_t a, uint64_t b,
                                      uint64_t *product) {
  return nx_u64_mul(a, b, product);
}

static nx_status schema_registry_bound(const nx_state_schema_registry *registry,
                                       uint64_t base, uint64_t per_definition,
                                       uint64_t per_migration,
                                       uint64_t cross_factor,
                                       uint64_t *bound) {
  uint64_t term;
  nx_status status;
  if (bound == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  *bound = base;
  if (registry == NULL ||
      ((uintptr_t)registry % _Alignof(nx_state_schema_registry)) != 0u ||
      atomic_load_explicit(&registry->initialized, memory_order_acquire) == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (registry->definitions == NULL || registry->definition_capacity == 0u ||
      registry->definition_count > registry->definition_capacity ||
      registry->migration_count > registry->migration_capacity ||
      (registry->migration_capacity != 0u && registry->migrations == NULL))
    return NX_STATUS_CORRUPT;
  status = schema_bound_product(registry->definition_count,
                                per_definition, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(bound, term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(registry->migration_count,
                                  per_migration, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(bound, term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(registry->definition_count,
                                  registry->migration_count, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(term, cross_factor, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(bound, term);
  return status;
}

static nx_status schema_scope_begin(nx_state_schema_registry *registry,
                                    uint32_t metric_slot, uint64_t operation_id,
                                    uint64_t bound,
                                    nx_operation_scope *scope) {
  nx_operation_metrics *metrics = NULL;
  nx_status status;
  if (registry != NULL &&
      ((uintptr_t)registry % _Alignof(nx_state_schema_registry)) == 0u &&
      atomic_load_explicit(&registry->initialized, memory_order_acquire) != 0u &&
      metric_slot < NX_STATE_SCHEMA_OPERATION_METRIC_COUNT)
    metrics = &registry->operation_metrics[metric_slot];
  status = nx_operation_scope_begin(scope, metrics, operation_id, bound, UINT64_MAX);
  if (status == NX_STATUS_OK) {
    status = nx_operation_scope_consume(scope, bound);
    if (status != NX_STATUS_OK)
      (void)nx_operation_scope_finish(scope, status,
                                      NX_RESULT_DOMAIN_REGISTRY, 0u, 0u, NULL);
  }
  return status;
}

static nx_status schema_scope_finish(nx_operation_scope *scope,
                                     nx_status operation_status) {
  return nx_operation_scope_finish(scope, operation_status,
                                   NX_RESULT_DOMAIN_REGISTRY, 0u, 0u, NULL);
}

nx_status nx_state_schema_registry_init(nx_state_schema_registry *registry,
                                        nx_state_schema_definition *definitions,
                                        uint32_t definition_capacity,
                                        nx_state_schema_migration *migrations,
                                        uint32_t migration_capacity,
                                        uint64_t registry_id) {
  nx_operation_scope scope;
  nx_operation_measurement measurement;
  uint64_t bound = 16u;
  uint64_t term;
  nx_status status;
  nx_status begin_status;
  if (nx_u64_mul(definition_capacity,
                 (uint64_t)sizeof(*definitions), &term) != NX_STATUS_OK ||
      schema_bound_add(&bound, term) != NX_STATUS_OK ||
      nx_u64_mul(migration_capacity,
                 (uint64_t)sizeof(*migrations), &term) != NX_STATUS_OK ||
      schema_bound_add(&bound, term) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  begin_status = nx_operation_scope_begin(&scope, NULL, NX_OP_STATE_SCHEMA_REGISTRY_INIT,
                                          bound, UINT64_MAX);
  if (begin_status == NX_STATUS_OK)
    begin_status = nx_operation_scope_consume(&scope, bound);
  if (begin_status != NX_STATUS_OK)
    return begin_status;
  status = schema_registry_init_impl(registry, definitions,
                                     definition_capacity, migrations,
                                     migration_capacity, registry_id);
  if (nx_operation_budget_finish(&scope.budget, &measurement) == NX_STATUS_OK) {
    if (status == NX_STATUS_OK) {
      uint32_t index;
      static const uint64_t operation_ids[
          NX_STATE_SCHEMA_OPERATION_METRIC_COUNT] = {
          NX_OP_STATE_SCHEMA_REGISTRY_INIT,
          NX_OP_STATE_SCHEMA_REGISTRY_ADD_DEFINITION,
          NX_OP_STATE_SCHEMA_REGISTRY_ADD_MIGRATION,
          NX_OP_STATE_SCHEMA_REGISTRY_SEAL,
          NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE,
          NX_OP_STATE_SCHEMA_REGISTRY_MANIFEST_DIGEST,
          NX_OP_STATE_SCHEMA_REGISTRY_LOOKUP,
          NX_OP_STATE_SCHEMA_REGISTRY_CLASSIFY,
          NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE_WRITE,
          NX_OP_STATE_STORE_CREATE_WITH_SCHEMA,
          NX_OP_STATE_STORE_UPDATE_WITH_SCHEMA,
          NX_OP_STATE_SCHEMA_REGISTRY_RESOLVE_MIGRATION};
      for (index = 0u; index < NX_STATE_SCHEMA_OPERATION_METRIC_COUNT; ++index)
        nx_operation_metrics_init(&registry->operation_metrics[index],
                                  operation_ids[index]);
      measurement.bytes_used = bound - 16u;
      nx_operation_metrics_record(
          &registry->operation_metrics[SCHEMA_METRIC_INIT], status,
          &measurement);
    }
  }
  scope.active = 0u;
  return status;
}

nx_status nx_state_schema_registry_add_definition(
    nx_state_schema_registry *registry,
    const nx_state_schema_definition *definition) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 8u, 2u, 0u, 0u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin(registry, SCHEMA_METRIC_ADD_DEFINITION,
                              NX_OP_STATE_SCHEMA_REGISTRY_ADD_DEFINITION,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_add_definition_impl(registry, definition);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_add_migration(
    nx_state_schema_registry *registry,
    const nx_state_schema_migration *migration) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 12u, 2u, 2u, 0u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin(registry, SCHEMA_METRIC_ADD_MIGRATION,
                              NX_OP_STATE_SCHEMA_REGISTRY_ADD_MIGRATION,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_add_migration_impl(registry, migration);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_seal(nx_state_schema_registry *registry) {
  nx_operation_scope scope;
  uint64_t bound;
  uint64_t definitions = registry == NULL ? 0u : registry->definition_count;
  uint64_t migrations = registry == NULL ? 0u : registry->migration_count;
  uint64_t version_steps = 0u;
  uint64_t term;
  uint32_t index;
  nx_status status = schema_registry_bound(registry, 32u, 4u, 4u, 4u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  if (registry != NULL) {
    for (index = 0u; index < registry->definition_count; ++index) {
      const nx_state_schema_definition *d = &registry->definitions[index];
      status = schema_bound_add(&version_steps,
          d->min_compatible_version >= d->min_supported_version
              ? (uint64_t)d->min_compatible_version - d->min_supported_version
              : 0u);
      if (status != NX_STATUS_OK)
        return status;
    }
  }
  status = schema_bound_product(version_steps, migrations, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(term, migrations, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(definitions, definitions, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(migrations, migrations, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin(registry, SCHEMA_METRIC_SEAL,
                              NX_OP_STATE_SCHEMA_REGISTRY_SEAL, bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_seal_impl(registry);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_validate(
    const nx_state_schema_registry *registry) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 32u, 4u, 4u, 4u, &bound);
  uint64_t version_steps = 0u;
  uint64_t term;
  uint32_t index;
  if (status != NX_STATUS_OK)
    return status;
  for (index = 0u; index < registry->definition_count; ++index) {
    const nx_state_schema_definition *d = &registry->definitions[index];
    status = schema_bound_add(&version_steps,
        (uint64_t)d->min_compatible_version - d->min_supported_version);
    if (status != NX_STATUS_OK)
      return status;
  }
  status = schema_bound_product(version_steps, registry->migration_count, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(term, registry->migration_count, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(registry->migration_count,
                                  registry->migration_count, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_VALIDATE,
                              NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_validate_impl(registry);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_manifest_digest(
    const nx_state_schema_registry *registry, nx_digest256 *digest) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 64u, 8u, 8u, 0u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_MANIFEST,
                              NX_OP_STATE_SCHEMA_REGISTRY_MANIFEST_DIGEST,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_manifest_digest_impl(registry, digest);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_lookup(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, const nx_state_schema_definition **definition) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 8u, 2u, 0u, 0u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_LOOKUP,
                              NX_OP_STATE_SCHEMA_REGISTRY_LOOKUP,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_lookup_impl(registry, type_id, schema_id, definition);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_classify(
    const nx_state_schema_registry *registry, const nx_state_node_spec *spec,
    nx_state_schema_compatibility *compatibility) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 16u, 4u, 0u, 0u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_CLASSIFY,
                              NX_OP_STATE_SCHEMA_REGISTRY_CLASSIFY,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_classify_impl(registry, spec, compatibility);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_validate_write(
    const nx_state_schema_registry *registry, const nx_state_node_spec *spec) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 48u, 8u, 8u, 4u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_VALIDATE_WRITE,
                              NX_OP_STATE_SCHEMA_REGISTRY_VALIDATE_WRITE,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_validate_write_impl(registry, spec);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_store_create_with_schema(
    const nx_state_schema_registry *registry, nx_state_store *store,
    const nx_state_node_spec *spec, nx_object_ref *created, nx_result *result) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 48u, 8u, 8u, 4u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_CREATE_WITH_SCHEMA,
                              NX_OP_STATE_STORE_CREATE_WITH_SCHEMA,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = state_store_create_with_schema_impl(registry, store, spec, created,
                                               result);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_store_update_with_schema(
    const nx_state_schema_registry *registry, nx_state_store *store,
    nx_object_ref reference, uint64_t expected_revision,
    const nx_state_node_spec *spec, nx_state_node *updated, nx_result *result) {
  nx_operation_scope scope;
  uint64_t bound;
  nx_status status = schema_registry_bound(registry, 48u, 8u, 8u, 4u, &bound);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_UPDATE_WITH_SCHEMA,
                              NX_OP_STATE_STORE_UPDATE_WITH_SCHEMA,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = state_store_update_with_schema_impl(
      registry, store, reference, expected_revision, spec, updated, result);
  return schema_scope_finish(&scope, status);
}

nx_status nx_state_schema_registry_resolve_migration(
    const nx_state_schema_registry *registry, uint64_t type_id,
    uint32_t schema_id, uint16_t from_version,
    const nx_state_schema_migration **path, uint32_t path_capacity,
    uint32_t *path_count) {
  nx_operation_scope scope;
  uint64_t bound;
  uint64_t term;
  nx_status status = schema_registry_bound(registry, 16u, 4u, 4u, 4u, &bound);
  if (status == NX_STATUS_OK)
    status = schema_bound_product(registry->migration_count,
                                  registry->migration_count, &term);
  if (status == NX_STATUS_OK)
    status = schema_bound_add(&bound, term);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_scope_begin((nx_state_schema_registry *)(void *)registry,
                              SCHEMA_METRIC_RESOLVE_MIGRATION,
                              NX_OP_STATE_SCHEMA_REGISTRY_RESOLVE_MIGRATION,
                              bound, &scope);
  if (status != NX_STATUS_OK)
    return status;
  status = schema_registry_resolve_migration_impl(
      registry, type_id, schema_id, from_version, path, path_capacity,
      path_count);
  return schema_scope_finish(&scope, status);
}
