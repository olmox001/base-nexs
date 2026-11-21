/* NXOLMO operation runtime regression against Base-NEXS single ABI. */
#include "nexs_budget.h"
#include "nexs_clock.h"
#include "nexs_operation_metrics.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n",              \
                          __FILE__, __LINE__, #expression);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_monotonic_clock(void) {
    uint64_t first = NX_NANOSECONDS_UNKNOWN;
    uint64_t second = NX_NANOSECONDS_UNKNOWN;
    uint64_t resolution = NX_NANOSECONDS_UNKNOWN;
    nx_status first_status = nx_monotonic_time_ns(&first);
    nx_status second_status = nx_monotonic_time_ns(&second);
    if (first_status == NX_STATUS_NOT_SUPPORTED) {
        CHECK(second_status == NX_STATUS_NOT_SUPPORTED);
        CHECK(first == NX_NANOSECONDS_UNKNOWN);
        CHECK(second == NX_NANOSECONDS_UNKNOWN);
        CHECK(nx_monotonic_clock_resolution_ns(&resolution) ==
              NX_STATUS_NOT_SUPPORTED);
        return 0;
    }
    CHECK(first_status == NX_STATUS_OK);
    CHECK(second_status == NX_STATUS_OK);
    CHECK(first != NX_NANOSECONDS_UNKNOWN && second != NX_NANOSECONDS_UNKNOWN);
    CHECK(second >= first);
    CHECK(nx_monotonic_clock_resolution_ns(&resolution) == NX_STATUS_OK);
    CHECK(resolution != 0u && resolution != NX_NANOSECONDS_UNKNOWN);
    return 0;
}

static int test_cycle_counter_contract(void) {
    nx_cycle_counter counter;
    nx_cycle_sample sample;
    nx_status status = nx_cycle_counter_init(&counter);

    if (status == NX_STATUS_OK) {
        CHECK(counter.source != NX_CYCLE_SOURCE_NONE);
        CHECK(nx_cycle_counter_read(&counter, &sample) == NX_STATUS_OK);
        CHECK(sample.value != NX_CYCLES_UNKNOWN);
        CHECK(sample.source == counter.source);
        CHECK((sample.flags & NX_CYCLE_FLAG_INVARIANT) != 0u);
    } else {
        CHECK(status == NX_STATUS_NOT_SUPPORTED);
        CHECK(counter.source == NX_CYCLE_SOURCE_NONE);
        CHECK(nx_cycle_counter_read(&counter, &sample) ==
              NX_STATUS_NOT_SUPPORTED);
        CHECK(sample.value == UINT64_MAX);
        CHECK((sample.flags & NX_CYCLE_FLAG_UNKNOWN) != 0u);
    }
    return 0;
}

