/* NXOLMO POSIX monotonic-clock algorithm, hosted adapter only.
 * Selected by config/sources.mk. Unknown wall time is never substituted.
 */
#define _POSIX_C_SOURCE 200809L
#include "nexs_clock.h"
#include <time.h>
nx_status nx_monotonic_time_ns(uint64_t *nanoseconds) {
    if (nanoseconds == NULL) return NX_STATUS_INVALID_ARGUMENT;
    *nanoseconds = NX_NANOSECONDS_UNKNOWN;
    struct timespec sample;
    if (clock_gettime(CLOCK_MONOTONIC, &sample) != 0)
        return NX_STATUS_NOT_SUPPORTED;
    if (sample.tv_sec < (time_t)0 || sample.tv_nsec < 0 ||
        sample.tv_nsec >= 1000000000L)
        return NX_STATUS_CORRUPT;
    uint64_t seconds = (uint64_t)sample.tv_sec;
    if (seconds > UINT64_MAX / UINT64_C(1000000000))
        return NX_STATUS_OVERFLOW;
    seconds *= UINT64_C(1000000000);
    uint64_t fraction = (uint64_t)sample.tv_nsec;
    if (fraction > UINT64_MAX - seconds)
        return NX_STATUS_OVERFLOW;
    *nanoseconds = seconds + fraction;
    return NX_STATUS_OK;
}
nx_status nx_monotonic_clock_resolution_ns(uint64_t *nanoseconds) {
    if (nanoseconds == NULL) return NX_STATUS_INVALID_ARGUMENT;
    *nanoseconds = NX_NANOSECONDS_UNKNOWN;
    struct timespec sample;
    if (clock_getres(CLOCK_MONOTONIC, &sample) != 0)
        return NX_STATUS_NOT_SUPPORTED;
    if (sample.tv_sec < (time_t)0 || sample.tv_nsec < 0 ||
        sample.tv_nsec >= 1000000000L)
        return NX_STATUS_CORRUPT;
    if ((uint64_t)sample.tv_sec >
        UINT64_MAX / UINT64_C(1000000000))
        return NX_STATUS_OVERFLOW;
    uint64_t result = (uint64_t)sample.tv_sec * UINT64_C(1000000000);
    if ((uint64_t)sample.tv_nsec > UINT64_MAX - result)
        return NX_STATUS_OVERFLOW;
    result += (uint64_t)sample.tv_nsec;
    *nanoseconds = result == 0U ? 1U : result;
    return NX_STATUS_OK;
}
