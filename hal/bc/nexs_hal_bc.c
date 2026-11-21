/* Base-NEXS HALB execution, one product implementation.
 * No privileged opcode may invoke hardware without real NXEC authority.
 * Nonblocking compute uses an externally admitted NXOLMO fuel budget.
 */
#include "nexs_hal_bc.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int halbc_is_hardware_opcode(uint8_t opcode) {
    return opcode == HALO_OPEN || opcode == HALO_CLOSE ||
           opcode == HALO_READ || opcode == HALO_WRITE ||
           opcode == HALO_SEEK || opcode == HALO_STAT ||
           opcode == HALO_MOUNT || opcode == HALO_UMOUNT ||
           opcode == HALO_CTL || opcode == HALO_CALL;
}

static int halbc_is_compute_opcode(uint8_t opcode) {
    return opcode == HALO_NOP || opcode == HALO_HALT ||
           opcode == HALO_PUSH || opcode == HALO_POP ||
           opcode == HALO_JMP || opcode == HALO_JZ;
}

static int64_t halbc_signed_u16(uint16_t raw) {
    return raw <= (uint16_t)INT16_MAX
               ? (int64_t)raw : (int64_t)raw - INT64_C(65536);
}

static int64_t halbc_signed_u32(uint32_t raw) {
    return raw <= (uint32_t)INT32_MAX
               ? (int64_t)raw : (int64_t)raw - INT64_C(4294967296);
}

static nx_status halbc_offset(const HalBcState *state, size_t current,
                               int64_t relative, size_t *destination) {
    if (current > (size_t)INT64_MAX)
        return NX_STATUS_OVERFLOW;
    int64_t target = (int64_t)current + relative;
    if (target < (int64_t)HALBC_HEADER_SIZE ||
        (uint64_t)target >= (uint64_t)state->bytecode.size)
        return NX_STATUS_MALFORMED;
    *destination = (size_t)target;
    return NX_STATUS_OK;
}

