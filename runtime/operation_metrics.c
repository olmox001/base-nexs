/* NXOLMO runtime source ported in-place against Base-NEXS canonical ABI. */
#include "nexs_operation_metrics.h"

#include "nexs_checked.h"

#include <stddef.h>

#define NX_METRICS_ATOMIC_RETRY_LIMIT 64u

static void nx_atomic_add_saturating(_Atomic uint64_t *target, uint64_t value) {
    uint32_t attempt;
    for (attempt = 0u; attempt < NX_METRICS_ATOMIC_RETRY_LIMIT; ++attempt) {
        uint64_t current = atomic_load_explicit(target, memory_order_relaxed);
        uint64_t next;
        if (current == UINT64_MAX)
            return;
        next = value > UINT64_MAX - current ? UINT64_MAX : current + value;
        if (atomic_compare_exchange_strong_explicit(
                target, &current, next, memory_order_relaxed,
                memory_order_relaxed))
            return;
    }
    /* Contention exhaustion is represented conservatively as saturation. */
    atomic_store_explicit(target, UINT64_MAX, memory_order_relaxed);
}

static void nx_atomic_min_bounded(
    nx_operation_metrics *metrics, _Atomic uint64_t *target, uint64_t value) {
    uint32_t attempt;
    for (attempt = 0u; attempt < NX_METRICS_ATOMIC_RETRY_LIMIT; ++attempt) {
        uint64_t current = atomic_load_explicit(target, memory_order_relaxed);
        if (current <= value)
            return;
        if (atomic_compare_exchange_strong_explicit(
                target, &current, value, memory_order_relaxed,
                memory_order_relaxed))
            return;
    }
    atomic_store_explicit(target, 0u, memory_order_relaxed);
    nx_atomic_add_saturating(&metrics->contention_events, 1u);
}

static void nx_atomic_max_bounded(
    nx_operation_metrics *metrics, _Atomic uint64_t *target, uint64_t value) {
    uint32_t attempt;
    for (attempt = 0u; attempt < NX_METRICS_ATOMIC_RETRY_LIMIT; ++attempt) {
        uint64_t current = atomic_load_explicit(target, memory_order_relaxed);
        if (current >= value)
            return;
        if (atomic_compare_exchange_strong_explicit(
                target, &current, value, memory_order_relaxed,
                memory_order_relaxed))
            return;
    }
    atomic_store_explicit(target, UINT64_MAX, memory_order_relaxed);
    nx_atomic_add_saturating(&metrics->contention_events, 1u);
}

void nx_operation_metrics_init(nx_operation_metrics *metrics,
                               uint64_t operation_id) {
    if (metrics == NULL)
        return;
    metrics->operation_id = operation_id;
    atomic_init(&metrics->calls, 0u);
    atomic_init(&metrics->failures, 0u);
    atomic_init(&metrics->known_cycle_samples, 0u);
    atomic_init(&metrics->unknown_cycle_samples, 0u);
    atomic_init(&metrics->cycles_total, 0u);
    atomic_init(&metrics->cycles_min, UINT64_MAX);
    atomic_init(&metrics->cycles_max, 0u);
    atomic_init(&metrics->bytes_total, 0u);
    atomic_init(&metrics->fuel_total, 0u);
    atomic_init(&metrics->contention_events, 0u);
    atomic_init(&metrics->known_elapsed_samples, 0u);
    atomic_init(&metrics->unknown_elapsed_samples, 0u);
    atomic_init(&metrics->elapsed_ns_total, 0u);
    atomic_init(&metrics->elapsed_ns_min, UINT64_MAX);
    atomic_init(&metrics->elapsed_ns_max, 0u);
}

static void nx_operation_metrics_record_elapsed(
    nx_operation_metrics *metrics, nx_status status,
    const nx_operation_measurement *measurement) {
    nx_atomic_add_saturating(&metrics->calls, 1u);
    if (status != NX_STATUS_OK)
        nx_atomic_add_saturating(&metrics->failures, 1u);
    if (status == NX_STATUS_BUSY || status == NX_STATUS_RETRY)
        nx_atomic_add_saturating(&metrics->contention_events, 1u);
    nx_atomic_add_saturating(&metrics->fuel_total, measurement->fuel_used);
    nx_atomic_add_saturating(&metrics->bytes_total, measurement->bytes_used);
    if (measurement->elapsed_ns == UINT64_MAX) {
        nx_atomic_add_saturating(&metrics->unknown_elapsed_samples, 1u);
        return;
    }
    nx_atomic_add_saturating(&metrics->known_elapsed_samples, 1u);
    nx_atomic_add_saturating(&metrics->elapsed_ns_total,
                             measurement->elapsed_ns);
    nx_atomic_min_bounded(metrics, &metrics->elapsed_ns_min,
                          measurement->elapsed_ns);
    nx_atomic_max_bounded(metrics, &metrics->elapsed_ns_max,
                          measurement->elapsed_ns);
}

