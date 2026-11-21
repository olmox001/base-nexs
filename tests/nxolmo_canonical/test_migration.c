/* Product-owned independent NXOLMO regression test port. */
#include "nexs_migration.h"

#include "nexs_crypto.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #x); \
  return 1; \
} } while (0)

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}
static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}
static void put_u64(uint8_t *p, uint64_t v) {
  put_u32(p, (uint32_t)(v >> 32));
  put_u32(p + 4, (uint32_t)v);
}
static size_t instruction(uint8_t *p, uint8_t op, uint32_t dst,
                          uint32_t src, const uint8_t *literal,
                          uint32_t length) {
  p[0] = op; p[1] = 0u; p[2] = 0u; p[3] = 0u;
  put_u32(p + 4, dst); put_u32(p + 8, src); put_u32(p + 12, length);
  if (op == NX_STATE_MIGRATION_OP_LITERAL)
    memcpy(p + 16, literal, (size_t)length);
  return (size_t)16u + (op == NX_STATE_MIGRATION_OP_LITERAL
                            ? (size_t)length : 0u);
}
static size_t make_program(uint8_t *p) {
  static const uint8_t xy[2] = {'X', 'Y'};
  static const uint8_t bang[1] = {'!'};
  size_t n = (size_t)NX_STATE_MIGRATION_HEADER_SIZE;
  memset(p, 0, 128u);
  p[0] = NX_STATE_MIGRATION_MAGIC_0;
  p[1] = NX_STATE_MIGRATION_MAGIC_1;
  p[2] = NX_STATE_MIGRATION_MAGIC_2;
  p[3] = NX_STATE_MIGRATION_MAGIC_3;
  put_u16(p + 4, NX_STATE_MIGRATION_FORMAT_VERSION);
  put_u16(p + 6, NX_STATE_MIGRATION_HEADER_SIZE);
  put_u64(p + 12, UINT64_C(0x100));
  put_u32(p + 20, 7u);
  put_u16(p + 24, 1u);
  put_u16(p + 26, 2u);
  put_u64(p + 28, UINT64_C(0xabc));
  put_u64(p + 36, 3u);
  put_u64(p + 44, 6u);
  put_u32(p + 52, 4u);
  n += instruction(p + n, NX_STATE_MIGRATION_OP_COPY, 0u, 0u, NULL, 1u);
  n += instruction(p + n, NX_STATE_MIGRATION_OP_LITERAL, 1u, 0u, xy, 2u);
  n += instruction(p + n, NX_STATE_MIGRATION_OP_COPY, 3u, 1u, NULL, 2u);
  n += instruction(p + n, NX_STATE_MIGRATION_OP_LITERAL, 5u, 0u, bang, 1u);
  return n;
}

static int test_sealed_registry_descriptor(void) {
  uint8_t program_bytes[128];
  static const uint8_t input_bytes[3] = {'a', 'b', 'c'};
  static const uint8_t expected_output[6] = {'a', 'X', 'Y', 'b', 'c', '!'};
  uint8_t scratch_bytes[16];
  uint8_t output_bytes[16];
  nx_state_schema_registry registry;
  nx_state_schema_definition definitions[1];
  nx_state_schema_migration migrations[1];
  nx_state_schema_definition definition;
  nx_state_schema_migration migration;
  nx_digest256 output_digest;
  nx_digest256 expected_digest;
  const nx_state_schema_migration *path[1] = {NULL};
  uint32_t path_count = 0u;
  size_t program_size = make_program(program_bytes);
  nx_const_span program = {program_bytes, program_size};
  nx_const_span input = {input_bytes, sizeof(input_bytes)};

  memset(&definition, 0, sizeof(definition));
  definition.type_id = UINT64_C(0x100);
  definition.schema_id = 7u;
  definition.min_supported_version = 1u;
  definition.min_compatible_version = 2u;
  definition.current_version = 2u;
  definition.max_payload_bytes = 16u;

  memset(&migration, 0, sizeof(migration));
  migration.type_id = UINT64_C(0x100);
  migration.schema_id = 7u;
  migration.from_version = 1u;
  migration.to_version = 2u;
  migration.migration_id = UINT64_C(0xabc);
  migration.fuel_limit = 10u;
  migration.max_input_bytes = 16u;
  migration.max_output_bytes = 16u;
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);

  CHECK(nx_state_schema_registry_init(&registry, definitions, 1u, migrations,
                                      1u, UINT64_C(0x4d49475241544531)) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_definition(&registry, &definition) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_add_migration(&registry, &migration) ==
        NX_STATUS_OK);
  CHECK(nx_state_schema_registry_seal(&registry) == NX_STATUS_OK);
  CHECK(nx_state_schema_registry_resolve_migration(
            &registry, UINT64_C(0x100), 7u, 1u, path, 1u, &path_count) ==
        NX_STATUS_OK);
  CHECK(path_count == 1u && path[0] != NULL);
  CHECK(nx_state_migration_execute(
            path[0], program, input,
            (nx_mut_span){scratch_bytes, sizeof(scratch_bytes)},
            (nx_mut_span){output_bytes, sizeof(output_bytes)},
            &output_digest) == NX_STATUS_OK);
  CHECK(memcmp(output_bytes, expected_output, sizeof(expected_output)) == 0);
  CHECK(nx_sha256_compute((nx_const_span){expected_output,
                                          sizeof(expected_output)},
                          &expected_digest) == NX_STATUS_OK);
  CHECK(memcmp(output_digest.bytes, expected_digest.bytes, 32u) == 0);
  return 0;
}