nx_status halbc_init(HalBcState *state, nx_const_span program,
                     int64_t *stack, size_t stack_capacity) {
    size_t stack_bytes = 0u;
    if (state == NULL || stack == NULL || stack_capacity == 0u ||
        ((uintptr_t)state % (uintptr_t)_Alignof(HalBcState)) != 0u ||
        ((uintptr_t)stack % (uintptr_t)_Alignof(int64_t)) != 0u ||
        nx_const_span_validate(program, HALBC_HEADER_SIZE) != NX_STATUS_OK)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_size_mul(stack_capacity, sizeof(*stack), &stack_bytes) !=
        NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    if (nx_pointer_ranges_overlap(state, sizeof(*state),
                                  program.data, program.size) != 0 ||
        nx_pointer_ranges_overlap(state, sizeof(*state),
                                  stack, stack_bytes) != 0 ||
        nx_pointer_ranges_overlap(stack, stack_bytes,
                                  program.data, program.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (memcmp(program.data, HALBC_MAGIC, 4u) != 0 ||
        program.data[4] != HALBC_VERSION ||
        program.data[5] != HALBC_FLAG_UTF8)
        return NX_STATUS_VERSION_MISMATCH;
    size_t payload = (size_t)program.data[6] |
                     ((size_t)program.data[7] << 8u);
    if (payload != program.size - HALBC_HEADER_SIZE)
        return NX_STATUS_MALFORMED;
    HalBcState fresh = {program, stack, stack_capacity,
                        0u, HALBC_HEADER_SIZE, 0u};
    *state = fresh;
    return NX_STATUS_OK;
}

static nx_status halbc_step_push(HalBcState *state) {
    size_t ip = state->ip;
    if (state->bytecode.size - ip < 6u ||
        state->bytecode.data[ip + 1u] != UINT8_C(0x80))
        return NX_STATUS_MALFORMED;
    if (state->stack_depth >= state->stack_capacity)
        return NX_STATUS_NO_SPACE;
    uint32_t raw = (uint32_t)state->bytecode.data[ip + 2u] |
                   ((uint32_t)state->bytecode.data[ip + 3u] << 8u) |
                   ((uint32_t)state->bytecode.data[ip + 4u] << 16u) |
                   ((uint32_t)state->bytecode.data[ip + 5u] << 24u);
    state->stack[state->stack_depth] = halbc_signed_u32(raw);
    state->stack_depth += 1u;
    state->ip += 6u;
    return NX_STATUS_OK;
}

static nx_status halbc_step_jump(HalBcState *state, int conditional) {
    size_t ip = state->ip;
    if (state->bytecode.size - ip < 3u ||
        (conditional != 0 && state->stack_depth == 0u))
        return NX_STATUS_MALFORMED;
    uint32_t raw_wide = (uint32_t)state->bytecode.data[ip + 1u] |
                        ((uint32_t)state->bytecode.data[ip + 2u] << 8u);
    uint16_t raw = (uint16_t)raw_wide;
    size_t destination = ip + 3u;
    int taken = conditional == 0 ||
                state->stack[state->stack_depth - 1u] == 0;
    if (taken != 0) {
        nx_status status = halbc_offset(
            state, destination, halbc_signed_u16(raw), &destination);
        if (status != NX_STATUS_OK)
            return status;
    } else if (destination >= state->bytecode.size) {
        return NX_STATUS_MALFORMED;
    }
    if (conditional != 0)
        state->stack_depth -= 1u;
    state->ip = destination;
    return NX_STATUS_OK;
}

static nx_status halbc_step(HalBcState *state) {
    uint8_t opcode = state->bytecode.data[state->ip];
    if (opcode == HALO_NOP) {
        state->ip += 1u;
        return NX_STATUS_OK;
    }
    if (opcode == HALO_HALT) {
        state->ip += 1u;
        state->halted = 1u;
        return NX_STATUS_OK;
    }
    if (opcode == HALO_PUSH)
        return halbc_step_push(state);
    if (opcode == HALO_POP) {
        if (state->stack_depth == 0u)
            return NX_STATUS_MALFORMED;
        state->stack_depth -= 1u;
        state->ip += 1u;
        return NX_STATUS_OK;
    }
    if (opcode == HALO_JMP || opcode == HALO_JZ)
        return halbc_step_jump(state, opcode == HALO_JZ);
    return NX_STATUS_MALFORMED;
}

nx_status halbc_run(HalBcState *state, nx_operation_budget *budget) {
    if (state == NULL || budget == NULL ||
        nx_pointer_ranges_overlap(state, sizeof(*state),
                                  budget, sizeof(*budget)) != 0 ||
        state->bytecode.data == NULL || state->stack == NULL ||
        state->stack_capacity == 0u ||
        state->stack_depth > state->stack_capacity)
        return NX_STATUS_INVALID_ARGUMENT;
    if (state->halted != 0u)
        return NX_STATUS_OK;
    uint64_t admitted = budget->fuel_remaining;
    for (uint64_t i = 0u; i < admitted; ++i) {
        if (state->ip < HALBC_HEADER_SIZE ||
            state->ip >= state->bytecode.size)
            return NX_STATUS_MALFORMED;
        uint8_t opcode = state->bytecode.data[state->ip];
        if (halbc_is_hardware_opcode(opcode) != 0)
            return NX_STATUS_ACCESS_DENIED;
        if (halbc_is_compute_opcode(opcode) == 0)
            return NX_STATUS_MALFORMED;
        nx_status status = nx_operation_budget_consume(budget, UINT64_C(1));
        if (status != NX_STATUS_OK)
            return status;
        status = halbc_step(state);
        if (status != NX_STATUS_OK)
            return status;
        if (state->halted != 0u)
            return NX_STATUS_OK;
        if (state->ip >= state->bytecode.size)
            return NX_STATUS_MALFORMED;
    }
    return NX_STATUS_BUDGET_EXHAUSTED;
}

nx_status halbc_asm_init(HalBcAssembler *assembler, nx_mut_span output) {
    if (assembler == NULL ||
        nx_mut_span_validate(output, HALBC_HEADER_SIZE) != NX_STATUS_OK ||
        nx_pointer_ranges_overlap(assembler, sizeof(*assembler),
                                  output.data, output.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    memcpy(output.data, HALBC_MAGIC, 4u);
    output.data[4] = HALBC_VERSION;
    output.data[5] = HALBC_FLAG_UTF8;
    output.data[6] = 0u;
    output.data[7] = 0u;
    assembler->output = output;
    assembler->length = HALBC_HEADER_SIZE;
    return NX_STATUS_OK;
}

static nx_status halbc_asm_reserve(const HalBcAssembler *assembler,
                                   size_t bytes, size_t *end) {
    if (assembler == NULL || end == NULL ||
        assembler->output.data == NULL ||
        assembler->length < HALBC_HEADER_SIZE)
        return NX_STATUS_INVALID_ARGUMENT;
    nx_status status = nx_size_add(assembler->length, bytes, end);
    if (status != NX_STATUS_OK)
        return status;
    if (*end > assembler->output.size ||
        *end - HALBC_HEADER_SIZE > (size_t)UINT16_MAX)
        return NX_STATUS_NO_SPACE;
    return NX_STATUS_OK;
}

nx_status halbc_asm_emit_simple(HalBcAssembler *assembler, uint8_t opcode) {
    if (opcode != HALO_NOP && opcode != HALO_HALT && opcode != HALO_POP)
        return NX_STATUS_ACCESS_DENIED;
    size_t end = 0u;
    nx_status status = halbc_asm_reserve(assembler, 1u, &end);
    if (status != NX_STATUS_OK)
        return status;
    assembler->output.data[assembler->length] = opcode;
    assembler->length = end;
    return NX_STATUS_OK;
}

nx_status halbc_asm_emit_push(HalBcAssembler *assembler, int32_t value) {
    size_t end = 0u;
    nx_status status = halbc_asm_reserve(assembler, 6u, &end);
    if (status != NX_STATUS_OK)
        return status;
    size_t pos = assembler->length;
    uint32_t raw = (uint32_t)value;
    assembler->output.data[pos] = HALO_PUSH;
    assembler->output.data[pos + 1u] = UINT8_C(0x80);
    for (size_t i = 0u; i < 4u; ++i)
        assembler->output.data[pos + i + 2u] =
            (uint8_t)(raw >> (8u * (unsigned int)i));
    assembler->length = end;
    return NX_STATUS_OK;
}

nx_status halbc_asm_emit_jump(HalBcAssembler *assembler, int16_t offset,
                              uint32_t conditional) {
    if (conditional > 1u)
        return NX_STATUS_INVALID_ARGUMENT;
    size_t end = 0u;
    nx_status status = halbc_asm_reserve(assembler, 3u, &end);
    if (status != NX_STATUS_OK)
        return status;
    uint16_t raw = (uint16_t)offset;
    size_t pos = assembler->length;
    assembler->output.data[pos] = conditional != 0u ? HALO_JZ : HALO_JMP;
    assembler->output.data[pos + 1u] = (uint8_t)raw;
    assembler->output.data[pos + 2u] = (uint8_t)(raw >> 8u);
    assembler->length = end;
    return NX_STATUS_OK;
}

nx_status halbc_asm_finish(HalBcAssembler *assembler, size_t *written) {
    if (written == NULL || assembler == NULL ||
        nx_pointer_ranges_overlap(written, sizeof(*written),
                                  assembler, sizeof(*assembler)) != 0 ||
        nx_pointer_ranges_overlap(written, sizeof(*written),
                                  assembler->output.data,
                                  assembler->output.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    size_t end = 0u;
    nx_status status = halbc_asm_reserve(assembler, 0u, &end);
    if (status != NX_STATUS_OK)
        return status;
    uint16_t payload = (uint16_t)(end - HALBC_HEADER_SIZE);
    assembler->output.data[6] = (uint8_t)payload;
    assembler->output.data[7] = (uint8_t)(payload >> 8u);
    *written = end;
    return NX_STATUS_OK;
}