static void nx_operation_metrics_record_cycles(
    nx_operation_metrics *metrics,
    const nx_operation_measurement *measurement) {
    if ((measurement->flags & NX_MEASUREMENT_CYCLES_VALID) == 0u ||
        measurement->cycles_used == NX_CYCLES_UNKNOWN) {
        nx_atomic_add_saturating(&metrics->unknown_cycle_samples, 1u);
        return;
    }
    nx_atomic_add_saturating(&metrics->known_cycle_samples, 1u);
    nx_atomic_add_saturating(&metrics->cycles_total, measurement->cycles_used);
    nx_atomic_min_bounded(metrics, &metrics->cycles_min,
                          measurement->cycles_used);
    nx_atomic_max_bounded(metrics, &metrics->cycles_max,
                          measurement->cycles_used);
}

void nx_operation_metrics_record(nx_operation_metrics *metrics,
                                 nx_status status,
                                 const nx_operation_measurement *measurement) {
    if (metrics == NULL || measurement == NULL ||
        ((uintptr_t)metrics % (uintptr_t)_Alignof(nx_operation_metrics)) != 0u ||
        ((uintptr_t)measurement %
         (uintptr_t)_Alignof(nx_operation_measurement)) != 0u ||
        nx_pointer_ranges_overlap(metrics, sizeof(*metrics),
                                  measurement, sizeof(*measurement)) != 0)
        return;
    nx_operation_metrics_record_elapsed(metrics, status, measurement);
    nx_operation_metrics_record_cycles(metrics, measurement);
}

static void nx_operation_metrics_read_cycles(
    const nx_operation_metrics *metrics,
    nx_operation_metrics_snapshot *snapshot, uint64_t known_samples) {
    snapshot->known_cycle_samples = known_samples;
    snapshot->unknown_cycle_samples = atomic_load_explicit(
        &metrics->unknown_cycle_samples, memory_order_relaxed);
    snapshot->cycles_total = known_samples == 0u ? NX_CYCLES_UNKNOWN
        : atomic_load_explicit(&metrics->cycles_total, memory_order_relaxed);
    snapshot->cycles_min = known_samples == 0u ? NX_CYCLES_UNKNOWN
        : atomic_load_explicit(&metrics->cycles_min, memory_order_relaxed);
    snapshot->cycles_max = known_samples == 0u ? NX_CYCLES_UNKNOWN
        : atomic_load_explicit(&metrics->cycles_max, memory_order_relaxed);
}

static void nx_operation_metrics_read_elapsed(
    const nx_operation_metrics *metrics,
    nx_operation_metrics_snapshot *snapshot) {
    snapshot->known_elapsed_samples = atomic_load_explicit(
        &metrics->known_elapsed_samples, memory_order_relaxed);
    snapshot->unknown_elapsed_samples = atomic_load_explicit(
        &metrics->unknown_elapsed_samples, memory_order_relaxed);
    snapshot->elapsed_ns_total = snapshot->known_elapsed_samples == 0u
        ? UINT64_MAX : atomic_load_explicit(
            &metrics->elapsed_ns_total, memory_order_relaxed);
    snapshot->elapsed_ns_min = snapshot->known_elapsed_samples == 0u
        ? UINT64_MAX : atomic_load_explicit(
            &metrics->elapsed_ns_min, memory_order_relaxed);
    snapshot->elapsed_ns_max = snapshot->known_elapsed_samples == 0u
        ? UINT64_MAX : atomic_load_explicit(
            &metrics->elapsed_ns_max, memory_order_relaxed);
}

