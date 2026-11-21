/* NXOLMO AMD64 invariant-RDTSCP cycle provider; hosted AMD64 only. */
#include "nexs_cycle_counter.h"

#include "nexs_checked.h"

#include <stddef.h>

#include <cpuid.h>

static int nx_x86_rdtscp_supported(uint32_t *flags) {

    unsigned int max_leaf;
    unsigned int eax = 0u;
    unsigned int ebx = 0u;
    unsigned int ecx = 0u;
    unsigned int edx = 0u;

    max_leaf = __get_cpuid_max(UINT32_C(0x80000000), NULL);
    if (max_leaf < UINT32_C(0x80000001))
        return 0;
    __cpuid(UINT32_C(0x80000001), eax, ebx, ecx, edx);
    if ((edx & (UINT32_C(1) << 27)) == 0u)
        return 0;

    if (max_leaf < UINT32_C(0x80000007))
        return 0;
    __cpuid(UINT32_C(0x80000007), eax, ebx, ecx, edx);
    if ((edx & (UINT32_C(1) << 8)) == 0u)
        return 0;
    *flags |= NX_CYCLE_FLAG_INVARIANT;
    return 1;

}

nx_status nx_cycle_counter_init(nx_cycle_counter *counter) {
    uint32_t flags = NX_CYCLE_FLAG_ORDERED;

    if (counter == NULL ||
        ((uintptr_t)counter % (uintptr_t)_Alignof(nx_cycle_counter)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    counter->source = NX_CYCLE_SOURCE_NONE;
    counter->flags = NX_CYCLE_FLAG_UNKNOWN;

    if (!nx_x86_rdtscp_supported(&flags))
        return NX_STATUS_NOT_SUPPORTED;

    counter->source = NX_CYCLE_SOURCE_X86_TSC;
    counter->flags = flags;
    return NX_STATUS_OK;
}

nx_status nx_cycle_counter_read(const nx_cycle_counter *counter,
                                nx_cycle_sample *sample) {
    if (sample == NULL ||
        ((uintptr_t)sample % (uintptr_t)_Alignof(nx_cycle_sample)) != 0u ||
        (counter != NULL &&
         (((uintptr_t)counter % (uintptr_t)_Alignof(nx_cycle_counter)) != 0u ||
          nx_pointer_ranges_overlap(counter, sizeof(*counter),
                                    sample, sizeof(*sample)) != 0)))
        return NX_STATUS_INVALID_ARGUMENT;
    sample->value = NX_CYCLES_UNKNOWN;
    sample->source = NX_CYCLE_SOURCE_NONE;
    sample->flags = NX_CYCLE_FLAG_UNKNOWN;
    sample->auxiliary = 0u;
    sample->reserved = 0u;

    if (counter == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (counter->source != NX_CYCLE_SOURCE_X86_TSC)
        return NX_STATUS_NOT_SUPPORTED;


    {
        uint32_t low = 0u;
        uint32_t high = 0u;
        uint32_t auxiliary = 0u;
        uint64_t value;

        __asm__ __volatile__("lfence\n\trdtscp\n\tlfence"
                             : "=a"(low), "=d"(high), "=c"(auxiliary)
                             :
                             : "memory");
        (void)auxiliary;
        value = ((uint64_t)high << 32) | (uint64_t)low;
        sample->value = value;
        sample->source = NX_CYCLE_SOURCE_X86_TSC;
        sample->flags = counter->flags | NX_CYCLE_FLAG_ORDERED;
        sample->auxiliary = auxiliary;
        sample->reserved = 0u;
        return NX_STATUS_OK;
    }

}
