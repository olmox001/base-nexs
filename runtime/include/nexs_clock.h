/* NXOLMO runtime contract ported to ONE Base-NEXS product ABI. */
#ifndef NEXS_CLOCK_H
#define NEXS_CLOCK_H

#include "nexs_checked.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_NANOSECONDS_UNKNOWN UINT64_MAX

/* Monotonic elapsed time in nanoseconds; never substitutes wall-clock time. */
nx_status nx_monotonic_time_ns(uint64_t *nanoseconds);
nx_status nx_monotonic_clock_resolution_ns(uint64_t *nanoseconds);

#ifdef __cplusplus
}
#endif

#endif
