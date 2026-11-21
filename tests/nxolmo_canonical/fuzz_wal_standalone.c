/* Original NXOLMO WAL test linked against Base-NEXS production port. */
#include "nexs_block_memory.h"
#include "nexs_block_memory_device.h"
#include "nexs_crypto.h"
#include "nexs_journal_wal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_BLOCK_SIZE UINT32_C(512)
#define FUZZ_BLOCK_COUNT UINT64_C(256)
#define FUZZ_JOURNAL_BLOCKS UINT64_C(64)
#define FUZZ_MEDIA_BYTES (FUZZ_BLOCK_COUNT * FUZZ_BLOCK_SIZE)
#define FUZZ_STAGING_BYTES (FUZZ_BLOCK_SIZE * 16u)
#define FUZZ_ITERATIONS UINT32_C(100000)

static uint8_t volatile_media[FUZZ_MEDIA_BYTES];
static uint8_t durable_media[FUZZ_MEDIA_BYTES];
static uint8_t baseline_media[FUZZ_MEDIA_BYTES];
static uint8_t payload[FUZZ_BLOCK_SIZE];
static nx_block_request requests[8];
static nx_block_completion completions[8];
static nx_block_memory backend;
static nx_block_memory_device adapter;
static nx_journal journal;
static uint8_t scratch[FUZZ_BLOCK_SIZE];
static uint8_t staging[FUZZ_STAGING_BYTES];
static nx_journal_write_entry entries[16];

#define FUZZ_ASSERT(expression)                                                    do {                                                                               if (!(expression)) {                                                               (void)fprintf(stderr, "WAL fuzz assertion failed line=%d input=%u\n",                           __LINE__, (unsigned int)i);                                      return 1;                                                                  }                                                                          } while (0)

static uint64_t next_random(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12u;
    x ^= x << 25u;
    x ^= x >> 27u;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

static nx_status digest_of(const void *data, size_t size,
                           nx_digest256 *digest) {
    return nx_sha256_compute((nx_const_span){data, size}, digest);
}

static nx_status setup_backend(void) {
    nx_status status = nx_block_memory_init(
        &backend, volatile_media, durable_media, FUZZ_BLOCK_COUNT,
        FUZZ_BLOCK_SIZE, requests, 8u, completions, 8u);
    if (status != NX_STATUS_OK)
        return status;
    return nx_block_memory_device_init(&adapter, &backend);
}

static nx_status setup_journal(void) {
    return nx_journal_init(&journal, &adapter.device, 0u,
                           FUZZ_JOURNAL_BLOCKS, scratch, sizeof(scratch),
                           staging, sizeof(staging), entries, 16u);
}

int main(void) {
    nx_digest256 base_root;
    nx_digest256 committed_root;
    nx_digest256 observed_root;
    uint64_t random_state = UINT64_C(0x9e3779b97f4a7c15);
    uint32_t accepted = 0u;
    uint32_t rejected = 0u;
    uint32_t i = 0u;
    memset(payload, 0x6b, sizeof(payload));
    FUZZ_ASSERT(digest_of("fuzz-base-root", 14u, &base_root) == NX_STATUS_OK);
    FUZZ_ASSERT(digest_of("fuzz-committed-root", 19u, &committed_root) ==
                NX_STATUS_OK);
    FUZZ_ASSERT(setup_backend() == NX_STATUS_OK);
    FUZZ_ASSERT(setup_journal() == NX_STATUS_OK);
    FUZZ_ASSERT(nx_journal_format(&journal, &base_root) == NX_STATUS_OK);
    FUZZ_ASSERT(nx_journal_begin(&journal, &base_root) == NX_STATUS_OK);
    FUZZ_ASSERT(nx_journal_write(&journal, 100u, 1u, payload) == NX_STATUS_OK);
    FUZZ_ASSERT(nx_block_memory_device_fail_operation(
                    &adapter, 9u, NX_STATUS_IO_ERROR) == NX_STATUS_OK);
    FUZZ_ASSERT(nx_journal_commit(&journal, &committed_root) ==
                NX_STATUS_IO_ERROR);
    FUZZ_ASSERT(nx_block_memory_crash(&backend) == NX_STATUS_OK);
    memcpy(baseline_media, durable_media, (size_t)FUZZ_MEDIA_BYTES);

    for (i = 0u; i < FUZZ_ITERATIONS; ++i) {
        uint64_t random_value = next_random(&random_state);
        /* Mutate only the two superblocks and the committed log extent.
         * Bytes beyond the committed extent are intentionally unused and
         * must not be classified as corruption by this recovery harness. */
        size_t mutation_offset =
            (size_t)(random_value %
                     ((NX_JOURNAL_SUPERBLOCK_COUNT + UINT64_C(3)) *
                      FUZZ_BLOCK_SIZE));
        uint8_t mutation_mask = (uint8_t)(next_random(&random_state) | 1u);
        nx_status status;
        uint8_t observed[FUZZ_BLOCK_SIZE];

        /* Backend initialization zeroes its media buffers, so restore the
         * durable baseline only after initializing the adapter/backend. */
        FUZZ_ASSERT(setup_backend() == NX_STATUS_OK);
        memcpy(volatile_media, baseline_media, (size_t)FUZZ_MEDIA_BYTES);
        memcpy(durable_media, baseline_media, (size_t)FUZZ_MEDIA_BYTES);
        FUZZ_ASSERT(setup_journal() == NX_STATUS_OK);
        volatile_media[mutation_offset] ^= mutation_mask;
        status = nx_journal_mount(&journal);
        if (status == NX_STATUS_OK) {
            FUZZ_ASSERT(nx_journal_root_read(&journal, &observed_root) ==
                        NX_STATUS_OK);
            FUZZ_ASSERT(memcmp(observed_root.bytes, committed_root.bytes,
                               32u) == 0);
            FUZZ_ASSERT(nx_journal_validate(&journal) == NX_STATUS_OK);
            FUZZ_ASSERT(nx_block_device_read(&adapter.device, 100u, 1u,
                                             observed) == NX_STATUS_OK);
            FUZZ_ASSERT(memcmp(observed, payload, sizeof(payload)) == 0);
            accepted++;
        } else if (status == NX_STATUS_CORRUPT ||
                   status == NX_STATUS_INTEGRITY_FAILURE) {
            memset(observed, 0xff, sizeof(observed));
            FUZZ_ASSERT(nx_block_device_read(&adapter.device, 100u, 1u,
                                             observed) == NX_STATUS_OK);
            FUZZ_ASSERT(memcmp(observed, (uint8_t[FUZZ_BLOCK_SIZE]){0},
                               sizeof(observed)) == 0);
            rejected++;
        } else {
            (void)fprintf(stderr, "unexpected mount status=%u iteration=%u\n",
                          (unsigned int)status, (unsigned int)i);
            return 1;
        }
    }
    (void)printf("WAL mutation outcomes: %u recoverable, %u rejected\n",
                 (unsigned int)accepted, (unsigned int)rejected);
    FUZZ_ASSERT(accepted != 0u && rejected != 0u);
    (void)printf("WAL media mutation: PASS (%u inputs, %u recoverable, %u rejected)\n",
                 (unsigned int)FUZZ_ITERATIONS, (unsigned int)accepted,
                 (unsigned int)rejected);
    return 0;
}
