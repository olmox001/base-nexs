/* NXOLMO registry algorithm port; one product-owned implementation. */
#include "nexs_migration.h"

#include "nexs_checked.h"
#include "nexs_crypto.h"
#include "nexs_operation_metrics.h"

#include <string.h>

typedef struct nx_migration_header {
  uint64_t type_id;
  uint32_t schema_id;
  uint16_t from_version;
  uint16_t to_version;
  uint64_t migration_id;
  uint64_t input_length;
  uint64_t output_length;
  uint32_t instruction_count;
} nx_migration_header;


static uint16_t migration_read_u16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t migration_read_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t migration_read_u64(const uint8_t *p) {
  return ((uint64_t)migration_read_u32(p) << 32) |
         (uint64_t)migration_read_u32(p + 4);
}

static int migration_span_overlaps(const void *a, size_t a_size,
                                   const void *b, size_t b_size) {
  if (a_size == 0u || b_size == 0u)
    return 0;
  return nx_pointer_ranges_overlap(a, a_size, b, b_size) != 0;
}

static nx_status migration_read_header(nx_const_span program,
                                       nx_migration_header *header) {
  const uint8_t *p;
  if (header == NULL || program.data == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  if (program.size < (size_t)NX_STATE_MIGRATION_HEADER_SIZE)
    return NX_STATUS_TRUNCATED;
  p = program.data;
  if (p[0] != NX_STATE_MIGRATION_MAGIC_0 ||
      p[1] != NX_STATE_MIGRATION_MAGIC_1 ||
      p[2] != NX_STATE_MIGRATION_MAGIC_2 ||
      p[3] != NX_STATE_MIGRATION_MAGIC_3)
    return NX_STATUS_MALFORMED;
  if (migration_read_u16(p + 4) != NX_STATE_MIGRATION_FORMAT_VERSION ||
      migration_read_u16(p + 6) != NX_STATE_MIGRATION_HEADER_SIZE)
    return NX_STATUS_VERSION_MISMATCH;
  if (migration_read_u32(p + 8) != 0u ||
      migration_read_u32(p + 56) != 0u)
    return NX_STATUS_MALFORMED;
  header->type_id = migration_read_u64(p + 12);
  header->schema_id = migration_read_u32(p + 20);
  header->from_version = migration_read_u16(p + 24);
  header->to_version = migration_read_u16(p + 26);
  header->migration_id = migration_read_u64(p + 28);
  header->input_length = migration_read_u64(p + 36);
  header->output_length = migration_read_u64(p + 44);
  header->instruction_count = migration_read_u32(p + 52);
  if (header->type_id == 0u || header->schema_id == 0u ||
      header->from_version == 0u ||
      header->to_version <= header->from_version ||
      header->migration_id == 0u)
    return NX_STATUS_MALFORMED;
  return NX_STATUS_OK;
}

static nx_status migration_decode_instruction(
    nx_const_span program, size_t cursor, uint8_t *opcode,
    uint32_t *destination, uint32_t *source, uint32_t *length) {
    const uint8_t *encoded;
    if (cursor > program.size ||
        program.size - cursor < (size_t)NX_STATE_MIGRATION_INSTRUCTION_SIZE)
        return NX_STATUS_TRUNCATED;
    encoded = program.data + cursor;
    *opcode = encoded[0];
    *destination = migration_read_u32(encoded + 4u);
    *source = migration_read_u32(encoded + 8u);
    *length = migration_read_u32(encoded + 12u);
    if (encoded[1] != 0u || encoded[2] != 0u || encoded[3] != 0u ||
        *length == 0u)
        return NX_STATUS_MALFORMED;
    return NX_STATUS_OK;
}

static nx_status migration_verify_copy_source(
    uint32_t source, uint32_t length, const nx_migration_header *header) {
    uint64_t source_end;
    return nx_range_validate((uint64_t)source, (uint64_t)length,
                             header->input_length, &source_end) == NX_STATUS_OK
               ? NX_STATUS_OK : NX_STATUS_LIMIT_EXCEEDED;
}

static nx_status migration_verify_instruction(
    const nx_state_schema_migration *migration, nx_const_span program,
    const nx_migration_header *header, size_t *cursor,
    uint64_t *output_cursor, uint64_t *fuel) {
    uint8_t opcode;
    uint32_t destination;
    uint32_t source;
    uint32_t length;
    uint64_t next_output;
    uint64_t next_fuel;
    size_t literal_end;
    nx_status status;
    status = migration_decode_instruction(
        program, *cursor, &opcode, &destination, &source, &length);
    if (status != NX_STATUS_OK)
        return status;
    if ((uint64_t)destination != *output_cursor)
        return NX_STATUS_MALFORMED;
    if (nx_u64_add(*output_cursor, (uint64_t)length, &next_output) !=
            NX_STATUS_OK || next_output > header->output_length)
        return NX_STATUS_LIMIT_EXCEEDED;
    if (nx_u64_add(*fuel, (uint64_t)length, &next_fuel) != NX_STATUS_OK ||
        next_fuel > migration->fuel_limit)
        return NX_STATUS_BUDGET_EXHAUSTED;
    *fuel = next_fuel;
    if (opcode == NX_STATE_MIGRATION_OP_COPY) {
        status = migration_verify_copy_source(source, length, header);
        if (status != NX_STATUS_OK)
            return status;
    } else if (opcode == NX_STATE_MIGRATION_OP_ZERO) {
        if (source != 0u)
            return NX_STATUS_MALFORMED;
    } else if (opcode == NX_STATE_MIGRATION_OP_LITERAL) {
        if (source != 0u ||
            nx_size_add(*cursor,
                        (size_t)NX_STATE_MIGRATION_INSTRUCTION_SIZE,
                        &literal_end) != NX_STATUS_OK ||
            nx_size_add(literal_end, (size_t)length, &literal_end) !=
                NX_STATUS_OK || literal_end > program.size)
            return NX_STATUS_TRUNCATED;
        *cursor = literal_end;
        *output_cursor = next_output;
        return NX_STATUS_OK;
    } else {
        return NX_STATUS_NOT_SUPPORTED;
    }
    *cursor += (size_t)NX_STATE_MIGRATION_INSTRUCTION_SIZE;
    *output_cursor = next_output;
    return NX_STATUS_OK;
}

static nx_status migration_verify(
    const nx_state_schema_migration *migration, nx_const_span program,
    const nx_migration_header *header, uint64_t *fuel_out) {
    size_t cursor = (size_t)NX_STATE_MIGRATION_HEADER_SIZE;
    size_t instruction_bytes;
    size_t maximum_program_size;
    uint64_t output_cursor = 0u;
    uint64_t fuel;
    uint32_t index;
    nx_status status;
    if (migration == NULL || header == NULL || fuel_out == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (migration->type_id != header->type_id ||
        migration->schema_id != header->schema_id ||
        migration->from_version != header->from_version ||
        migration->to_version != header->to_version ||
        migration->migration_id != header->migration_id)
        return NX_STATUS_VERSION_MISMATCH;
    if (migration->fuel_limit == 0u || migration->max_input_bytes == 0u ||
        migration->max_output_bytes == 0u || migration->flags != 0u ||
        migration->reserved != 0u)
        return NX_STATUS_CORRUPT;
    if (header->input_length > migration->max_input_bytes ||
        header->output_length > migration->max_output_bytes)
        return NX_STATUS_LIMIT_EXCEEDED;
    if (header->output_length > (uint64_t)SIZE_MAX ||
        nx_size_mul((size_t)header->instruction_count,
                    (size_t)NX_STATE_MIGRATION_INSTRUCTION_SIZE,
                    &instruction_bytes) != NX_STATUS_OK ||
        nx_size_add((size_t)NX_STATE_MIGRATION_HEADER_SIZE,
                    instruction_bytes, &maximum_program_size) != NX_STATUS_OK ||
        nx_size_add(maximum_program_size, (size_t)header->output_length,
                    &maximum_program_size) != NX_STATUS_OK ||
        program.size > maximum_program_size)
        return NX_STATUS_LIMIT_EXCEEDED;
    if (header->instruction_count == 0u && header->output_length != 0u)
        return NX_STATUS_MALFORMED;
    fuel = (uint64_t)header->instruction_count;
    for (index = 0u; index < header->instruction_count; ++index) {
        status = migration_verify_instruction(
            migration, program, header, &cursor, &output_cursor, &fuel);
        if (status != NX_STATUS_OK)
            return status;
    }
    if (cursor != program.size || output_cursor != header->output_length)
        return NX_STATUS_MALFORMED;
    *fuel_out = fuel;
    return NX_STATUS_OK;
}

static nx_status migration_validate_ranges(
    const nx_state_schema_migration *migration, nx_const_span program,
    nx_const_span input, nx_mut_span scratch, nx_mut_span output,
    const nx_digest256 *output_digest) {
    if (migration_span_overlaps(migration, sizeof(*migration), program.data,
                                program.size) ||
        migration_span_overlaps(migration, sizeof(*migration), input.data,
                                input.size) ||
        migration_span_overlaps(migration, sizeof(*migration), scratch.data,
                                scratch.size) ||
        migration_span_overlaps(migration, sizeof(*migration), output.data,
                                output.size) ||
        migration_span_overlaps(program.data, program.size, input.data,
                                input.size) ||
        migration_span_overlaps(program.data, program.size, scratch.data,
                                scratch.size) ||
        migration_span_overlaps(program.data, program.size, output.data,
                                output.size) ||
        migration_span_overlaps(input.data, input.size, scratch.data,
                                scratch.size) ||
        migration_span_overlaps(input.data, input.size, output.data,
                                output.size) ||
        migration_span_overlaps(scratch.data, scratch.size, output.data,
                                output.size) ||
        migration_span_overlaps(output_digest, sizeof(*output_digest),
                                program.data, program.size) ||
        migration_span_overlaps(output_digest, sizeof(*output_digest),
                                input.data, input.size) ||
        migration_span_overlaps(output_digest, sizeof(*output_digest),
                                scratch.data, scratch.size) ||
        migration_span_overlaps(output_digest, sizeof(*output_digest),
                                output.data, output.size) ||
        migration_span_overlaps(output_digest, sizeof(*output_digest),
                                migration, sizeof(*migration)))
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status migration_verify_program(
    const nx_state_schema_migration *migration, nx_const_span program,
    nx_const_span input, nx_mut_span scratch, nx_mut_span output,
    nx_migration_header *header, uint64_t *verified_fuel) {
    static const uint8_t zero_digest[32] = {0u};
    nx_digest256 actual_digest;
    nx_status status = migration_read_header(program, header);
    if (status != NX_STATUS_OK)
        return status;
    if (header->input_length != (uint64_t)input.size)
        return NX_STATUS_VERSION_MISMATCH;
    status = nx_sha256_compute(program, &actual_digest);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_crypto_constant_time_equal(migration->program_digest.bytes,
                                      zero_digest, sizeof(zero_digest)))
        return NX_STATUS_UNAVAILABLE;
    if (!nx_crypto_constant_time_equal(
            actual_digest.bytes, migration->program_digest.bytes,
            sizeof(actual_digest.bytes)))
        return NX_STATUS_INTEGRITY_FAILURE;
    status = migration_verify(migration, program, header, verified_fuel);
    if (status != NX_STATUS_OK)
        return status;
    if (header->output_length > (uint64_t)scratch.size ||
        header->output_length > (uint64_t)output.size)
        return NX_STATUS_NO_SPACE;
    return NX_STATUS_OK;
}

static nx_status migration_execute_instruction(
    nx_const_span program, nx_const_span input, nx_mut_span scratch,
    nx_operation_scope *scope, size_t *cursor, uint64_t *output_cursor) {
    const uint8_t *encoded = program.data + *cursor;
    uint8_t opcode = encoded[0];
    uint32_t source = migration_read_u32(encoded + 8u);
    uint32_t length = migration_read_u32(encoded + 12u);
    size_t instruction_size = (size_t)NX_STATE_MIGRATION_INSTRUCTION_SIZE;
    nx_status status = nx_operation_scope_consume(
        scope, UINT64_C(1) + (uint64_t)length);
    if (status != NX_STATUS_OK)
        return status;
    if (opcode == NX_STATE_MIGRATION_OP_COPY)
        memcpy(scratch.data + (size_t)*output_cursor,
               input.data + (size_t)source, (size_t)length);
    else if (opcode == NX_STATE_MIGRATION_OP_ZERO)
        memset(scratch.data + (size_t)*output_cursor, 0, (size_t)length);
    else {
        memcpy(scratch.data + (size_t)*output_cursor,
               program.data + *cursor + instruction_size, (size_t)length);
        instruction_size += (size_t)length;
    }
    *output_cursor += (uint64_t)length;
    *cursor += instruction_size;
    return NX_STATUS_OK;
}

static nx_status migration_execute_instructions(
    nx_const_span program, nx_const_span input, nx_mut_span scratch,
    const nx_migration_header *header, nx_operation_scope *scope) {
    size_t cursor = (size_t)NX_STATE_MIGRATION_HEADER_SIZE;
    uint64_t output_cursor = 0u;
    uint32_t index;
    for (index = 0u; index < header->instruction_count; ++index) {
        nx_status status = migration_execute_instruction(
            program, input, scratch, scope, &cursor, &output_cursor);
        if (status != NX_STATUS_OK)
            return status;
    }
    return output_cursor == header->output_length
               ? NX_STATUS_OK : NX_STATUS_INTERNAL;
}

static nx_status migration_publish_output(
    nx_mut_span scratch, nx_mut_span output, nx_digest256 *output_digest,
    const nx_migration_header *header, nx_operation_scope *scope,
    uint64_t migration_id) {
    nx_digest256 digest;
    nx_status status = nx_sha256_compute(
        (nx_const_span){scratch.data, (size_t)header->output_length}, &digest);
    status = nx_operation_scope_finish_ex(
        scope, status, NX_RESULT_DOMAIN_REGISTRY, header->instruction_count,
        migration_id, header->output_length, NULL);
    if (status != NX_STATUS_OK)
        return status;
    if (header->output_length != 0u)
        memcpy(output.data, scratch.data, (size_t)header->output_length);
    *output_digest = digest;
    return NX_STATUS_OK;
}

nx_status nx_state_migration_execute(
    const nx_state_schema_migration *migration, nx_const_span program,
    nx_const_span input, nx_mut_span scratch, nx_mut_span output,
    nx_digest256 *output_digest) {
    nx_migration_header header;
    nx_operation_scope scope;
    uint64_t verified_fuel = 0u;
    uint64_t execution_fuel_limit;
    nx_status status;
    if (migration == NULL || output_digest == NULL ||
        ((uintptr_t)migration %
         (uintptr_t)_Alignof(nx_state_schema_migration)) != 0u ||
        ((uintptr_t)output_digest % (uintptr_t)_Alignof(nx_digest256)) != 0u ||
        nx_const_span_validate(program, program.size) != NX_STATUS_OK ||
        nx_const_span_validate(input, input.size) != NX_STATUS_OK ||
        nx_mut_span_validate(scratch, scratch.size) != NX_STATUS_OK ||
        nx_mut_span_validate(output, output.size) != NX_STATUS_OK)
        return NX_STATUS_INVALID_ARGUMENT;
    status = migration_validate_ranges(
        migration, program, input, scratch, output, output_digest);
    if (status == NX_STATUS_OK)
        status = migration_verify_program(
            migration, program, input, scratch, output, &header,
            &verified_fuel);
    if (status != NX_STATUS_OK)
        return status;
    execution_fuel_limit = verified_fuel == 0u ? 1u : verified_fuel;
    status = nx_operation_scope_begin(
        &scope, NULL, NX_OP_STATE_MIGRATION_EXECUTE,
        execution_fuel_limit, UINT64_MAX);
    if (status != NX_STATUS_OK)
        return status;
    status = migration_execute_instructions(
        program, input, scratch, &header, &scope);
    if (status != NX_STATUS_OK)
        return nx_operation_scope_finish_ex(
            &scope, status, NX_RESULT_DOMAIN_REGISTRY,
            header.instruction_count, migration->migration_id,
            header.output_length, NULL);
    return migration_publish_output(
        scratch, output, output_digest, &header, &scope,
        migration->migration_id);
}
