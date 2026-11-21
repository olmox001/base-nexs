/* Base-NEXS HALB: bounded, caller-owned and hardware-denied bytecode core.
 * Canonical wire profile v1; NXEC device endpoints are NOT admitted here.
 * No global device table, recursive execution or target-specific libc.
 */
#ifndef NEXS_HAL_BC_H
#define NEXS_HAL_BC_H

#include "nexs_budget.h"
#include "nexs_checked.h"
#include <stddef.h>
#include <stdint.h>

#define HALBC_MAGIC "HALB"
#define HALBC_VERSION UINT8_C(1)
#define HALBC_FLAG_UTF8 UINT8_C(1)
#define HALBC_HEADER_SIZE ((size_t)8u)

typedef enum HalOpcode {
    HALO_NOP = 0x00,
    HALO_OPEN = 0x01,
    HALO_CLOSE = 0x02,
    HALO_READ = 0x03,
    HALO_WRITE = 0x04,
    HALO_SEEK = 0x05,
    HALO_STAT = 0x06,
    HALO_MOUNT = 0x07,
    HALO_UMOUNT = 0x08,
    HALO_CTL = 0x09,
    HALO_PUSH = 0x0a,
    HALO_POP = 0x0b,
    HALO_CALL = 0x0c,
    HALO_JMP = 0x0d,
    HALO_JZ = 0x0e,
    HALO_HALT = 0xff
} HalOpcode;

/* Caller owns the bytecode and stack for the entire execution lifetime,
 * including all resumes. No mutable overlap with state or budget is valid. */
typedef struct HalBcState {
    nx_const_span bytecode;
    int64_t *stack;
    size_t stack_capacity;
    size_t stack_depth;
    size_t ip;
    uint32_t halted;
} HalBcState;

/* No changes to state or caller-owned stack on validation failure. */
nx_status halbc_init(HalBcState *state, nx_const_span program,
                     int64_t *stack, size_t stack_capacity);

/* One bounded quantum; returns OK only on explicit HALT. An exhausted budget
 * preserves the continuation. Hardware/system opcodes always fail closed,
 * without consuming fuel or changing state. NXEC grant admission is OPEN. */
nx_status halbc_run(HalBcState *state, nx_operation_budget *budget);

/* The assembler owns no backing memory. The wire encodes payload-byte length
 * as little-endian u16; this is a binary ABI bound, not a resource quota. */
typedef struct HalBcAssembler {
    nx_mut_span output;
    size_t length;
} HalBcAssembler;

nx_status halbc_asm_init(HalBcAssembler *assembler, nx_mut_span output);
nx_status halbc_asm_emit_simple(HalBcAssembler *assembler, uint8_t opcode);
nx_status halbc_asm_emit_push(HalBcAssembler *assembler, int32_t value);
nx_status halbc_asm_emit_jump(HalBcAssembler *assembler, int16_t offset,
                              uint32_t conditional);
nx_status halbc_asm_finish(HalBcAssembler *assembler, size_t *written);

#endif /* NEXS_HAL_BC_H */