static int test_fuel_and_measurement(void) {
    nx_operation_budget budget;
    nx_operation_measurement measurement;

    CHECK(nx_operation_budget_init(&budget, 42u, 10u, UINT64_MAX) ==
          NX_STATUS_OK);
    CHECK(nx_operation_budget_consume(&budget, 4u) == NX_STATUS_OK);
    CHECK(nx_operation_budget_consume(&budget, 6u) == NX_STATUS_OK);
    CHECK(budget.fuel_remaining == 0u);
    CHECK(nx_operation_budget_consume(&budget, 1u) ==
          NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(nx_operation_budget_finish(&budget, &measurement) == NX_STATUS_OK);
    CHECK(measurement.operation_id == 42u);
    CHECK(measurement.fuel_used == 10u);
    if ((measurement.flags & NX_MEASUREMENT_CYCLES_VALID) == 0u)
        CHECK(measurement.cycles_used == UINT64_MAX);
    else
        CHECK(measurement.cycles_used != UINT64_MAX);

    CHECK(nx_operation_budget_init(&budget, 1u, 0u, UINT64_MAX) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_operation_budget_init(&budget, 1u, 10u, 0u) ==
          NX_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_cycle_limit_at_finish(void) {
    nx_cycle_counter counter;
    nx_operation_budget budget;
    nx_operation_measurement measurement;
    nx_status status = nx_cycle_counter_init(&counter);
    volatile uint64_t spin = 0u;
    uint64_t i;

    if (status == NX_STATUS_NOT_SUPPORTED)
        return 0;
    CHECK(status == NX_STATUS_OK);
    CHECK(nx_operation_budget_init_with_counter(&budget, &counter, 77u,
                                                100000u, 1u) == NX_STATUS_OK);
    for (i = 0u; i < UINT64_C(100000); ++i)
        spin += i;
    (void)spin;
    status = nx_operation_budget_finish(&budget, &measurement);
    if ((measurement.flags & NX_MEASUREMENT_CYCLES_VALID) != 0u) {
        CHECK(measurement.cycles_used > 1u);
        CHECK(status == NX_STATUS_BUDGET_EXHAUSTED);
    } else {
        CHECK(measurement.cycles_used == UINT64_MAX);
        CHECK(status == NX_STATUS_OK);
    }
    return 0;
}

static int test_operation_scope(void) {
    nx_operation_metrics metrics;
    nx_operation_scope scope;
    nx_operation_metrics_snapshot snapshot;
    nx_result result;

    nx_operation_metrics_init(&metrics, 99u);
    CHECK(nx_operation_scope_begin(&scope, &metrics, 99u, 3u,
                                   UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_scope_consume(&scope, 1u) == NX_STATUS_OK);
    CHECK(nx_operation_scope_consume(&scope, 2u) == NX_STATUS_OK);
    CHECK(nx_operation_scope_consume(&scope, 1u) ==
          NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(nx_operation_scope_finish(&scope, NX_STATUS_OK,
                                    NX_RESULT_DOMAIN_RUNTIME, 3u, 0u,
                                    &result) == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(nx_result_validate(&result) == NX_STATUS_OK);
    CHECK(result.operation_id == 99u);
    CHECK(result.status == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(result.fuel_used == 3u);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.calls == 1u);
    CHECK(snapshot.failures == 1u);
    CHECK(snapshot.fuel_total == 3u);
    CHECK(snapshot.bytes_total == 0u);
    CHECK(nx_operation_scope_finish(&scope, NX_STATUS_OK,
                                    NX_RESULT_DOMAIN_RUNTIME, 0u, 0u,
                                    &result) == NX_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_direct_budget_poll_and_counter_scope(void) {
    nx_cycle_counter counter;
    nx_operation_budget budget;
    nx_operation_metrics metrics;
    nx_operation_scope scope;
    nx_operation_measurement measurement;
    nx_operation_metrics_snapshot snapshot;
    nx_result result;
    union scope_result_overlap {
        nx_operation_scope scope;
        nx_result result;
    } overlap;
    nx_status status = nx_cycle_counter_init(&counter);
    CHECK(status == NX_STATUS_OK || status == NX_STATUS_NOT_SUPPORTED);
    CHECK(nx_operation_budget_init_with_counter(
        &budget, &counter, 101u, 16u, UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_budget_poll(&budget) == NX_STATUS_OK);
    CHECK(nx_operation_budget_consume(&budget, 3u) == NX_STATUS_OK);
    CHECK(nx_operation_budget_poll(&budget) == NX_STATUS_OK);
    CHECK(nx_operation_budget_finish(&budget, &measurement) == NX_STATUS_OK);
    CHECK(measurement.operation_id == 101u);
    CHECK(measurement.fuel_used == 3u);

    nx_operation_metrics_init(&metrics, 102u);
    CHECK(nx_operation_scope_begin_with_counter(
        &scope, &metrics, &counter, 102u, 16u, UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_scope_poll(&scope) == NX_STATUS_OK);
    CHECK(nx_operation_scope_consume(&scope, 5u) == NX_STATUS_OK);
    CHECK(nx_operation_scope_poll(&scope) == NX_STATUS_OK);
    CHECK(nx_operation_scope_finish(&scope, NX_STATUS_OK,
                                    NX_RESULT_DOMAIN_RUNTIME, 5u, 0u,
                                    &result) == NX_STATUS_OK);
    CHECK(nx_result_validate(&result) == NX_STATUS_OK);
    CHECK(nx_operation_scope_begin_with_counter(
        &overlap.scope, NULL, &counter, 103u, 16u, UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_scope_finish(&overlap.scope, NX_STATUS_OK,
                                    NX_RESULT_DOMAIN_RUNTIME, 0u, 0u,
                                    &overlap.result) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_operation_scope_finish(&overlap.scope, NX_STATUS_OK,
                                    NX_RESULT_DOMAIN_RUNTIME, 0u, 0u,
                                    &result) == NX_STATUS_OK);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.calls == 1u && snapshot.fuel_total == 5u);
    return 0;
}

static int test_runtime_output_aliasing(void) {
    union counter_sample_alias {
        nx_cycle_counter counter;
        nx_cycle_sample sample;
    } counter_alias;
    union budget_measurement_alias {
        nx_operation_budget budget;
        nx_operation_measurement measurement;
    } budget_alias;
    union metrics_snapshot_alias {
        nx_operation_metrics metrics;
        nx_operation_metrics_snapshot snapshot;
    } metrics_alias;
    union scope_result_alias {
        nx_operation_scope scope;
        nx_result result;
    } scope_alias;
    nx_operation_metrics metrics;
    nx_operation_metrics_snapshot snapshot;
    nx_result result;

    CHECK(nx_cycle_counter_init(&counter_alias.counter) == NX_STATUS_OK ||
          counter_alias.counter.source == NX_CYCLE_SOURCE_NONE);
    CHECK(nx_cycle_counter_read(&counter_alias.counter,
                                &counter_alias.sample) ==
          NX_STATUS_INVALID_ARGUMENT);

    CHECK(nx_operation_budget_init(&budget_alias.budget, 1u, 8u,
                                   UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_budget_finish(&budget_alias.budget,
                                     &budget_alias.measurement) ==
          NX_STATUS_INVALID_ARGUMENT);

    nx_operation_metrics_init(&metrics_alias.metrics, 3u);
    CHECK(nx_operation_metrics_read(&metrics_alias.metrics,
                                    &metrics_alias.snapshot) ==
          NX_STATUS_INVALID_ARGUMENT);
    nx_operation_metrics_init(&metrics, 4u);
    nx_operation_metrics_record(&metrics, NX_STATUS_OK,
        (const nx_operation_measurement *)&metrics);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.calls == 0u);

    CHECK(nx_operation_scope_begin(&scope_alias.scope, NULL, 5u, 8u,
                                   UINT64_MAX) == NX_STATUS_OK);
    CHECK(nx_operation_scope_finish_ex(&scope_alias.scope, NX_STATUS_OK,
          NX_RESULT_DOMAIN_RUNTIME, 0u, 0u, 0u, &scope_alias.result) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_operation_scope_finish(&scope_alias.scope, NX_STATUS_OK,
          NX_RESULT_DOMAIN_RUNTIME, 0u, 0u, &result) == NX_STATUS_OK);
    return 0;
}

static int test_metrics_aggregation(void) {
    nx_operation_metrics metrics;
    nx_operation_metrics_snapshot snapshot;
    nx_operation_measurement known = {
        7u, 5u, 12u, NX_CYCLE_SOURCE_X86_TSC, NX_MEASUREMENT_CYCLES_VALID,
        4u, 0u, 0u, 34u
    };
    nx_operation_measurement unknown = {
        7u, 3u, UINT64_MAX, NX_CYCLE_SOURCE_NONE,
        NX_MEASUREMENT_CYCLES_UNKNOWN, 0u, 0u, 0u, UINT64_MAX
    };

    nx_operation_metrics_init(&metrics, 7u);
    nx_operation_metrics_record(&metrics, NX_STATUS_OK, &known);
    nx_operation_metrics_record(&metrics, NX_STATUS_BUSY, &unknown);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.operation_id == 7u);
    CHECK(snapshot.calls == 2u);
    CHECK(snapshot.failures == 1u);
    CHECK(snapshot.known_cycle_samples == 1u);
    CHECK(snapshot.unknown_cycle_samples == 1u);
    CHECK(snapshot.cycles_total == 12u);
    CHECK(snapshot.cycles_min == 12u);
    CHECK(snapshot.cycles_max == 12u);
    CHECK(snapshot.bytes_total == 4u);
    CHECK(snapshot.fuel_total == 8u);
    CHECK(snapshot.contention_events == 1u);
    CHECK(snapshot.known_elapsed_samples == 1u);
    CHECK(snapshot.unknown_elapsed_samples == 1u);
    CHECK(snapshot.elapsed_ns_total == 34u);
    CHECK(snapshot.elapsed_ns_min == 34u);
    CHECK(snapshot.elapsed_ns_max == 34u);

    nx_operation_metrics_init(&metrics, 8u);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.cycles_total == UINT64_MAX);
    CHECK(snapshot.cycles_min == UINT64_MAX);
    CHECK(snapshot.cycles_max == UINT64_MAX);
    CHECK(snapshot.unknown_cycle_samples == 0u);
    CHECK(snapshot.elapsed_ns_total == UINT64_MAX);
    CHECK(snapshot.elapsed_ns_min == UINT64_MAX);
    CHECK(snapshot.elapsed_ns_max == UINT64_MAX);

    nx_operation_metrics_init(&metrics, 9u);
    atomic_store_explicit(&metrics.calls, UINT64_MAX - 1u, memory_order_relaxed);
    atomic_store_explicit(&metrics.cycles_total, UINT64_MAX - 2u,
                          memory_order_relaxed);
    atomic_store_explicit(&metrics.fuel_total, UINT64_MAX - 1u,
                          memory_order_relaxed);
    nx_operation_metrics_record(&metrics, NX_STATUS_OK, &known);
    CHECK(nx_operation_metrics_read(&metrics, &snapshot) == NX_STATUS_OK);
    CHECK(snapshot.calls == UINT64_MAX);
    CHECK(snapshot.cycles_total == UINT64_MAX);
    CHECK(snapshot.fuel_total == UINT64_MAX);
    return 0;
}

int main(void) {
    CHECK(test_monotonic_clock() == 0);
    CHECK(test_cycle_counter_contract() == 0);
    CHECK(test_fuel_and_measurement() == 0);
    CHECK(test_cycle_limit_at_finish() == 0);
    CHECK(test_operation_scope() == 0);
    CHECK(test_direct_budget_poll_and_counter_scope() == 0);
    CHECK(test_runtime_output_aliasing() == 0);
    CHECK(test_metrics_aggregation() == 0);
    (void)puts("runtime budget/metrics unit tests: PASS");
    return 0;
}
