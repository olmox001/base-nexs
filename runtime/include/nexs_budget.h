/* NXOLMO runtime contract ported to ONE Base-NEXS product ABI. */
#ifndef NEXS_BUDGET_H
#define NEXS_BUDGET_H

#include "nexs_checked.h"
#include "nexs_cycle_counter.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_operation_budget {
    nx_cycle_counter counter;
    uint64_t operation_id;
    uint64_t fuel_limit;
    uint64_t fuel_remaining;
    uint64_t cycle_limit;
    uint64_t cycle_start;
    uint32_t cycle_source;
    uint32_t flags;
    uint32_t cycle_auxiliary;
    uint32_t reserved;
    uint64_t elapsed_start_ns;
} nx_operation_budget;

typedef struct nx_operation_measurement {
    uint64_t operation_id;
    uint64_t fuel_used;
    uint64_t cycles_used;
    uint32_t cycle_source;
    uint32_t flags;
    uint64_t bytes_used;
    uint32_t cycle_auxiliary;
    uint32_t reserved;
    uint64_t elapsed_ns;
} nx_operation_measurement;

enum {
    NX_MEASUREMENT_CYCLES_VALID = UINT32_C(1) << 0,
    NX_MEASUREMENT_CYCLES_UNKNOWN = UINT32_C(1) << 1
};

nx_status nx_operation_budget_init(nx_operation_budget *budget,
                                   uint64_t operation_id,
                                   uint64_t fuel_limit,
                                   uint64_t cycle_limit);
nx_status nx_operation_budget_init_with_counter(
    nx_operation_budget *budget, const nx_cycle_counter *counter,
    uint64_t operation_id, uint64_t fuel_limit, uint64_t cycle_limit);
nx_status nx_operation_budget_consume(nx_operation_budget *budget,
                                      uint64_t fuel_units);
nx_status nx_operation_budget_poll(nx_operation_budget *budget);
nx_status nx_operation_budget_finish(nx_operation_budget *budget,
                                      nx_operation_measurement *measurement);

#ifdef __cplusplus
}
#endif

#endif
