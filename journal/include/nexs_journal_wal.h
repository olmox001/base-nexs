/* Canonical NXOLMO WAL ABI adapted to Base-NEXS shared statuses and IDs.
 * Caller owns scratch/staging/entries; not a device capability token. */
#ifndef NEXS_JOURNAL_WAL_H
#define NEXS_JOURNAL_WAL_H

#include "nexs_block_device.h"
#include "nexs_checked.h"
#include "nexs_types.h"

#include <stddef.h>
#include <stdint.h>


#define NX_JOURNAL_ABI_VERSION UINT32_C(1)
#define NX_JOURNAL_SUPERBLOCK_COUNT UINT32_C(2)
#define NX_JOURNAL_SUPERBLOCK_HEADER_SIZE UINT32_C(160)
#define NX_JOURNAL_WRITE_HEADER_SIZE UINT32_C(80)
#define NX_JOURNAL_COMMIT_HEADER_SIZE UINT32_C(136)

typedef enum nx_journal_state {
    NX_JOURNAL_STATE_UNMOUNTED = 0,
    NX_JOURNAL_STATE_CLEAN = 1,
    NX_JOURNAL_STATE_ACTIVE = 2,
    NX_JOURNAL_STATE_COMMITTED = 3,
    NX_JOURNAL_STATE_RECOVERY_REQUIRED = 4
} nx_journal_state;

typedef struct nx_journal_write_entry {
    uint64_t first_block;
    uint64_t staging_offset;
    uint64_t payload_bytes;
    uint32_t block_count;
    uint32_t reserved;
} nx_journal_write_entry;

typedef struct nx_journal {
    nx_block_device *device;
    uint64_t first_block;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t abi_version;
    uint8_t *scratch;
    size_t scratch_size;
    uint8_t *staging;
    size_t staging_capacity;
    nx_journal_write_entry *entries;
    uint32_t entry_capacity;
    uint32_t write_count;
    uint64_t staged_bytes;
    uint64_t generation;
    uint64_t transaction_id;
    uint64_t committed_log_blocks;
    nx_digest256 root_digest;
    nx_digest256 base_root_digest;
    nx_digest256 transaction_digest;
    uint32_t state;
    uint32_t active;
} nx_journal;

nx_status nx_journal_init(nx_journal *journal, nx_block_device *device,
                          uint64_t first_block, uint64_t block_count,
                          void *block_scratch, size_t block_scratch_size,
                          void *staging, size_t staging_capacity,
                          nx_journal_write_entry *entries,
                          uint32_t entry_capacity);
nx_status nx_journal_format(nx_journal *journal,
                            const nx_digest256 *initial_root_digest);
nx_status nx_journal_mount(nx_journal *journal);
nx_status nx_journal_recover(nx_journal *journal);
nx_status nx_journal_begin(nx_journal *journal,
                           const nx_digest256 *expected_root_digest);
nx_status nx_journal_write(nx_journal *journal, uint64_t first_block,
                           uint32_t block_count, const void *block_data);
nx_status nx_journal_commit(nx_journal *journal,
                            const nx_digest256 *new_root_digest);
nx_status nx_journal_abort(nx_journal *journal);
nx_status nx_journal_root_read(const nx_journal *journal,
                               nx_digest256 *root_digest);
nx_status nx_journal_validate(const nx_journal *journal);


#endif
