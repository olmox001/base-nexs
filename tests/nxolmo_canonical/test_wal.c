/* Original NXOLMO WAL test linked against Base-NEXS production port. */
#include "nexs_block_memory.h"
#include "nexs_block_memory_device.h"
#include "nexs_crypto.h"
#include "nexs_journal_wal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BLOCK_SIZE UINT32_C(512)
#define TEST_BLOCK_COUNT UINT64_C(256)
#define TEST_JOURNAL_FIRST UINT64_C(0)
#define TEST_JOURNAL_BLOCKS UINT64_C(64)
#define TEST_STAGING_SIZE (TEST_BLOCK_SIZE * 16u)
#define TEST_WRITE_CAPACITY UINT32_C(16)

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,   \
                          __LINE__, #expression);                             \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static uint8_t volatile_media[TEST_BLOCK_COUNT * TEST_BLOCK_SIZE];
static uint8_t durable_media[TEST_BLOCK_COUNT * TEST_BLOCK_SIZE];
static nx_block_request requests[8];
static nx_block_completion completions[8];
static nx_block_memory memory_backend;
static nx_block_memory_device memory_adapter;
static nx_journal journal;
static nx_journal recovered;
static uint8_t scratch_a[TEST_BLOCK_SIZE];
static uint8_t scratch_b[TEST_BLOCK_SIZE];
static uint8_t staging_a[TEST_STAGING_SIZE];
static uint8_t staging_b[TEST_STAGING_SIZE];
static nx_journal_write_entry entries_a[TEST_WRITE_CAPACITY];
static nx_journal_write_entry entries_b[TEST_WRITE_CAPACITY];

static nx_status make_digest(const void *data, size_t size,
                             nx_digest256 *digest) {
    return nx_sha256_compute((nx_const_span){data, size}, digest);
}

static nx_status initialize_media(void) {
    nx_status status = nx_block_memory_init(
        &memory_backend, volatile_media, durable_media, TEST_BLOCK_COUNT,
        TEST_BLOCK_SIZE, requests, 8u, completions, 8u);
    if (status != NX_STATUS_OK)
        return status;
    return nx_block_memory_device_init(&memory_adapter, &memory_backend);
}

static nx_status initialize_journal(nx_journal *target, uint8_t *scratch,
                                    uint8_t *staging,
                                    nx_journal_write_entry *entries) {
    memset(scratch, 0xa5, TEST_BLOCK_SIZE);
    memset(staging, 0xa5, TEST_STAGING_SIZE);
    memset(entries, 0xa5,
           TEST_WRITE_CAPACITY * sizeof(*entries));
    return nx_journal_init(target, &memory_adapter.device,
                           TEST_JOURNAL_FIRST, TEST_JOURNAL_BLOCKS,
                           scratch, TEST_BLOCK_SIZE, staging,
                           TEST_STAGING_SIZE, entries, TEST_WRITE_CAPACITY);
}

