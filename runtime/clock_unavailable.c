/* Core clock provider: explicit no-clock result until platform admitted.
 * Hosted strong provider supplies a real monotonic clock.
 */
#include "nexs_clock.h"
__attribute__((weak)) nx_status nx_monotonic_time_ns(uint64_t *value) {
    if (value == NULL) return NX_STATUS_INVALID_ARGUMENT;
    *value = NX_NANOSECONDS_UNKNOWN;
    return NX_STATUS_NOT_SUPPORTED;
}
__attribute__((weak)) nx_status nx_monotonic_clock_resolution_ns(
    uint64_t *value) {
    if (value == NULL) return NX_STATUS_INVALID_ARGUMENT;
    *value = NX_NANOSECONDS_UNKNOWN;
    return NX_STATUS_NOT_SUPPORTED;
}
