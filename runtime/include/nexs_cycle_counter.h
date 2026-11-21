/* NXOLMO runtime contract ported to ONE Base-NEXS product ABI. */
#ifndef NEXS_CYCLE_COUNTER_H
#define NEXS_CYCLE_COUNTER_H

#include "nexs_checked.h"
#include "nexs_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nx_cycle_source {
    NX_CYCLE_SOURCE_NONE = 0,
    NX_CYCLE_SOURCE_X86_TSC = 1
} nx_cycle_source;

enum {
    NX_CYCLE_FLAG_INVARIANT = UINT32_C(1) << 0,
    NX_CYCLE_FLAG_ORDERED = UINT32_C(1) << 1,
    NX_CYCLE_FLAG_UNKNOWN = UINT32_C(1) << 2
};

typedef struct nx_cycle_counter {
    uint32_t source;
    uint32_t flags;
} nx_cycle_counter;

typedef struct nx_cycle_sample {
    uint64_t value;
    uint32_t source;
    uint32_t flags;
    /* RDTSCP TSC_AUX value; used to reject samples across CPU migration. */
    uint32_t auxiliary;
    uint32_t reserved;
} nx_cycle_sample;

nx_status nx_cycle_counter_init(nx_cycle_counter *counter);
nx_status nx_cycle_counter_read(const nx_cycle_counter *counter,
                                nx_cycle_sample *sample);

#ifdef __cplusplus
}
#endif

#endif
