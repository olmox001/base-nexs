/* NXOLMO runtime source ported in-place against Base-NEXS canonical ABI. */
#include "nexs_budget.h"
#include "nexs_clock.h"
#include "nexs_checked.h"

#include <stddef.h>

nx_status nx_operation_budget_init_with_counter(
    nx_operation_budget *budget, const nx_cycle_counter *counter,
    uint64_t operation_id, uint64_t fuel_limit, uint64_t cycle_limit) {
    nx_cycle_sample sample;
    nx_status status;

    if (budget == NULL || counter == NULL || fuel_limit == 0u ||
        cycle_limit == 0u ||
        ((uintptr_t)budget % (uintptr_t)_Alignof(nx_operation_budget)) != 0u ||
        ((uintptr_t)counter % (uintptr_t)_Alignof(nx_cycle_counter)) != 0u ||
        nx_pointer_ranges_overlap(budget, sizeof(*budget),
                                  counter, sizeof(*counter)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;

    budget->counter = *counter;
    budget->operation_id = operation_id;
    budget->fuel_limit = fuel_limit;
    budget->fuel_remaining = fuel_limit;
    budget->cycle_limit = cycle_limit;
    budget->cycle_start = NX_CYCLES_UNKNOWN;
    budget->cycle_source = NX_CYCLE_SOURCE_NONE;
    budget->flags = NX_MEASUREMENT_CYCLES_UNKNOWN;
    budget->cycle_auxiliary = 0u;
    budget->reserved = 0u;
    budget->elapsed_start_ns = NX_NANOSECONDS_UNKNOWN;
    (void)nx_monotonic_time_ns(&budget->elapsed_start_ns);

    if (counter->source == NX_CYCLE_SOURCE_NONE) {
        budget->cycle_limit = UINT64_MAX;
        return NX_STATUS_OK;
    }

    status = nx_cycle_counter_read(counter, &sample);
    if (status != NX_STATUS_OK || sample.value == NX_CYCLES_UNKNOWN) {
        budget->cycle_limit = UINT64_MAX;
        return NX_STATUS_OK;
    }

    budget->cycle_start = sample.value;
    budget->cycle_source = sample.source;
    budget->cycle_auxiliary = sample.auxiliary;
    budget->flags = NX_MEASUREMENT_CYCLES_VALID;
    return NX_STATUS_OK;
}

nx_status nx_operation_budget_init(nx_operation_budget *budget,
                                   uint64_t operation_id,
                                   uint64_t fuel_limit,
                                   uint64_t cycle_limit) {
    nx_cycle_counter counter;
    nx_status status;

    status = nx_cycle_counter_init(&counter);
    if (status != NX_STATUS_OK && status != NX_STATUS_NOT_SUPPORTED)
        return status;
    return nx_operation_budget_init_with_counter(
        budget, &counter, operation_id, fuel_limit, cycle_limit);
}

nx_status nx_operation_budget_poll(nx_operation_budget *budget) {
    nx_cycle_sample sample;
    nx_status status;

    if (budget == NULL ||
        ((uintptr_t)budget % (uintptr_t)_Alignof(nx_operation_budget)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (budget->cycle_limit == UINT64_MAX ||
        budget->cycle_start == NX_CYCLES_UNKNOWN)
        return NX_STATUS_OK;

    status = nx_cycle_counter_read(&budget->counter, &sample);
    if (status != NX_STATUS_OK || sample.value == NX_CYCLES_UNKNOWN ||
        sample.source != budget->cycle_source ||
        sample.auxiliary != budget->cycle_auxiliary ||
        sample.value < budget->cycle_start) {
        budget->cycle_limit = UINT64_MAX;
        budget->cycle_start = NX_CYCLES_UNKNOWN;
        budget->cycle_source = NX_CYCLE_SOURCE_NONE;
        budget->cycle_auxiliary = 0u;
        budget->flags = NX_MEASUREMENT_CYCLES_UNKNOWN;
        return NX_STATUS_OK;
    }

    if ((sample.value - budget->cycle_start) > budget->cycle_limit)
        return NX_STATUS_BUDGET_EXHAUSTED;
    return NX_STATUS_OK;
}

nx_status nx_operation_budget_consume(nx_operation_budget *budget,
                                      uint64_t fuel_units) {
    if (budget == NULL ||
        ((uintptr_t)budget % (uintptr_t)_Alignof(nx_operation_budget)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (fuel_units > budget->fuel_remaining) {
        budget->fuel_remaining = 0u;
        return NX_STATUS_BUDGET_EXHAUSTED;
    }
    budget->fuel_remaining -= fuel_units;
    return nx_operation_budget_poll(budget);
}

static void nx_operation_budget_measure_elapsed(
    const nx_operation_budget *budget, nx_operation_measurement *measurement) {
    uint64_t elapsed_end_ns = NX_NANOSECONDS_UNKNOWN;
    if (budget->elapsed_start_ns != NX_NANOSECONDS_UNKNOWN &&
        nx_monotonic_time_ns(&elapsed_end_ns) == NX_STATUS_OK &&
        elapsed_end_ns >= budget->elapsed_start_ns)
        measurement->elapsed_ns = elapsed_end_ns - budget->elapsed_start_ns;
}

nx_status nx_operation_budget_finish(nx_operation_budget *budget,
                                      nx_operation_measurement *measurement) {
    nx_cycle_sample sample;
    nx_status status;

    if (measurement == NULL ||
        ((uintptr_t)measurement % (uintptr_t)_Alignof(nx_operation_measurement)) != 0u ||
        (budget != NULL &&
         (((uintptr_t)budget % (uintptr_t)_Alignof(nx_operation_budget)) != 0u ||
          nx_pointer_ranges_overlap(budget, sizeof(*budget),
                                    measurement, sizeof(*measurement)) != 0)))
        return NX_STATUS_INVALID_ARGUMENT;
    measurement->operation_id = budget == NULL ? 0u : budget->operation_id;
    measurement->fuel_used = 0u;
    measurement->cycles_used = NX_CYCLES_UNKNOWN;
    measurement->cycle_source = NX_CYCLE_SOURCE_NONE;
    measurement->flags = NX_MEASUREMENT_CYCLES_UNKNOWN;
    measurement->bytes_used = 0u;
    measurement->cycle_auxiliary = 0u;
    measurement->reserved = 0u;
    measurement->elapsed_ns = NX_NANOSECONDS_UNKNOWN;

    if (budget == NULL)
        return NX_STATUS_INVALID_ARGUMENT;

    measurement->fuel_used = budget->fuel_limit - budget->fuel_remaining;
    nx_operation_budget_measure_elapsed(budget, measurement);
    if (budget->cycle_start == NX_CYCLES_UNKNOWN ||
        budget->cycle_source == NX_CYCLE_SOURCE_NONE)
        return NX_STATUS_OK;

    status = nx_cycle_counter_read(&budget->counter, &sample);
    if (status != NX_STATUS_OK || sample.value == NX_CYCLES_UNKNOWN ||
        sample.source != budget->cycle_source ||
        sample.auxiliary != budget->cycle_auxiliary ||
        sample.value < budget->cycle_start)
        return NX_STATUS_OK;

    measurement->cycles_used = sample.value - budget->cycle_start;
    measurement->cycle_source = sample.source;
    measurement->cycle_auxiliary = sample.auxiliary;
    measurement->flags = NX_MEASUREMENT_CYCLES_VALID;
    if (budget->cycle_limit != UINT64_MAX &&
        measurement->cycles_used > budget->cycle_limit)
        return NX_STATUS_BUDGET_EXHAUSTED;
    return NX_STATUS_OK;
}
