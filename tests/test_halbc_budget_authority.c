/* Production-linked Base-NEXS HALBC security and budget conformance.
 * Genuine NXOLMO operation budget and canonical bytecode executor.
 */
#include "nexs_hal_bc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    (void)fprintf(stderr, "HALBC_BOUNDARY_FAILURE line=%d: %s\n", \
                  __LINE__, #expr); return 1; \
  } } while (0)

static nx_status fuel(nx_operation_budget *budget, uint64_t units) {
    return nx_operation_budget_init(budget, UINT64_C(0x5a771), units,
                                    UINT64_MAX);
}

static int bounded_compile_and_resume(void) {
    uint8_t code[256];
    int64_t stack[4] = {0};
    HalBcState state;
    HalBcAssembler assembler;
    size_t written = 0u;
    nx_operation_budget budget;
    CHECK(halbc_asm_init(&assembler, (nx_mut_span){code, sizeof(code)}) ==
          NX_STATUS_OK);
    CHECK(halbc_asm_emit_push(&assembler, 42) == NX_STATUS_OK);
    CHECK(halbc_asm_emit_simple(&assembler, HALO_HALT) == NX_STATUS_OK);
    CHECK(halbc_asm_finish(&assembler, &written) == NX_STATUS_OK &&
          written == 15u);
    CHECK(halbc_init(&state, (nx_const_span){code,written},
                     stack,4u) == NX_STATUS_OK);
    CHECK(fuel(&budget, 1u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(state.halted == 0u && state.ip == 14u &&
          state.stack_depth == 1u && stack[0] == 42);
    CHECK(budget.fuel_remaining == 0u);
    CHECK(fuel(&budget,1u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_OK &&
          state.halted == 1u && stack[0] == 42);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_OK);
    return 0;
}

static int deliberately_infinite_loop_yields(void) {
    const uint8_t infinite[] = {
        'H','A','L','B',1u,1u,3u,0u,HALO_JMP,0xfdu,0xffu
    };
    int64_t stack[1] = {0};
    HalBcState state;
    nx_operation_budget budget;
    CHECK(halbc_init(&state,(nx_const_span){infinite,sizeof(infinite)},
                     stack,1u) == NX_STATUS_OK);
    CHECK(fuel(&budget,137u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_BUDGET_EXHAUSTED &&
          budget.fuel_remaining == 0u &&
          state.halted == 0u && state.ip == 8u);
    CHECK(fuel(&budget,13u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_BUDGET_EXHAUSTED &&
          state.ip == 8u && budget.fuel_remaining == 0u);
    return 0;
}

static int malformed_headers_and_jumps_fail_closed(void) {
    const uint8_t outside[] = {
        'H','A','L','B',1u,1u,3u,0u,HALO_JMP,0xfcu,0xffu
    };
    const uint8_t missing_halt[] = {
        'H','A','L','B',1u,1u,1u,0u,HALO_NOP
    };
    int64_t stack[4] = {0};
    HalBcState state = {0};
    state.ip = 412u;
    CHECK(halbc_init(NULL,(nx_const_span){outside,sizeof(outside)},
                     stack,4u) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(halbc_init(&state,(nx_const_span){NULL,sizeof(outside)},
                     stack,4u) == NX_STATUS_INVALID_ARGUMENT &&
          state.ip == 412u);
    uint8_t corrupt[sizeof(outside)];
    memcpy(corrupt,outside,sizeof(corrupt));
    corrupt[6] = 7u;
    CHECK(halbc_init(&state,(nx_const_span){corrupt,sizeof(corrupt)},
                     stack,4u) == NX_STATUS_MALFORMED &&
          state.ip == 412u);
    CHECK(halbc_init(&state,(nx_const_span){outside,sizeof(outside)},
                     stack,4u) == NX_STATUS_OK);
    nx_operation_budget budget;
    CHECK(fuel(&budget,3u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_MALFORMED);
    CHECK(halbc_init(&state,(nx_const_span){missing_halt,
                        sizeof(missing_halt)},stack,4u) == NX_STATUS_OK);
    CHECK(fuel(&budget,3u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_MALFORMED);
    return 0;
}

static int privileged_opcodes_do_not_consume_fuel(void) {
    const uint8_t opens[] = {
        'H','A','L','B',1u,1u,4u,0u,HALO_OPEN,0u,0u,HALO_HALT
    };
    const uint8_t callback[] = {
        'H','A','L','B',1u,1u,4u,0u,HALO_CALL,0u,0u,HALO_HALT
    };
    const uint8_t *all[] = {opens,callback};
    const size_t lengths[] = {sizeof(opens),sizeof(callback)};
    int64_t stack[2] = {0};
    nx_operation_budget budget;
    for (size_t i = 0u; i < 2u; ++i) {
        HalBcState state;
        CHECK(halbc_init(&state,(nx_const_span){all[i],lengths[i]},
                         stack,2u) == NX_STATUS_OK);
        CHECK(fuel(&budget,17u) == NX_STATUS_OK);
        CHECK(halbc_run(&state,&budget) == NX_STATUS_ACCESS_DENIED);
        CHECK(state.ip == 8u && state.halted == 0u &&
              state.stack_depth == 0u && budget.fuel_remaining == 17u);
    }
    return 0;
}

static int caller_owned_capacity_and_output_unchanged(void) {
    uint8_t short_buffer[8] = {0};
    HalBcAssembler assembler;
    size_t written = 313u;
    CHECK(halbc_asm_init(&assembler,(nx_mut_span){
        short_buffer,sizeof(short_buffer)}) == NX_STATUS_OK);
    CHECK(halbc_asm_emit_push(&assembler,42) == NX_STATUS_NO_SPACE);
    CHECK(assembler.length == 8u && short_buffer[6] == 0u &&
          short_buffer[7] == 0u);
    CHECK(halbc_asm_emit_simple(&assembler,HALO_OPEN) ==
          NX_STATUS_ACCESS_DENIED && assembler.length == 8u);
    CHECK(halbc_asm_finish(&assembler,&written) == NX_STATUS_OK &&
          written == 8u);
    uint8_t output[32];
    CHECK(halbc_asm_init(&assembler,(nx_mut_span){output,sizeof(output)}) ==
          NX_STATUS_OK);
    CHECK(halbc_asm_emit_push(&assembler,INT32_MIN) == NX_STATUS_OK);
    CHECK(halbc_asm_emit_simple(&assembler,HALO_HALT) == NX_STATUS_OK);
    CHECK(halbc_asm_finish(&assembler,&written) == NX_STATUS_OK);
    int64_t stack[1] = {INT64_C(12)};
    HalBcState state;
    nx_operation_budget budget;
    CHECK(halbc_init(&state,(nx_const_span){output,written},
                     stack,1u) == NX_STATUS_OK);
    CHECK(fuel(&budget,4u) == NX_STATUS_OK);
    CHECK(halbc_run(&state,&budget) == NX_STATUS_OK &&
          stack[0] == (int64_t)INT32_MIN);
    return 0;
}

int main(void) {
    CHECK(bounded_compile_and_resume() == 0);
    CHECK(deliberately_infinite_loop_yields() == 0);
    CHECK(malformed_headers_and_jumps_fail_closed() == 0);
    CHECK(privileged_opcodes_do_not_consume_fuel() == 0);
    CHECK(caller_owned_capacity_and_output_unchanged() == 0);
    (void)puts("HALBC_BUDGET_AUTHORITY_PASS bounded=1 resumed=1 "
               "raw_hardware_DENIED=1 caller_owned=1 negative_oracles=1");
    return 0;
}