nx_status nx_operation_metrics_read(const nx_operation_metrics *metrics,
                                    nx_operation_metrics_snapshot *snapshot) {
    uint64_t known_samples;
    if (metrics == NULL || snapshot == NULL ||
        ((uintptr_t)metrics % (uintptr_t)_Alignof(nx_operation_metrics)) != 0u ||
        ((uintptr_t)snapshot %
         (uintptr_t)_Alignof(nx_operation_metrics_snapshot)) != 0u ||
        nx_pointer_ranges_overlap(metrics, sizeof(*metrics),
                                  snapshot, sizeof(*snapshot)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    snapshot->operation_id = metrics->operation_id;
    snapshot->calls = atomic_load_explicit(&metrics->calls, memory_order_relaxed);
    snapshot->failures = atomic_load_explicit(&metrics->failures, memory_order_relaxed);
    snapshot->bytes_total = atomic_load_explicit(&metrics->bytes_total,
                                                 memory_order_relaxed);
    snapshot->fuel_total = atomic_load_explicit(&metrics->fuel_total,
                                                memory_order_relaxed);
    snapshot->contention_events = atomic_load_explicit(
        &metrics->contention_events, memory_order_relaxed);
    known_samples = atomic_load_explicit(&metrics->known_cycle_samples,
                                         memory_order_relaxed);
    nx_operation_metrics_read_cycles(metrics, snapshot, known_samples);
    nx_operation_metrics_read_elapsed(metrics, snapshot);
    return NX_STATUS_OK;
}


nx_status nx_operation_scope_begin_with_counter(
    nx_operation_scope *scope, nx_operation_metrics *metrics,
    const nx_cycle_counter *counter, uint64_t operation_id,
    uint64_t fuel_limit, uint64_t cycle_limit) {
    nx_status status;

    if (scope == NULL ||
        ((uintptr_t)scope % (uintptr_t)_Alignof(nx_operation_scope)) != 0u ||
        (metrics != NULL &&
         (((uintptr_t)metrics % (uintptr_t)_Alignof(nx_operation_metrics)) != 0u ||
          nx_pointer_ranges_overlap(scope, sizeof(*scope),
                                    metrics, sizeof(*metrics)) != 0)) ||
        (counter != NULL &&
         nx_pointer_ranges_overlap(scope, sizeof(*scope),
                                   counter, sizeof(*counter)) != 0))
        return NX_STATUS_INVALID_ARGUMENT;
    scope->metrics = metrics;
    scope->budget_status = NX_STATUS_OK;
    scope->active = 0u;
    status = nx_operation_budget_init_with_counter(
        &scope->budget, counter, operation_id, fuel_limit, cycle_limit);
    if (status != NX_STATUS_OK)
        return status;
    scope->active = 1u;
    return NX_STATUS_OK;
}

nx_status nx_operation_scope_begin(nx_operation_scope *scope,
                                   nx_operation_metrics *metrics,
                                   uint64_t operation_id,
                                   uint64_t fuel_limit,
                                   uint64_t cycle_limit) {
    nx_cycle_counter counter;
    nx_status status = nx_cycle_counter_init(&counter);

    if (status != NX_STATUS_OK && status != NX_STATUS_NOT_SUPPORTED)
        return status;
    return nx_operation_scope_begin_with_counter(
        scope, metrics, &counter, operation_id, fuel_limit, cycle_limit);
}

nx_status nx_operation_scope_consume(nx_operation_scope *scope,
                                     uint64_t fuel_units) {
    nx_status status;

    if (scope == NULL ||
        ((uintptr_t)scope % (uintptr_t)_Alignof(nx_operation_scope)) != 0u ||
        scope->active == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (scope->budget_status != NX_STATUS_OK)
        return scope->budget_status;
    status = nx_operation_budget_consume(&scope->budget, fuel_units);
    if (status != NX_STATUS_OK)
        scope->budget_status = status;
    return status;
}

nx_status nx_operation_scope_poll(nx_operation_scope *scope) {
    nx_status status;

    if (scope == NULL ||
        ((uintptr_t)scope % (uintptr_t)_Alignof(nx_operation_scope)) != 0u ||
        scope->active == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (scope->budget_status != NX_STATUS_OK)
        return scope->budget_status;
    status = nx_operation_budget_poll(&scope->budget);
    if (status != NX_STATUS_OK)
        scope->budget_status = status;
    return status;
}

nx_status nx_operation_scope_finish_ex(nx_operation_scope *scope,
                                       nx_status operation_status,
                                       nx_result_domain domain,
                                       uint64_t completed,
                                       uint64_t detail,
                                       uint64_t bytes_used,
                                       nx_result *result) {
    nx_operation_measurement measurement;
    nx_status final_status = operation_status;
    nx_status measure_status;

    if (scope == NULL ||
        ((uintptr_t)scope % (uintptr_t)_Alignof(nx_operation_scope)) != 0u ||
        scope->active == 0u ||
        (result != NULL &&
         (((uintptr_t)result % (uintptr_t)_Alignof(nx_result)) != 0u ||
          nx_pointer_ranges_overlap(scope, sizeof(*scope),
                                    result, sizeof(*result)) != 0 ||
          (scope->metrics != NULL &&
           nx_pointer_ranges_overlap(scope->metrics, sizeof(*scope->metrics),
                                     result, sizeof(*result)) != 0))))
        return NX_STATUS_INVALID_ARGUMENT;

    if (final_status == NX_STATUS_OK && scope->budget_status != NX_STATUS_OK)
        final_status = scope->budget_status;

    measure_status = nx_operation_budget_finish(&scope->budget, &measurement);
    measurement.bytes_used = bytes_used;
    if (measure_status != NX_STATUS_OK && final_status == NX_STATUS_OK)
        final_status = measure_status;

    if (scope->metrics != NULL)
        nx_operation_metrics_record(scope->metrics, final_status, &measurement);
    if (result != NULL) {
        nx_status result_status = nx_result_init(
            result, final_status, domain, measurement.operation_id, completed,
            detail, measurement.cycles_used, measurement.fuel_used);
        if (result_status != NX_STATUS_OK && final_status == NX_STATUS_OK)
            final_status = result_status;
    }

    scope->active = 0u;
    return final_status;
}

nx_status nx_operation_scope_finish(nx_operation_scope *scope,
                                    nx_status operation_status,
                                    nx_result_domain domain,
                                    uint64_t completed,
                                    uint64_t detail,
                                    nx_result *result) {
    return nx_operation_scope_finish_ex(scope, operation_status, domain,
                                        completed, detail, 0u, result);
}
