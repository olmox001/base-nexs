/* NXOLMO runtime contract ported to ONE Base-NEXS product ABI. */
#ifndef NEXS_METRICS_H
#define NEXS_METRICS_H

#include "nexs_checked.h"
#include "nexs_budget.h"
#include "nexs_result.h"

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Metrics updates use bounded 64-attempt atomic retries. Counter contention
 * exhaustion saturates totals; extrema contention widens min/max to the
 * conservative bounds 0/UINT64_MAX and increments contention_events.
 */
typedef struct nx_operation_metrics {
    uint64_t operation_id;
    _Atomic uint64_t calls;
    _Atomic uint64_t failures;
    _Atomic uint64_t known_cycle_samples;
    _Atomic uint64_t unknown_cycle_samples;
    _Atomic uint64_t cycles_total;
    _Atomic uint64_t cycles_min;
    _Atomic uint64_t cycles_max;
    _Atomic uint64_t bytes_total;
    _Atomic uint64_t fuel_total;
    _Atomic uint64_t contention_events;
    _Atomic uint64_t known_elapsed_samples;
    _Atomic uint64_t unknown_elapsed_samples;
    _Atomic uint64_t elapsed_ns_total;
    _Atomic uint64_t elapsed_ns_min;
    _Atomic uint64_t elapsed_ns_max;
} nx_operation_metrics;

typedef struct nx_operation_metrics_snapshot {
    uint64_t operation_id;
    uint64_t calls;
    uint64_t failures;
    uint64_t known_cycle_samples;
    uint64_t unknown_cycle_samples;
    uint64_t cycles_total;
    uint64_t cycles_min;
    uint64_t cycles_max;
    uint64_t bytes_total;
    uint64_t fuel_total;
    uint64_t contention_events;
    uint64_t known_elapsed_samples;
    uint64_t unknown_elapsed_samples;
    uint64_t elapsed_ns_total;
    uint64_t elapsed_ns_min;
    uint64_t elapsed_ns_max;
} nx_operation_metrics_snapshot;

void nx_operation_metrics_init(nx_operation_metrics *metrics,
                               uint64_t operation_id);
void nx_operation_metrics_record(nx_operation_metrics *metrics,
                                 nx_status status,
                                 const nx_operation_measurement *measurement);
nx_status nx_operation_metrics_read(const nx_operation_metrics *metrics,
                                    nx_operation_metrics_snapshot *snapshot);

typedef struct nx_operation_scope {
    nx_operation_budget budget;
    nx_operation_metrics *metrics;
    nx_status budget_status;
    uint32_t active;
} nx_operation_scope;

nx_status nx_operation_scope_begin(nx_operation_scope *scope,
                                   nx_operation_metrics *metrics,
                                   uint64_t operation_id,
                                   uint64_t fuel_limit,
                                   uint64_t cycle_limit);
nx_status nx_operation_scope_begin_with_counter(
    nx_operation_scope *scope, nx_operation_metrics *metrics,
    const nx_cycle_counter *counter, uint64_t operation_id,
    uint64_t fuel_limit, uint64_t cycle_limit);
nx_status nx_operation_scope_consume(nx_operation_scope *scope,
                                     uint64_t fuel_units);
nx_status nx_operation_scope_poll(nx_operation_scope *scope);
nx_status nx_operation_scope_finish_ex(nx_operation_scope *scope,
                                       nx_status operation_status,
                                       nx_result_domain domain,
                                       uint64_t completed,
                                       uint64_t detail,
                                       uint64_t bytes_used,
                                       nx_result *result);
nx_status nx_operation_scope_finish(nx_operation_scope *scope,
                                    nx_status operation_status,
                                    nx_result_domain domain,
                                    uint64_t completed,
                                    uint64_t detail,
                                    nx_result *result);

#ifdef __cplusplus
}
#endif

#endif
