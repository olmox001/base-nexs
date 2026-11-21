/* Product-owned independent NXOLMO regression test port. */
#include "nexs_schema.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCHEMA_THREADS UINT32_C(4)
#define SCHEMA_ITERATIONS UINT32_C(1000)

typedef struct schema_worker_context {
  const nx_state_schema_registry *registry;
  _Atomic uint32_t *errors;
  _Atomic uint32_t *completed;
} schema_worker_context;

static void *schema_reader(void *argument) {
  schema_worker_context *context = (schema_worker_context *)argument;
  uint32_t i;
  for (i = 0u; i < SCHEMA_ITERATIONS; ++i) {
    nx_state_node_spec spec;
    nx_state_schema_compatibility compatibility = 0u;
    const nx_state_schema_definition *definition = NULL;
    const nx_state_schema_migration *path[1] = {NULL};
    uint32_t path_count = 0u;
    nx_status status;

    memset(&spec, 0, sizeof(spec));
    spec.type_id = 77u;
    spec.schema_id = 9u;
    spec.schema_version = 3u;
    spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    spec.flags = NX_STATE_NODE_FLAG_IMMUTABLE;
    spec.payload_length = 64u;

    status = nx_state_schema_registry_lookup(context->registry, 77u, 9u,
                                             &definition);
    if (status != NX_STATUS_OK || definition == NULL ||
        definition->current_version != 3u ||
        nx_state_schema_registry_classify(context->registry, &spec,
                                          &compatibility) != NX_STATUS_OK ||
        compatibility != NX_STATE_SCHEMA_EXACT ||
        nx_state_schema_registry_resolve_migration(context->registry, 77u, 9u,
                                                   1u, path, 1u, &path_count) !=
            NX_STATUS_OK ||
        path_count != 1u || path[0] == NULL ||
        path[0]->migration_id != UINT64_C(0x77090001) ||
        nx_state_schema_registry_validate(context->registry) != NX_STATUS_OK) {
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
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_schema_migration migrations[1];
  nx_state_schema_definition definition;
  nx_state_schema_migration migration;
  _Atomic uint32_t errors;
  _Atomic uint32_t completed;
  schema_worker_context context;
  pthread_t threads[SCHEMA_THREADS];
  uint32_t i;

  memset(&definition, 0, sizeof(definition));
  definition.type_id = 77u;
  definition.schema_id = 9u;
  definition.min_supported_version = 1u;
  definition.min_compatible_version = 2u;
  definition.current_version = 3u;
  definition.allowed_node_flags = NX_STATE_NODE_FLAG_IMMUTABLE;
  definition.required_node_flags = NX_STATE_NODE_FLAG_IMMUTABLE;
  definition.max_payload_bytes = 1024u;

  memset(&migration, 0, sizeof(migration));
  migration.type_id = 77u;
  migration.schema_id = 9u;
  migration.from_version = 1u;
  migration.to_version = 3u;
  migration.migration_id = UINT64_C(0x77090001);
  migration.fuel_limit = 1000u;
  migration.max_input_bytes = 1024u;
  migration.max_output_bytes = 1024u;

  if (nx_state_schema_registry_init(&registry, definitions, 1u, migrations, 1u,
                                    UINT64_C(0x534348454d415453)) !=
          NX_STATUS_OK ||
      nx_state_schema_registry_add_definition(&registry, &definition) !=
          NX_STATUS_OK ||
      nx_state_schema_registry_add_migration(&registry, &migration) !=
          NX_STATUS_OK ||
      nx_state_schema_registry_seal(&registry) != NX_STATUS_OK)
    return 1;

  atomic_init(&errors, 0u);
  atomic_init(&completed, 0u);
  context.registry = &registry;
  context.errors = &errors;
  context.completed = &completed;
  for (i = 0u; i < SCHEMA_THREADS; ++i) {
    if (pthread_create(&threads[i], NULL, schema_reader, &context) != 0)
      return 1;
  }
  for (i = 0u; i < SCHEMA_THREADS; ++i)
    (void)pthread_join(threads[i], NULL);
  if (atomic_load_explicit(&errors, memory_order_relaxed) != 0u ||
      atomic_load_explicit(&completed, memory_order_relaxed) !=
          SCHEMA_THREADS * SCHEMA_ITERATIONS)
    return 1;
  (void)printf("registry schema immutable readers: PASS (%u operations)\n",
               atomic_load_explicit(&completed, memory_order_relaxed));
  return 0;
}
