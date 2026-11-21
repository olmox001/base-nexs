/* NXOLMO cycle-counter platform boundary, fallback only.
 * Strong provider is selected solely in a hardware-specific adapter.
 * Unknown cycles MUST remain unknown and never count as fuel.
 */
#include "nexs_cycle_counter.h"
#include "nexs_checked.h"
#include <stddef.h>
__attribute__((weak)) nx_status nx_cycle_counter_init(
    nx_cycle_counter *counter) {
    if (counter == NULL ||
        ((uintptr_t)counter % (uintptr_t)_Alignof(nx_cycle_counter)) != 0U)
        return NX_STATUS_INVALID_ARGUMENT;
    counter->source = NX_CYCLE_SOURCE_NONE;
    counter->flags = NX_CYCLE_FLAG_UNKNOWN;
    return NX_STATUS_NOT_SUPPORTED;
}
__attribute__((weak)) nx_status nx_cycle_counter_read(
    const nx_cycle_counter *counter, nx_cycle_sample *sample) {
    if (sample == NULL ||
        ((uintptr_t)sample % (uintptr_t)_Alignof(nx_cycle_sample)) != 0U ||
        (counter != NULL &&
         (((uintptr_t)counter % (uintptr_t)_Alignof(nx_cycle_counter)) != 0U ||
          nx_pointer_ranges_overlap(counter, sizeof(*counter),
                                    sample, sizeof(*sample)) != 0)))
        return NX_STATUS_INVALID_ARGUMENT;
    sample->value = NX_CYCLES_UNKNOWN;
    sample->source = NX_CYCLE_SOURCE_NONE;
    sample->flags = NX_CYCLE_FLAG_UNKNOWN;
    sample->auxiliary = 0U;
    sample->reserved = 0U;
    if (counter == NULL) return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_NOT_SUPPORTED;
}