static int test_commit_and_mount(void) {
    nx_digest256 root0;
    nx_digest256 root1;
    nx_digest256 observed;
    uint8_t payload_a[TEST_BLOCK_SIZE * 2u];
    uint8_t payload_b[TEST_BLOCK_SIZE];
    memset(payload_a, 0x3cu, sizeof(payload_a));
    memset(payload_b, 0xa7, sizeof(payload_b));
    CHECK(make_digest("root-0", 6u, &root0) == NX_STATUS_OK);
    CHECK(make_digest("root-1", 6u, &root1) == NX_STATUS_OK);
    CHECK(initialize_media() == NX_STATUS_OK);
    CHECK(initialize_journal(&journal, scratch_a, staging_a, entries_a) ==
          NX_STATUS_OK);
    CHECK(memcmp(scratch_a, (uint8_t[TEST_BLOCK_SIZE]){0},
                 sizeof(scratch_a)) == 0);
    CHECK(memcmp(staging_a, (uint8_t[TEST_STAGING_SIZE]){0},
                 sizeof(staging_a)) == 0);
    CHECK(memcmp(entries_a, (nx_journal_write_entry[TEST_WRITE_CAPACITY]){0},
                 sizeof(entries_a)) == 0);
    CHECK(nx_journal_format(&journal, &root0) == NX_STATUS_OK);
    CHECK(nx_journal_root_read(&journal, &observed) == NX_STATUS_OK);
    CHECK(memcmp(observed.bytes, root0.bytes, 32u) == 0);

    CHECK(nx_journal_begin(&journal, &root0) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 100u, 2u, payload_a) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 110u, 1u, payload_b) == NX_STATUS_OK);
    CHECK(nx_journal_commit(&journal, &root1) == NX_STATUS_OK);
    CHECK(journal.state == NX_JOURNAL_STATE_CLEAN);
    CHECK(memcmp(volatile_media + 100u * TEST_BLOCK_SIZE, payload_a,
                 sizeof(payload_a)) == 0);
    CHECK(memcmp(volatile_media + 110u * TEST_BLOCK_SIZE, payload_b,
                 sizeof(payload_b)) == 0);
    CHECK(nx_journal_root_read(&journal, &observed) == NX_STATUS_OK);
    CHECK(memcmp(observed.bytes, root1.bytes, 32u) == 0);
    CHECK(nx_journal_begin(&journal, &root0) == NX_STATUS_CONFLICT);

    CHECK(initialize_journal(&recovered, scratch_b, staging_b, entries_b) ==
          NX_STATUS_OK);
    CHECK(nx_journal_mount(&recovered) == NX_STATUS_OK);
    CHECK(nx_journal_root_read(&recovered, &observed) == NX_STATUS_OK);
    CHECK(memcmp(observed.bytes, root1.bytes, 32u) == 0);
    CHECK(nx_journal_validate(&recovered) == NX_STATUS_OK);
    return 0;
}