int main(void) {
  uint8_t program_bytes[128];
  const uint8_t input_bytes[3] = {'a', 'b', 'c'};
  uint8_t scratch_bytes[16];
  uint8_t output_bytes[16];
  nx_state_schema_migration migration = {
      .type_id = UINT64_C(0x100), .schema_id = 7u,
      .from_version = 1u, .to_version = 2u,
      .migration_id = UINT64_C(0xabc), .fuel_limit = 10u,
      .max_input_bytes = 16u, .max_output_bytes = 16u,
      .flags = 0u, .reserved = 0u};
  nx_digest256 output_digest;
  nx_digest256 expected_digest;
  union {
    nx_digest256 digest;
    uint8_t bytes[sizeof(nx_digest256)];
  } alias_storage;
  size_t program_size = make_program(program_bytes);
  nx_const_span program = {program_bytes, program_size};
  nx_const_span input = {input_bytes, sizeof(input_bytes)};
  nx_mut_span scratch = {scratch_bytes, sizeof(scratch_bytes)};
  nx_mut_span output = {output_bytes, sizeof(output_bytes)};
  static const uint8_t expected_output[6] = {'a', 'X', 'Y', 'b', 'c', '!'};

  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  memset(output_bytes, 0xa5, sizeof(output_bytes));
  memset(scratch_bytes, 0x5a, sizeof(scratch_bytes));
  memset(&output_digest, 0x3c, sizeof(output_digest));
  { nx_status st = nx_state_migration_execute(
        &migration, program, input, scratch, output, &output_digest);
    if (st != NX_STATUS_OK) fprintf(stderr, "migration status=%u\n", st);
    CHECK(st == NX_STATUS_OK);
  }
  CHECK(memcmp(output_bytes, expected_output, sizeof(expected_output)) == 0);
  CHECK(nx_sha256_compute((nx_const_span){expected_output,
                                          sizeof(expected_output)},
                          &expected_digest) == NX_STATUS_OK);
  CHECK(memcmp(output_digest.bytes, expected_digest.bytes, 32u) == 0);

  /* Digest mismatch and malformed bytecode leave destination and digest alone. */
  memset(output_bytes, 0xa5, sizeof(output_bytes));
  memset(&output_digest, 0x3c, sizeof(output_digest));
  program_bytes[program_size - 1u] ^= 1u;
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_INTEGRITY_FAILURE);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);
  program_bytes[program_size - 1u] ^= 1u;

  memset(&migration.program_digest, 0, sizeof(migration.program_digest));
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_UNAVAILABLE);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);

  /* Fuel is instruction count plus bytes transformed. */
  migration.fuel_limit = 9u;
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_BUDGET_EXHAUSTED);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);
  migration.fuel_limit = 10u;

  /* Reject a source range beyond the input before touching destination. */
  put_u32(program_bytes + NX_STATE_MIGRATION_HEADER_SIZE + 8u, 3u);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_LIMIT_EXCEEDED);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);
  put_u32(program_bytes + NX_STATE_MIGRATION_HEADER_SIZE + 8u, 0u);

  /* Header and instruction version/flag/reserved checks. */
  put_u32(program_bytes + 8u, 1u);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_MALFORMED);
  put_u32(program_bytes + 8u, 0u);
  put_u16(program_bytes + 4u, 2u);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_VERSION_MISMATCH);
  put_u16(program_bytes + 4u, NX_STATE_MIGRATION_FORMAT_VERSION);
  program_bytes[NX_STATE_MIGRATION_HEADER_SIZE + 1u] = 1u;
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_MALFORMED);
  program_bytes[NX_STATE_MIGRATION_HEADER_SIZE + 1u] = 0u;

  /* Identity mismatch, malformed destination coverage and unsupported opcodes. */
  put_u32(program_bytes + 20u, 8u);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_VERSION_MISMATCH);
  put_u32(program_bytes + 20u, 7u);

  put_u32(program_bytes + NX_STATE_MIGRATION_HEADER_SIZE + 4u, 1u);
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_MALFORMED);
  put_u32(program_bytes + NX_STATE_MIGRATION_HEADER_SIZE + 4u, 0u);

  program_bytes[NX_STATE_MIGRATION_HEADER_SIZE] = 99u;
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_NOT_SUPPORTED);
  program_bytes[NX_STATE_MIGRATION_HEADER_SIZE] = NX_STATE_MIGRATION_OP_COPY;

  /* A truncated literal and trailing data are rejected by exact framing. */
  CHECK(nx_sha256_compute((nx_const_span){program_bytes, program_size - 1u},
                          &migration.program_digest) == NX_STATUS_OK);
  { nx_status st = nx_state_migration_execute(
        &migration, (nx_const_span){program_bytes, program_size - 1u},
        input, scratch, output, &output_digest);
    if (st != NX_STATUS_TRUNCATED) fprintf(stderr, "truncated status=%u\n", st);
    CHECK(st == NX_STATUS_TRUNCATED);
  }
  program_bytes[program_size] = 0u;
  CHECK(nx_sha256_compute((nx_const_span){program_bytes, program_size + 1u},
                          &migration.program_digest) == NX_STATUS_OK);
  CHECK(nx_state_migration_execute(
            &migration, (nx_const_span){program_bytes, program_size + 1u},
            input, scratch, output,
            &output_digest) == NX_STATUS_MALFORMED);

  /* Capacity and alias failures cannot publish destination or digest. */
  CHECK(nx_sha256_compute(program, &migration.program_digest) == NX_STATUS_OK);
  output.size = 5u;
  CHECK(nx_state_migration_execute(&migration, program, input, scratch, output,
                                   &output_digest) == NX_STATUS_NO_SPACE);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);
  output.size = sizeof(output_bytes);
  CHECK(nx_state_migration_execute(
            &migration, program, input, scratch,
            (nx_mut_span){scratch_bytes, sizeof(scratch_bytes)},
            &output_digest) == NX_STATUS_INVALID_ARGUMENT);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);

  /* Program/input, descriptor/output and digest/output aliases are rejected. */
  CHECK(nx_state_migration_execute(
            &migration, program,
            (nx_const_span){program_bytes, sizeof(input_bytes)},
            scratch, output, &output_digest) == NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_migration_execute(
            &migration, program, input, scratch,
            (nx_mut_span){(uint8_t *)(void *)&migration, sizeof(migration)},
            &output_digest) == NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_state_migration_execute(
            &migration, program, input, scratch,
            (nx_mut_span){alias_storage.bytes, sizeof(alias_storage.bytes)},
            &alias_storage.digest) == NX_STATUS_INVALID_ARGUMENT);
  CHECK(output_bytes[0] == 0xa5u && output_digest.bytes[0] == 0x3cu);

  CHECK(test_sealed_registry_descriptor() == 0);
  puts("PASS schema migration executor");
  return 0;
}
