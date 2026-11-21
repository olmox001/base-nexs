/* Billion-call independent CPU stress of the actual NXOLMO 9P product codec.
 * Counts each real decode + validation; throughput is OPERATIONS/s, NOT FLOPS.
 * No throughput claim is made without this run finishing successfully. */
#include "nexs_p9_frame.h"
#include "nexs_fs9p.h"
#include "nexs_endian.h"
#include "nexs_clock.h"
#include "nexs_cycle_counter.h"
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint64_t start;
    uint64_t iterations;
    uint64_t passed;
    uint64_t rejected;
    uint64_t cycles;
    int cycle_known;
    int failed;
} worker;

static void *run_worker(void *context) {
    worker *w = (worker *)context;
    uint8_t wire[11] = {11u, 0u, 0u, 0u, P9_TCLUNK, 0u, 0u, 0u, 0u, 0u, 0u};
    uint64_t index;
    nx_cycle_counter counter;
    nx_cycle_sample cycle_begin, cycle_end;
    int sampled = nx_cycle_counter_init(&counter) == NX_STATUS_OK &&
                  nx_cycle_counter_read(&counter, &cycle_begin) == NX_STATUS_OK;
    for (index = 0u; index < w->iterations; ++index) {
        nx_9p_frame_view frame;
        nx_const_span input = {wire, sizeof(wire)};
        size_t consumed = 0u;
        uint64_t id = w->start + index;
        int bad = (id % UINT64_C(1000000)) == 0u;
        nx_status status;
        nx_store_le16(wire + 5u, (uint16_t)id);
        nx_store_le32(wire + 7u, (uint32_t)id);
        wire[0] = bad ? 12u : 11u;
        status = nx_9p_frame_decode(input, 8192u, &frame, &consumed);
        if (bad != 0) {
            if (status != NX_STATUS_TRUNCATED) {
                w->failed = 1; return NULL;
            }
            w->rejected++;
        } else {
            if (status != NX_STATUS_OK || consumed != sizeof(wire) ||
                frame.tag != (uint16_t)id ||
                nx_load_le32(frame.body) != (uint32_t)id ||
                nx_9p_request_validate(&frame) != NX_STATUS_OK) {
                w->failed = 1; return NULL;
            }
            w->passed++;
        }
    }
    if (sampled &&
        nx_cycle_counter_read(&counter, &cycle_end) == NX_STATUS_OK &&
        cycle_begin.source == cycle_end.source &&
        cycle_begin.auxiliary == cycle_end.auxiliary &&
        cycle_end.value >= cycle_begin.value) {
        w->cycles = cycle_end.value - cycle_begin.value;
        w->cycle_known = 1;
    }
    return NULL;
}

static int parse_count(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0u)
        return 0;
    *out = (uint64_t)value;
    return 1;
}

int main(int argc, char **argv) {
    uint64_t operations = UINT64_C(200000000);
    uint64_t workers64 = UINT64_C(8);
    pthread_t *threads;
    worker *workers;
    uint64_t before = 0u, after = 0u, total = 0u, rejected = 0u;
    uint64_t i, launched = 0u, measured_cycles = 0u;
    int complete_cycle_sample = 1;
    int failed = 0;
    if ((argc > 1 && !parse_count(argv[1], &operations)) ||
        (argc > 2 && !parse_count(argv[2], &workers64)) ||
        argc > 3 || workers64 > SIZE_MAX / sizeof(*workers))
        return 2;
    threads = calloc((size_t)workers64, sizeof(*threads));
    workers = calloc((size_t)workers64, sizeof(*workers));
    if (threads == NULL || workers == NULL)
        return 2;
    if (nx_monotonic_time_ns(&before) != NX_STATUS_OK)
        return 2;
    for (i = 0u; i < workers64; ++i) {
        workers[i].start = operations / workers64 * i +
                           (i < operations % workers64 ? i : operations % workers64);
        workers[i].iterations = operations / workers64 +
                                (i < operations % workers64 ? 1u : 0u);
        if (pthread_create(&threads[i], NULL, run_worker, &workers[i]) != 0) {
            failed = 1;
            break;
        }
        launched++;
    }
    for (i = 0u; i < launched; ++i) {
        if (pthread_join(threads[i], NULL) != 0)
            failed = 1;
        total += workers[i].passed;
        rejected += workers[i].rejected;
        if (workers[i].failed != 0)
            failed = 1;
        if (workers[i].cycle_known == 0 ||
            UINT64_MAX - measured_cycles < workers[i].cycles) {
            complete_cycle_sample = 0;
        } else {
            measured_cycles += workers[i].cycles;
        }
    }
    if (nx_monotonic_time_ns(&after) != NX_STATUS_OK || after <= before)
        failed = 1;
    if (failed == 0 && total + rejected != operations)
        failed = 1;
    if (failed == 0) {
        double seconds = (double)(after - before) / 1e9;
        printf("P9 codec workload: verified=%" PRIu64
               " malformed_rejected=%" PRIu64
               " total=%" PRIu64 " workers=%" PRIu64
               " seconds=%.6f Gops_per_s=%.6f PASS\n",
               total, rejected, operations, workers64, seconds,
               (double)operations / seconds / 1e9);
        if (complete_cycle_sample && operations != 0u)
            printf("P9_REAL_CPU_CYCLES known=1 batch_cycles=%" PRIu64
                   " mean_cycles_per_codec_request=%.6f "
                   "per_request_percentiles=not_sampled\n",
                   measured_cycles, (double)measured_cycles / operations);
        else
            puts("P9_REAL_CPU_CYCLES known=0 "
                 "reason=provider_unavailable_or_cpu_migration");
    } else
        fprintf(stderr, "P9 billion workload FAILED: validated=%" PRIu64
                " rejected=%" PRIu64 " expected=%" PRIu64 "\n",
                total, rejected, operations);
    free(workers);
    free(threads);
    return failed;
}