static int test_abort_and_region_protection(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    uint8_t payload[TEST_BLOCK_SIZE];
    uint8_t observed[TEST_BLOCK_SIZE];
    memset(payload, 0x55, sizeof(payload));
    CHECK(make_digest("root-1", 6u, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-2", 6u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&journal, &root) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 10u, 1u, payload) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_journal_write(&journal, 120u, 1u, payload) == NX_STATUS_OK);
    CHECK(nx_journal_abort(&journal) == NX_STATUS_OK);
    CHECK(memcmp(scratch_a, (uint8_t[TEST_BLOCK_SIZE]){0},
                 sizeof(scratch_a)) == 0);
    CHECK(memcmp(staging_a, (uint8_t[TEST_STAGING_SIZE]){0},
                 sizeof(staging_a)) == 0);
    CHECK(memcmp(entries_a, (nx_journal_write_entry[TEST_WRITE_CAPACITY]){0},
                 sizeof(entries_a)) == 0);
    CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
    CHECK(initialize_journal(&recovered, scratch_b, staging_b, entries_b) ==
          NX_STATUS_OK);
    CHECK(nx_journal_mount(&recovered) == NX_STATUS_OK);
    memset(observed, 0xff, sizeof(observed));
    CHECK(nx_block_device_read(&memory_adapter.device, 120u, 1u,
                               observed) == NX_STATUS_OK);
    CHECK(memcmp(observed, (uint8_t[TEST_BLOCK_SIZE]){0},
                 sizeof(observed)) == 0);
    CHECK(nx_journal_root_read(&recovered, &root) == NX_STATUS_OK);
    CHECK(memcmp(root.bytes, next_root.bytes, 32u) != 0);
    return 0;
}

static int test_replay_after_home_write_failure(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    nx_digest256 observed_root;
    uint8_t payload[TEST_BLOCK_SIZE * 2u];
    memset(payload, 0x91, sizeof(payload));
    CHECK(nx_journal_root_read(&journal, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-replay-a", 13u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&journal, &root) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 130u, 2u, payload) == NX_STATUS_OK);
    CHECK(nx_block_memory_device_fail_operation(&memory_adapter, 9u,
                                                NX_STATUS_IO_ERROR) ==
          NX_STATUS_OK);
    CHECK(nx_journal_commit(&journal, &next_root) == NX_STATUS_IO_ERROR);
    CHECK(journal.state == NX_JOURNAL_STATE_COMMITTED);
    CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
    CHECK(initialize_journal(&recovered, scratch_b, staging_b, entries_b) ==
          NX_STATUS_OK);
    CHECK(nx_journal_mount(&recovered) == NX_STATUS_OK);
    CHECK(recovered.state == NX_JOURNAL_STATE_CLEAN);
    CHECK(memcmp(volatile_media + 130u * TEST_BLOCK_SIZE, payload,
                 sizeof(payload)) == 0);
    CHECK(nx_journal_root_read(&recovered, &observed_root) == NX_STATUS_OK);
    CHECK(memcmp(observed_root.bytes, next_root.bytes, 32u) == 0);
    return 0;
}

static int test_replay_after_home_flush_failure(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    uint8_t payload[TEST_BLOCK_SIZE];
    memset(payload, 0x6d, sizeof(payload));
    CHECK(nx_journal_root_read(&recovered, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-replay-b", 13u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&recovered, &root) == NX_STATUS_OK);
    CHECK(nx_journal_write(&recovered, 140u, 1u, payload) == NX_STATUS_OK);
    CHECK(nx_block_memory_device_fail_operation(&memory_adapter, 10u,
                                                NX_STATUS_IO_ERROR) ==
          NX_STATUS_OK);
    CHECK(nx_journal_commit(&recovered, &next_root) == NX_STATUS_IO_ERROR);
    CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
    CHECK(nx_journal_mount(&journal) == NX_STATUS_OK);
    CHECK(memcmp(volatile_media + 140u * TEST_BLOCK_SIZE, payload,
                 sizeof(payload)) == 0);
    return 0;
}

static int test_crash_point_matrix(void) {
    uint64_t failure_point;
    for (failure_point = 1u; failure_point <= 14u; ++failure_point) {
        nx_digest256 root_before;
        nx_digest256 root_after;
        nx_digest256 observed_root;
        uint8_t payload[TEST_BLOCK_SIZE];
        uint8_t observed[TEST_BLOCK_SIZE];
        uint64_t target_block = 100u + failure_point;
        nx_status status;
        memset(volatile_media, 0, sizeof(volatile_media));
        memset(durable_media, 0, sizeof(durable_media));
        memset(requests, 0, sizeof(requests));
        memset(completions, 0, sizeof(completions));
        memset(payload, (int)(0x30u + failure_point), sizeof(payload));
        CHECK(make_digest("matrix-base", 11u, &root_before) == NX_STATUS_OK);
        CHECK(make_digest(&failure_point, sizeof(failure_point),
                          &root_after) == NX_STATUS_OK);
        CHECK(initialize_media() == NX_STATUS_OK);
        CHECK(initialize_journal(&journal, scratch_a, staging_a, entries_a) ==
              NX_STATUS_OK);
        CHECK(nx_journal_format(&journal, &root_before) == NX_STATUS_OK);
        CHECK(nx_journal_begin(&journal, &root_before) == NX_STATUS_OK);
        CHECK(nx_journal_write(&journal, target_block, 1u, payload) ==
              NX_STATUS_OK);
        CHECK(nx_block_memory_device_fail_operation(
                  &memory_adapter, failure_point, NX_STATUS_IO_ERROR) ==
              NX_STATUS_OK);
        status = nx_journal_commit(&journal, &root_after);
        CHECK(status == NX_STATUS_IO_ERROR);
        CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
        CHECK(initialize_journal(&recovered, scratch_b, staging_b,
                                 entries_b) == NX_STATUS_OK);
        CHECK(nx_journal_mount(&recovered) == NX_STATUS_OK);
        CHECK(nx_journal_root_read(&recovered, &observed_root) ==
              NX_STATUS_OK);
        memset(observed, 0xff, sizeof(observed));
        CHECK(nx_block_device_read(&memory_adapter.device, target_block, 1u,
                                   observed) == NX_STATUS_OK);
        if (failure_point <= 6u) {
            CHECK(memcmp(observed_root.bytes, root_before.bytes, 32u) == 0);
            CHECK(memcmp(observed, (uint8_t[TEST_BLOCK_SIZE]){0},
                         sizeof(observed)) == 0);
        } else {
            CHECK(memcmp(observed_root.bytes, root_after.bytes, 32u) == 0);
            CHECK(memcmp(observed, payload, sizeof(payload)) == 0);
        }
        CHECK(nx_journal_validate(&recovered) == NX_STATUS_OK);
    }
    return 0;
}

static int test_corrupt_log_preflight_is_atomic(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    uint8_t payload[TEST_BLOCK_SIZE];
    uint8_t observed[TEST_BLOCK_SIZE];
    memset(payload, 0xe3, sizeof(payload));
    CHECK(nx_journal_root_read(&recovered, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-corrupt", 12u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&recovered, &root) == NX_STATUS_OK);
    CHECK(nx_journal_write(&recovered, 150u, 1u, payload) == NX_STATUS_OK);
    CHECK(nx_block_memory_device_fail_operation(&memory_adapter, 9u,
                                                NX_STATUS_IO_ERROR) ==
          NX_STATUS_OK);
    CHECK(nx_journal_commit(&recovered, &next_root) == NX_STATUS_IO_ERROR);
    CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
    /* One write record occupies header block 2 and payload block 3. */
    volatile_media[3u * TEST_BLOCK_SIZE + 17u] ^= UINT8_C(0x80);
    CHECK(nx_journal_mount(&journal) == NX_STATUS_INTEGRITY_FAILURE);
    memset(observed, 0xff, sizeof(observed));
    CHECK(nx_block_device_read(&memory_adapter.device, 150u, 1u,
                               observed) == NX_STATUS_OK);
    CHECK(memcmp(observed, (uint8_t[TEST_BLOCK_SIZE]){0},
                 sizeof(observed)) == 0);
    return 0;
}

static int test_root_only_commit_and_overlapping_writes(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    nx_digest256 final_root;
    uint8_t first[TEST_BLOCK_SIZE];
    uint8_t second[TEST_BLOCK_SIZE];
    memset(first, 0x11, sizeof(first));
    memset(second, 0x22, sizeof(second));
    CHECK(nx_journal_root_read(&journal, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-only", 9u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&journal, &root) == NX_STATUS_OK);
    CHECK(nx_journal_commit(&journal, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_root_read(&journal, &final_root) == NX_STATUS_OK);
    CHECK(memcmp(final_root.bytes, next_root.bytes, 32u) == 0);

    CHECK(nx_journal_begin(&journal, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 180u, 1u, first) == NX_STATUS_OK);
    CHECK(nx_journal_write(&journal, 180u, 1u, second) == NX_STATUS_OK);
    CHECK(make_digest("root-overlap", 12u, &final_root) == NX_STATUS_OK);
    CHECK(nx_journal_commit(&journal, &final_root) == NX_STATUS_OK);
    CHECK(memcmp(volatile_media + 180u * TEST_BLOCK_SIZE, second,
                 sizeof(second)) == 0);
    return 0;
}

static int test_superblock_replica_recovery(void) {
    nx_digest256 root;
    nx_digest256 next_root;
    memset(&root, 0, sizeof(root));
    CHECK(nx_journal_root_read(&journal, &root) == NX_STATUS_OK);
    CHECK(make_digest("root-replica", 12u, &next_root) == NX_STATUS_OK);
    CHECK(nx_journal_begin(&journal, &root) == NX_STATUS_OK);
    CHECK(nx_journal_commit(&journal, &next_root) == NX_STATUS_OK);
    CHECK(nx_block_memory_crash(&memory_backend) == NX_STATUS_OK);
    /* Damage slot zero; the independently flushed slot one remains valid. */
    volatile_media[9u] ^= UINT8_C(0x01);
    durable_media[9u] ^= UINT8_C(0x01);
    CHECK(nx_journal_mount(&recovered) == NX_STATUS_OK);
    CHECK(nx_journal_root_read(&recovered, &root) == NX_STATUS_OK);
    CHECK(memcmp(root.bytes, next_root.bytes, 32u) == 0);
    return 0;
}

int main(void) {
    CHECK(test_commit_and_mount() == 0);
    CHECK(test_abort_and_region_protection() == 0);
    CHECK(test_replay_after_home_write_failure() == 0);
    CHECK(test_replay_after_home_flush_failure() == 0);
    CHECK(test_root_only_commit_and_overlapping_writes() == 0);
    CHECK(test_superblock_replica_recovery() == 0);
    CHECK(test_crash_point_matrix() == 0);
    CHECK(test_corrupt_log_preflight_is_atomic() == 0);
    puts("NXOLMO WAL transaction/recovery tests: PASS");
    return 0;
}
