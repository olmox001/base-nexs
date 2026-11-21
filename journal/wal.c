/* Pinned NXOLMO WAL port; retained original transaction semantics.
 * This product implementation is not an NXEC authorization bypass. */
#include "nexs_journal_wal.h"

#include "nexs_checked.h"
#include "nexs_crypto.h"
#include "nexs_crypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NX_JOURNAL_FORMAT_VERSION UINT16_C(1)
#define NX_JOURNAL_STATE_DISK_CLEAN UINT32_C(1)
#define NX_JOURNAL_STATE_DISK_COMMITTED UINT32_C(2)
#define NX_JOURNAL_SB_CRC_OFFSET UINT32_C(144)
#define NX_JOURNAL_WRITE_CRC_OFFSET UINT32_C(72)
#define NX_JOURNAL_COMMIT_CRC_OFFSET UINT32_C(128)
#define NX_JOURNAL_WRITE_DESCRIPTOR_SIZE UINT32_C(48)

typedef struct nx_journal_superblock {
    uint64_t generation;
    uint64_t transaction_id;
    uint64_t log_blocks;
    uint32_t state;
    uint32_t write_count;
    nx_digest256 transaction_digest;
    nx_digest256 base_root_digest;
    nx_digest256 root_digest;
} nx_journal_superblock;

typedef struct nx_journal_record {
    uint64_t transaction_id;
    uint64_t first_block;
    uint64_t payload_bytes;
    uint32_t ordinal;
    uint32_t block_count;
    nx_digest256 payload_digest;
} nx_journal_record;

typedef struct nx_journal_commit_record {
    uint64_t transaction_id;
    uint64_t data_record_blocks;
    uint32_t write_count;
    nx_digest256 transaction_digest;
    nx_digest256 base_root_digest;
    nx_digest256 root_digest;
} nx_journal_commit_record;

static void nx_journal_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8u);
}

static void nx_journal_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8u);
    p[2] = (uint8_t)(v >> 16u);
    p[3] = (uint8_t)(v >> 24u);
}

static void nx_journal_put_u64(uint8_t *p, uint64_t v) {
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}

static uint16_t nx_journal_get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t nx_journal_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t nx_journal_get_u64(const uint8_t *p) {
    uint64_t v = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        v |= (uint64_t)p[i] << (i * 8u);
    return v;
}

static int nx_journal_bytes_zero(const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        if (p[i] != 0u)
            return 0;
    }
    return 1;
}

static int nx_journal_digest_equal(const nx_digest256 *a,
                                   const nx_digest256 *b) {
    return nx_crypto_constant_time_equal(a->bytes, b->bytes,
                                         sizeof(a->bytes));
}

static nx_status nx_journal_block_crc_write(nx_journal *journal,
                                            uint32_t crc_offset) {
    uint64_t crc = 0u;
    nx_status status;
    nx_journal_put_u64(journal->scratch + crc_offset, 0u);
    status = nx_crc64_ecma(
        (nx_const_span){journal->scratch, (size_t)journal->block_size}, &crc);
    if (status != NX_STATUS_OK)
        return status;
    nx_journal_put_u64(journal->scratch + crc_offset, crc);
    return NX_STATUS_OK;
}

static nx_status nx_journal_block_crc_check(nx_journal *journal,
                                            uint32_t crc_offset) {
    uint64_t expected = nx_journal_get_u64(journal->scratch + crc_offset);
    uint64_t actual = 0u;
    nx_status status;
    nx_journal_put_u64(journal->scratch + crc_offset, 0u);
    status = nx_crc64_ecma(
        (nx_const_span){journal->scratch, (size_t)journal->block_size},
        &actual);
    nx_journal_put_u64(journal->scratch + crc_offset, expected);
    if (status != NX_STATUS_OK)
        return status;
    return actual == expected ? NX_STATUS_OK : NX_STATUS_INTEGRITY_FAILURE;
}

static int nx_journal_region_overlaps(const nx_journal *journal,
                                      uint64_t first_block,
                                      uint32_t block_count) {
    uint64_t end;
    uint64_t journal_end;
    if (nx_u64_add(first_block, (uint64_t)block_count, &end) != NX_STATUS_OK ||
        nx_u64_add(journal->first_block, journal->block_count,
                   &journal_end) != NX_STATUS_OK)
        return 1;
    return first_block < journal_end && journal->first_block < end;
}

static nx_status nx_journal_device_range(const nx_journal *journal,
                                         uint64_t first_block,
                                         uint32_t block_count) {
    uint64_t end;
    nx_status status = nx_u64_add(first_block, (uint64_t)block_count, &end);
    if (status != NX_STATUS_OK)
        return status;
    if (end > journal->device->block_count)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_journal_region_overlaps(journal, first_block, block_count) != 0)
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static nx_status nx_journal_write_superblock_slot(
    nx_journal *journal, const nx_journal_superblock *superblock,
    uint64_t slot) {
    nx_status status;
    memset(journal->scratch, 0, (size_t)journal->block_size);
    memcpy(journal->scratch, "NXJS", 4u);
    nx_journal_put_u16(journal->scratch + 4u, NX_JOURNAL_FORMAT_VERSION);
    nx_journal_put_u16(journal->scratch + 6u,
                       (uint16_t)NX_JOURNAL_SUPERBLOCK_HEADER_SIZE);
    nx_journal_put_u64(journal->scratch + 8u, superblock->generation);
    nx_journal_put_u32(journal->scratch + 16u, superblock->state);
    nx_journal_put_u64(journal->scratch + 24u,
                       superblock->transaction_id);
    nx_journal_put_u64(journal->scratch + 32u, superblock->log_blocks);
    nx_journal_put_u32(journal->scratch + 40u, superblock->write_count);
    memcpy(journal->scratch + 48u,
           superblock->transaction_digest.bytes, 32u);
    memcpy(journal->scratch + 80u,
           superblock->base_root_digest.bytes, 32u);
    memcpy(journal->scratch + 112u, superblock->root_digest.bytes, 32u);
    status = nx_journal_block_crc_write(journal, NX_JOURNAL_SB_CRC_OFFSET);
    if (status != NX_STATUS_OK)
        return status;
    return nx_block_device_write(journal->device,
                                 journal->first_block + slot, 1u,
                                 journal->scratch);
}

static nx_status nx_journal_write_superblock(
    nx_journal *journal, const nx_journal_superblock *superblock) {
    return nx_journal_write_superblock_slot(
        journal, superblock, superblock->generation & UINT64_C(1));
}

static nx_status nx_journal_superblock_header_validate(
    nx_journal *journal) {
    uint16_t header_size;
    if (memcmp(journal->scratch, "NXJS", 4u) != 0 ||
        nx_journal_get_u16(journal->scratch + 4u) !=
            NX_JOURNAL_FORMAT_VERSION)
        return NX_STATUS_CORRUPT;
    header_size = nx_journal_get_u16(journal->scratch + 6u);
    if (header_size != NX_JOURNAL_SUPERBLOCK_HEADER_SIZE ||
        !nx_journal_bytes_zero(journal->scratch + 20u, 4u) ||
        !nx_journal_bytes_zero(journal->scratch + 44u, 4u) ||
        !nx_journal_bytes_zero(journal->scratch + 152u,
                               (size_t)journal->block_size - 152u))
        return NX_STATUS_CORRUPT;
    return nx_journal_block_crc_check(journal, NX_JOURNAL_SB_CRC_OFFSET);
}

static nx_status nx_journal_superblock_semantics_validate(
    const nx_journal *journal, const nx_journal_superblock *superblock) {
    uint64_t max_log_blocks = journal->block_count -
                              NX_JOURNAL_SUPERBLOCK_COUNT;
    if (superblock->generation == 0u)
        return NX_STATUS_CORRUPT;
    if (superblock->state == NX_JOURNAL_STATE_DISK_CLEAN) {
        if (superblock->log_blocks != 0u || superblock->write_count != 0u ||
            !nx_journal_bytes_zero(superblock->transaction_digest.bytes, 32u) ||
            !nx_journal_bytes_zero(superblock->base_root_digest.bytes, 32u))
            return NX_STATUS_CORRUPT;
    } else if (superblock->state == NX_JOURNAL_STATE_DISK_COMMITTED) {
        if (superblock->transaction_id == 0u ||
            superblock->log_blocks == 0u ||
            superblock->log_blocks > max_log_blocks ||
            superblock->write_count > journal->entry_capacity)
            return NX_STATUS_CORRUPT;
    } else {
        return NX_STATUS_CORRUPT;
    }
    return NX_STATUS_OK;
}

static nx_status nx_journal_parse_superblock(
    nx_journal *journal, nx_journal_superblock *superblock) {
    nx_status status = nx_journal_superblock_header_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    memset(superblock, 0, sizeof(*superblock));
    superblock->generation = nx_journal_get_u64(journal->scratch + 8u);
    superblock->state = nx_journal_get_u32(journal->scratch + 16u);
    superblock->transaction_id = nx_journal_get_u64(journal->scratch + 24u);
    superblock->log_blocks = nx_journal_get_u64(journal->scratch + 32u);
    superblock->write_count = nx_journal_get_u32(journal->scratch + 40u);
    memcpy(superblock->transaction_digest.bytes, journal->scratch + 48u, 32u);
    memcpy(superblock->base_root_digest.bytes, journal->scratch + 80u, 32u);
    memcpy(superblock->root_digest.bytes, journal->scratch + 112u, 32u);
    return nx_journal_superblock_semantics_validate(journal, superblock);
}

static int nx_journal_superblock_equal(const nx_journal_superblock *a,
                                       const nx_journal_superblock *b) {
    return a->generation == b->generation &&
           a->transaction_id == b->transaction_id &&
           a->log_blocks == b->log_blocks &&
           a->state == b->state &&
           a->write_count == b->write_count &&
           nx_journal_digest_equal(&a->transaction_digest,
                                   &b->transaction_digest) &&
           nx_journal_digest_equal(&a->base_root_digest,
                                   &b->base_root_digest) &&
           nx_journal_digest_equal(&a->root_digest, &b->root_digest);
}

static nx_status nx_journal_select_superblock_pair(
    const nx_journal_superblock *first,
    const nx_journal_superblock *second,
    nx_journal_superblock *selected) {
    const nx_journal_superblock *high;
    const nx_journal_superblock *low;
    uint64_t low_generation;
    uint64_t high_generation;
    if (first->generation == second->generation) {
        if (!nx_journal_superblock_equal(first, second))
            return NX_STATUS_CORRUPT;
        *selected = *first;
        return NX_STATUS_OK;
    }
    low_generation = first->generation < second->generation
                         ? first->generation : second->generation;
    high_generation = first->generation > second->generation
                          ? first->generation : second->generation;
    if (high_generation - low_generation != 1u)
        return NX_STATUS_CORRUPT;
    high = first->generation > second->generation ? first : second;
    low = first->generation > second->generation ? second : first;
    if (high->state == NX_JOURNAL_STATE_DISK_COMMITTED) {
        if (low->state != NX_JOURNAL_STATE_DISK_CLEAN ||
            low->transaction_id == UINT64_MAX ||
            high->transaction_id != low->transaction_id + 1u ||
            !nx_journal_digest_equal(&high->base_root_digest,
                                     &low->root_digest))
            return NX_STATUS_CORRUPT;
    } else if (high->state == NX_JOURNAL_STATE_DISK_CLEAN) {
        if (high->transaction_id != low->transaction_id ||
            !nx_journal_digest_equal(&high->root_digest, &low->root_digest) ||
            (low->state != NX_JOURNAL_STATE_DISK_CLEAN &&
             low->state != NX_JOURNAL_STATE_DISK_COMMITTED))
            return NX_STATUS_CORRUPT;
    } else {
        return NX_STATUS_CORRUPT;
    }
    *selected = *high;
    return NX_STATUS_OK;
}

static nx_status nx_journal_load_superblock(
    nx_journal *journal, nx_journal_superblock *selected) {
    nx_journal_superblock candidates[NX_JOURNAL_SUPERBLOCK_COUNT];
    nx_status statuses[NX_JOURNAL_SUPERBLOCK_COUNT];
    uint32_t index;
    for (index = 0u; index < NX_JOURNAL_SUPERBLOCK_COUNT; ++index) {
        statuses[index] = nx_block_device_read(
            journal->device, journal->first_block + (uint64_t)index,
            1u, journal->scratch);
        if (statuses[index] != NX_STATUS_OK)
            return statuses[index];
        statuses[index] = nx_journal_parse_superblock(
            journal, &candidates[index]);
    }
    if (statuses[0] == NX_STATUS_OK && statuses[1] == NX_STATUS_OK)
        return nx_journal_select_superblock_pair(
            &candidates[0], &candidates[1], selected);
    if (statuses[0] != NX_STATUS_OK && statuses[1] != NX_STATUS_OK)
        return NX_STATUS_CORRUPT;
    *selected = statuses[0] == NX_STATUS_OK ? candidates[0] : candidates[1];
    return NX_STATUS_OK;
}

static nx_status nx_journal_hash_write(nx_sha256_context *hash,
                                       uint32_t ordinal,
                                       uint64_t first_block,
                                       uint32_t block_count,
                                       const nx_digest256 *payload_digest) {
    uint8_t descriptor[NX_JOURNAL_WRITE_DESCRIPTOR_SIZE];
    memset(descriptor, 0, sizeof(descriptor));
    nx_journal_put_u32(descriptor, ordinal);
    nx_journal_put_u64(descriptor + 4u, first_block);
    nx_journal_put_u32(descriptor + 12u, block_count);
    memcpy(descriptor + 16u, payload_digest->bytes, 32u);
    return nx_sha256_update(
        hash, (nx_const_span){descriptor, sizeof(descriptor)});
}

static nx_status nx_journal_build_write_record(
    nx_journal *journal, uint64_t transaction_id, uint32_t ordinal,
    const nx_journal_write_entry *entry, const nx_digest256 *payload_digest) {
    nx_status status;
    memset(journal->scratch, 0, (size_t)journal->block_size);
    memcpy(journal->scratch, "NXWR", 4u);
    nx_journal_put_u16(journal->scratch + 4u, NX_JOURNAL_FORMAT_VERSION);
    nx_journal_put_u16(journal->scratch + 6u,
                       (uint16_t)NX_JOURNAL_WRITE_HEADER_SIZE);
    nx_journal_put_u64(journal->scratch + 8u, transaction_id);
    nx_journal_put_u32(journal->scratch + 16u, ordinal);
    nx_journal_put_u32(journal->scratch + 20u, entry->block_count);
    nx_journal_put_u64(journal->scratch + 24u, entry->first_block);
    nx_journal_put_u64(journal->scratch + 32u, entry->payload_bytes);
    memcpy(journal->scratch + 40u, payload_digest->bytes, 32u);
    status = nx_journal_block_crc_write(journal, NX_JOURNAL_WRITE_CRC_OFFSET);
    if (status != NX_STATUS_OK)
        return status;
    return NX_STATUS_OK;
}

static nx_status nx_journal_parse_write_record(
    nx_journal *journal, nx_journal_record *record) {
    uint64_t expected_bytes;
    nx_status status;
    if (memcmp(journal->scratch, "NXWR", 4u) != 0 ||
        nx_journal_get_u16(journal->scratch + 4u) !=
            NX_JOURNAL_FORMAT_VERSION ||
        nx_journal_get_u16(journal->scratch + 6u) !=
            NX_JOURNAL_WRITE_HEADER_SIZE ||
        !nx_journal_bytes_zero(journal->scratch + 80u,
                               (size_t)journal->block_size - 80u))
        return NX_STATUS_CORRUPT;
    status = nx_journal_block_crc_check(journal, NX_JOURNAL_WRITE_CRC_OFFSET);
    if (status != NX_STATUS_OK)
        return status;
    memset(record, 0, sizeof(*record));
    record->transaction_id = nx_journal_get_u64(journal->scratch + 8u);
    record->ordinal = nx_journal_get_u32(journal->scratch + 16u);
    record->block_count = nx_journal_get_u32(journal->scratch + 20u);
    record->first_block = nx_journal_get_u64(journal->scratch + 24u);
    record->payload_bytes = nx_journal_get_u64(journal->scratch + 32u);
    memcpy(record->payload_digest.bytes, journal->scratch + 40u, 32u);
    if (record->transaction_id == 0u || record->block_count == 0u ||
        record->block_count > journal->device->max_transfer_blocks ||
        nx_u64_mul((uint64_t)record->block_count,
                   (uint64_t)journal->block_size, &expected_bytes) !=
            NX_STATUS_OK ||
        record->payload_bytes != expected_bytes ||
        expected_bytes > (uint64_t)SIZE_MAX)
        return NX_STATUS_CORRUPT;
    status = nx_journal_device_range(journal, record->first_block,
                                     record->block_count);
    return status == NX_STATUS_OK ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status nx_journal_build_commit_record(
    nx_journal *journal, const nx_journal_commit_record *record) {
    nx_status status;
    memset(journal->scratch, 0, (size_t)journal->block_size);
    memcpy(journal->scratch, "NXWC", 4u);
    nx_journal_put_u16(journal->scratch + 4u, NX_JOURNAL_FORMAT_VERSION);
    nx_journal_put_u16(journal->scratch + 6u,
                       (uint16_t)NX_JOURNAL_COMMIT_HEADER_SIZE);
    nx_journal_put_u64(journal->scratch + 8u, record->transaction_id);
    nx_journal_put_u32(journal->scratch + 16u, record->write_count);
    nx_journal_put_u64(journal->scratch + 24u,
                       record->data_record_blocks);
    memcpy(journal->scratch + 32u,
           record->transaction_digest.bytes, 32u);
    memcpy(journal->scratch + 64u,
           record->base_root_digest.bytes, 32u);
    memcpy(journal->scratch + 96u, record->root_digest.bytes, 32u);
    status = nx_journal_block_crc_write(journal,
                                        NX_JOURNAL_COMMIT_CRC_OFFSET);
    return status;
}

static nx_status nx_journal_parse_commit_record(
    nx_journal *journal, nx_journal_commit_record *record) {
    nx_status status;
    if (memcmp(journal->scratch, "NXWC", 4u) != 0 ||
        nx_journal_get_u16(journal->scratch + 4u) !=
            NX_JOURNAL_FORMAT_VERSION ||
        nx_journal_get_u16(journal->scratch + 6u) !=
            NX_JOURNAL_COMMIT_HEADER_SIZE ||
        !nx_journal_bytes_zero(journal->scratch + 20u, 4u) ||
        !nx_journal_bytes_zero(journal->scratch + 136u,
                               (size_t)journal->block_size - 136u))
        return NX_STATUS_CORRUPT;
    status = nx_journal_block_crc_check(journal,
                                        NX_JOURNAL_COMMIT_CRC_OFFSET);
    if (status != NX_STATUS_OK)
        return status;
    memset(record, 0, sizeof(*record));
    record->transaction_id = nx_journal_get_u64(journal->scratch + 8u);
    record->write_count = nx_journal_get_u32(journal->scratch + 16u);
    record->data_record_blocks = nx_journal_get_u64(journal->scratch + 24u);
    memcpy(record->transaction_digest.bytes, journal->scratch + 32u, 32u);
    memcpy(record->base_root_digest.bytes, journal->scratch + 64u, 32u);
    memcpy(record->root_digest.bytes, journal->scratch + 96u, 32u);
    return NX_STATUS_OK;
}

static nx_status nx_journal_preflight_read_record(
    nx_journal *journal, const nx_journal_superblock *superblock,
    uint32_t ordinal, uint64_t cursor, nx_journal_record *record,
    uint64_t *record_blocks) {
    nx_status status = nx_block_device_read(
        journal->device,
        journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + cursor,
        1u, journal->scratch);
    if (status == NX_STATUS_OK)
        status = nx_journal_parse_write_record(journal, record);
    if (status != NX_STATUS_OK)
        return status;
    if (record->transaction_id != superblock->transaction_id ||
        record->ordinal != ordinal ||
        nx_u64_add(UINT64_C(1), (uint64_t)record->block_count,
                   record_blocks) != NX_STATUS_OK ||
        cursor >= superblock->log_blocks - 1u ||
        *record_blocks > (superblock->log_blocks - 1u) - cursor)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status nx_journal_preflight_write_entry(
    nx_journal *journal, const nx_journal_superblock *superblock,
    uint32_t ordinal, uint64_t *cursor, uint64_t *staging_offset,
    nx_sha256_context *hash) {
    nx_journal_record record;
    nx_journal_write_entry *entry;
    nx_digest256 payload_digest;
    uint64_t record_blocks;
    nx_status status = nx_journal_preflight_read_record(
        journal, superblock, ordinal, *cursor, &record, &record_blocks);
    if (status != NX_STATUS_OK)
        return status;
    if (*staging_offset > (uint64_t)journal->staging_capacity ||
        record.payload_bytes > (uint64_t)journal->staging_capacity -
                                   *staging_offset)
        return NX_STATUS_CORRUPT;
    status = nx_block_device_read(
        journal->device,
        journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + *cursor + 1u,
        record.block_count,
        journal->staging + (size_t)*staging_offset);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_sha256_compute(
        (nx_const_span){journal->staging + (size_t)*staging_offset,
                        (size_t)record.payload_bytes}, &payload_digest);
    if (status != NX_STATUS_OK)
        return status;
    if (!nx_journal_digest_equal(&payload_digest, &record.payload_digest))
        return NX_STATUS_INTEGRITY_FAILURE;
    status = nx_journal_hash_write(hash, ordinal, record.first_block,
                                   record.block_count, &payload_digest);
    if (status != NX_STATUS_OK)
        return status;
    entry = &journal->entries[ordinal];
    entry->first_block = record.first_block;
    entry->block_count = record.block_count;
    entry->staging_offset = *staging_offset;
    entry->payload_bytes = record.payload_bytes;
    entry->reserved = 0u;
    *staging_offset += record.payload_bytes;
    *cursor += record_blocks;
    return NX_STATUS_OK;
}

static nx_status nx_journal_preflight_validate_commit(
    nx_journal *journal, const nx_journal_superblock *superblock,
    uint32_t ordinal, uint64_t cursor, uint64_t staging_offset,
    nx_sha256_context *hash) {
    nx_journal_commit_record record;
    nx_digest256 digest;
    nx_status status;
    if (cursor != superblock->log_blocks - 1u ||
        ordinal != superblock->write_count)
        return NX_STATUS_CORRUPT;
    status = nx_block_device_read(
        journal->device,
        journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + cursor,
        1u, journal->scratch);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_parse_commit_record(journal, &record);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_sha256_final(hash, &digest);
    if (status != NX_STATUS_OK)
        return status;
    if (record.transaction_id != superblock->transaction_id ||
        record.write_count != superblock->write_count ||
        record.data_record_blocks != cursor ||
        !nx_journal_digest_equal(&digest, &superblock->transaction_digest) ||
        !nx_journal_digest_equal(&record.transaction_digest,
                                 &superblock->transaction_digest) ||
        !nx_journal_digest_equal(&record.base_root_digest,
                                 &superblock->base_root_digest) ||
        !nx_journal_digest_equal(&record.root_digest,
                                 &superblock->root_digest))
        return NX_STATUS_INTEGRITY_FAILURE;
    journal->write_count = ordinal;
    journal->staged_bytes = staging_offset;
    journal->transaction_digest = digest;
    journal->base_root_digest = superblock->base_root_digest;
    journal->root_digest = superblock->root_digest;
    return NX_STATUS_OK;
}

static nx_status nx_journal_preflight(nx_journal *journal,
                                      const nx_journal_superblock *sb) {
    nx_sha256_context hash;
    uint64_t cursor = 0u;
    uint64_t staging_offset = 0u;
    uint32_t ordinal;
    nx_status status;
    if (sb->write_count > journal->entry_capacity)
        return NX_STATUS_CORRUPT;
    nx_sha256_init(&hash);
    for (ordinal = 0u; ordinal < sb->write_count; ++ordinal) {
        status = nx_journal_preflight_write_entry(
            journal, sb, ordinal, &cursor, &staging_offset, &hash);
        if (status != NX_STATUS_OK)
            return status;
    }
    return nx_journal_preflight_validate_commit(
        journal, sb, ordinal, cursor, staging_offset, &hash);
}

static nx_status nx_journal_write_clean_superblock(nx_journal *journal,
                                                   uint64_t generation,
                                                   uint64_t transaction_id,
                                                   const nx_digest256 *root) {
    nx_journal_superblock clean;
    uint64_t primary_slot = generation & UINT64_C(1);
    nx_status status;
    memset(&clean, 0, sizeof(clean));
    clean.generation = generation;
    clean.state = NX_JOURNAL_STATE_DISK_CLEAN;
    clean.transaction_id = transaction_id;
    clean.root_digest = *root;
    status = nx_journal_write_superblock_slot(journal, &clean, primary_slot);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_write_superblock_slot(journal, &clean,
                                               UINT64_C(1) - primary_slot);
    if (status != NX_STATUS_OK)
        return status;
    return nx_block_device_flush(journal->device);
}

static void nx_journal_clear_transient(nx_journal *journal) {
    memset(journal->scratch, 0, (size_t)journal->block_size);
    memset(journal->staging, 0, journal->staging_capacity);
    memset(journal->entries, 0,
           (size_t)journal->entry_capacity * sizeof(*journal->entries));
    memset(&journal->base_root_digest, 0,
           sizeof(journal->base_root_digest));
    memset(&journal->transaction_digest, 0,
           sizeof(journal->transaction_digest));
    journal->write_count = 0u;
    journal->staged_bytes = 0u;
    journal->active = 0u;
}

static nx_status nx_journal_replay_failure(nx_journal *journal,
                                             nx_status failure) {
    nx_journal_clear_transient(journal);
    journal->state = NX_JOURNAL_STATE_COMMITTED;
    return failure;
}

static nx_status nx_journal_replay(nx_journal *journal,
                                   const nx_journal_superblock *sb) {
    uint32_t index;
    nx_status status;
    if (sb->generation == UINT64_MAX)
        return nx_journal_replay_failure(journal,
                                         NX_STATUS_RESOURCE_EXHAUSTED);
    status = nx_journal_preflight(journal, sb);
    if (status != NX_STATUS_OK)
        return nx_journal_replay_failure(journal, status);
    for (index = 0u; index < journal->write_count; ++index) {
        const nx_journal_write_entry *entry = &journal->entries[index];
        status = nx_block_device_write(
            journal->device, entry->first_block, entry->block_count,
            journal->staging + (size_t)entry->staging_offset);
        if (status != NX_STATUS_OK)
            return nx_journal_replay_failure(journal, status);
    }
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return nx_journal_replay_failure(journal, status);
    status = nx_journal_write_clean_superblock(
        journal, sb->generation + 1u, sb->transaction_id, &sb->root_digest);
    if (status != NX_STATUS_OK)
        return nx_journal_replay_failure(journal, status);
    journal->generation = sb->generation + 1u;
    journal->transaction_id = sb->transaction_id;
    journal->committed_log_blocks = 0u;
    journal->root_digest = sb->root_digest;
    nx_journal_clear_transient(journal);
    journal->state = NX_JOURNAL_STATE_CLEAN;
    return NX_STATUS_OK;
}

typedef struct {
    const void *data;
    size_t size;
} nx_journal_memory_range;

static int nx_journal_ranges_disjoint(
    const nx_journal_memory_range *ranges, uint32_t count) {
    uint32_t left;
    for (left = 0u; left < count; ++left) {
        uint32_t right;
        for (right = left + 1u; right < count; ++right) {
            if (nx_pointer_ranges_overlap(
                    ranges[left].data, ranges[left].size,
                    ranges[right].data, ranges[right].size) != 0)
                return 0;
        }
    }
    return 1;
}

static nx_status nx_journal_init_validate_args(
    nx_journal *journal, nx_block_device *device, uint64_t first_block,
    uint64_t block_count, void *scratch, size_t scratch_size,
    void *staging, size_t staging_capacity,
    nx_journal_write_entry *entries, uint32_t entry_capacity,
    size_t *entry_bytes) {
    uint64_t end;
    nx_status status;
    if (journal == NULL || device == NULL || scratch == NULL ||
        staging == NULL || entries == NULL || scratch_size == 0u ||
        staging_capacity == 0u || entry_capacity == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_block_device_validate(device);
    if (status != NX_STATUS_OK)
        return status;
    if (block_count < (uint64_t)NX_JOURNAL_SUPERBLOCK_COUNT + 1u ||
        nx_u64_add(first_block, block_count, &end) != NX_STATUS_OK ||
        end > device->block_count || scratch_size < (size_t)device->block_size ||
        staging_capacity % (size_t)device->block_size != 0u ||
        nx_size_mul((size_t)entry_capacity, sizeof(*entries),
                    entry_bytes) != NX_STATUS_OK)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_journal_init_validate_ranges(
    nx_journal *journal, nx_block_device *device, void *scratch,
    size_t scratch_size, void *staging, size_t staging_capacity,
    nx_journal_write_entry *entries, size_t entry_bytes) {
    nx_journal_memory_range ranges[5];
    if (((uintptr_t)scratch % (uintptr_t)device->buffer_alignment) != 0u ||
        ((uintptr_t)staging % (uintptr_t)device->buffer_alignment) != 0u ||
        ((uintptr_t)entries %
         (uintptr_t)_Alignof(nx_journal_write_entry)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    ranges[0] = (nx_journal_memory_range){journal, sizeof(*journal)};
    ranges[1] = (nx_journal_memory_range){device, sizeof(*device)};
    ranges[2] = (nx_journal_memory_range){scratch, scratch_size};
    ranges[3] = (nx_journal_memory_range){staging, staging_capacity};
    ranges[4] = (nx_journal_memory_range){entries, entry_bytes};
    return nx_journal_ranges_disjoint(ranges, 5u) != 0
               ? NX_STATUS_OK : NX_STATUS_INVALID_ARGUMENT;
}

nx_status nx_journal_init(nx_journal *journal, nx_block_device *device,
                          uint64_t first_block, uint64_t block_count,
                          void *block_scratch, size_t block_scratch_size,
                          void *staging, size_t staging_capacity,
                          nx_journal_write_entry *entries,
                          uint32_t entry_capacity) {
    size_t entry_bytes;
    nx_status status = nx_journal_init_validate_args(
        journal, device, first_block, block_count, block_scratch,
        block_scratch_size, staging, staging_capacity, entries,
        entry_capacity, &entry_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_init_validate_ranges(
        journal, device, block_scratch, block_scratch_size, staging,
        staging_capacity, entries, entry_bytes);
    if (status != NX_STATUS_OK)
        return status;
    memset(journal, 0, sizeof(*journal));
    memset(block_scratch, 0, (size_t)device->block_size);
    memset(staging, 0, staging_capacity);
    memset(entries, 0, entry_bytes);
    journal->device = device;
    journal->first_block = first_block;
    journal->block_count = block_count;
    journal->block_size = device->block_size;
    journal->abi_version = NX_JOURNAL_ABI_VERSION;
    journal->scratch = (uint8_t *)block_scratch;
    journal->scratch_size = block_scratch_size;
    journal->staging = (uint8_t *)staging;
    journal->staging_capacity = staging_capacity;
    journal->entries = entries;
    journal->entry_capacity = entry_capacity;
    journal->state = NX_JOURNAL_STATE_UNMOUNTED;
    return nx_journal_validate(journal);
}

static nx_status nx_journal_validate_geometry(
    const nx_journal *journal, size_t *entry_bytes) {
    uint64_t end;
    if (journal == NULL || journal->device == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_block_device_validate(journal->device) != NX_STATUS_OK ||
        journal->block_size == 0u ||
        journal->abi_version != NX_JOURNAL_ABI_VERSION ||
        journal->scratch == NULL || journal->staging == NULL ||
        journal->entries == NULL ||
        journal->scratch_size < (size_t)journal->block_size ||
        journal->staging_capacity == 0u ||
        journal->staging_capacity % (size_t)journal->block_size != 0u ||
        journal->entry_capacity == 0u ||
        journal->write_count > journal->entry_capacity ||
        journal->staged_bytes > (uint64_t)journal->staging_capacity ||
        journal->state > NX_JOURNAL_STATE_RECOVERY_REQUIRED ||
        journal->active > 1u ||
        ((journal->state == NX_JOURNAL_STATE_ACTIVE) !=
         (journal->active == 1u)) ||
        journal->block_size != journal->device->block_size ||
        journal->block_count < (uint64_t)NX_JOURNAL_SUPERBLOCK_COUNT + 1u ||
        nx_u64_add(journal->first_block, journal->block_count, &end) !=
            NX_STATUS_OK || end > journal->device->block_count ||
        nx_size_mul((size_t)journal->entry_capacity,
                    sizeof(*journal->entries), entry_bytes) != NX_STATUS_OK)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status nx_journal_validate_ranges(
    const nx_journal *journal, size_t entry_bytes) {
    nx_journal_memory_range ranges[5];
    if (((uintptr_t)journal->scratch %
         (uintptr_t)journal->device->buffer_alignment) != 0u ||
        ((uintptr_t)journal->staging %
         (uintptr_t)journal->device->buffer_alignment) != 0u ||
        ((uintptr_t)journal->entries %
         (uintptr_t)_Alignof(nx_journal_write_entry)) != 0u)
        return NX_STATUS_CORRUPT;
    ranges[0] = (nx_journal_memory_range){journal, sizeof(*journal)};
    ranges[1] = (nx_journal_memory_range){journal->device,
                                          sizeof(*journal->device)};
    ranges[2] = (nx_journal_memory_range){journal->scratch,
                                          journal->scratch_size};
    ranges[3] = (nx_journal_memory_range){journal->staging,
                                          journal->staging_capacity};
    ranges[4] = (nx_journal_memory_range){journal->entries, entry_bytes};
    return nx_journal_ranges_disjoint(ranges, 5u) != 0
               ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

static nx_status nx_journal_validate_state(const nx_journal *journal) {
    if (journal->state != NX_JOURNAL_STATE_UNMOUNTED &&
        journal->generation == 0u)
        return NX_STATUS_CORRUPT;
    if ((journal->state == NX_JOURNAL_STATE_CLEAN ||
         journal->state == NX_JOURNAL_STATE_ACTIVE) &&
        journal->committed_log_blocks != 0u)
        return NX_STATUS_CORRUPT;
    if (journal->state == NX_JOURNAL_STATE_COMMITTED &&
        journal->committed_log_blocks == 0u)
        return NX_STATUS_CORRUPT;
    if ((journal->state == NX_JOURNAL_STATE_UNMOUNTED ||
         journal->state == NX_JOURNAL_STATE_CLEAN) &&
        (journal->write_count != 0u || journal->staged_bytes != 0u))
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status nx_journal_validate_entries(const nx_journal *journal) {
    uint64_t expected_offset = 0u;
    uint32_t index;
    for (index = 0u; index < journal->write_count; ++index) {
        const nx_journal_write_entry *entry = &journal->entries[index];
        uint64_t expected_bytes;
        uint64_t next_offset;
        if (entry->reserved != 0u || entry->block_count == 0u ||
            entry->block_count > journal->device->max_transfer_blocks ||
            entry->staging_offset != expected_offset ||
            nx_u64_mul((uint64_t)entry->block_count,
                       (uint64_t)journal->block_size,
                       &expected_bytes) != NX_STATUS_OK ||
            entry->payload_bytes != expected_bytes ||
            nx_u64_add(expected_offset, expected_bytes,
                       &next_offset) != NX_STATUS_OK ||
            next_offset > journal->staged_bytes ||
            nx_journal_device_range(journal, entry->first_block,
                                    entry->block_count) != NX_STATUS_OK)
            return NX_STATUS_CORRUPT;
        expected_offset = next_offset;
    }
    return expected_offset == journal->staged_bytes
               ? NX_STATUS_OK : NX_STATUS_CORRUPT;
}

nx_status nx_journal_validate(const nx_journal *journal) {
    size_t entry_bytes;
    nx_status status = nx_journal_validate_geometry(journal, &entry_bytes);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_validate_ranges(journal, entry_bytes);
    if (status == NX_STATUS_OK)
        status = nx_journal_validate_state(journal);
    if (status == NX_STATUS_OK)
        status = nx_journal_validate_entries(journal);
    return status;
}

nx_status nx_journal_format(nx_journal *journal,
                            const nx_digest256 *initial_root_digest) {
    nx_journal_superblock clean;
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    if (journal->state != NX_JOURNAL_STATE_UNMOUNTED ||
        initial_root_digest == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    memset(&clean, 0, sizeof(clean));
    clean.state = NX_JOURNAL_STATE_DISK_CLEAN;
    clean.generation = 1u;
    clean.root_digest = *initial_root_digest;
    status = nx_journal_write_superblock(journal, &clean);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return status;
    clean.generation = 2u;
    status = nx_journal_write_superblock(journal, &clean);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_write_superblock_slot(journal, &clean, UINT64_C(1));
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return status;
    journal->generation = 2u;
    journal->transaction_id = 0u;
    journal->committed_log_blocks = 0u;
    journal->root_digest = *initial_root_digest;
    journal->state = NX_JOURNAL_STATE_CLEAN;
    journal->active = 0u;
    return NX_STATUS_OK;
}

nx_status nx_journal_mount(nx_journal *journal) {
    nx_journal_superblock sb;
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    if (journal->state == NX_JOURNAL_STATE_ACTIVE)
        return NX_STATUS_BUSY;
    status = nx_journal_load_superblock(journal, &sb);
    if (status != NX_STATUS_OK)
        return status;
    journal->generation = sb.generation;
    journal->transaction_id = sb.transaction_id;
    journal->committed_log_blocks = sb.log_blocks;
    journal->root_digest = sb.root_digest;
    journal->base_root_digest = sb.base_root_digest;
    journal->transaction_digest = sb.transaction_digest;
    journal->write_count = 0u;
    journal->staged_bytes = 0u;
    journal->active = 0u;
    if (sb.state == NX_JOURNAL_STATE_DISK_CLEAN) {
        nx_journal_clear_transient(journal);
        journal->state = NX_JOURNAL_STATE_CLEAN;
        return NX_STATUS_OK;
    }
    journal->state = NX_JOURNAL_STATE_COMMITTED;
    return nx_journal_replay(journal, &sb);
}

nx_status nx_journal_recover(nx_journal *journal) {
    if (journal == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (journal->state == NX_JOURNAL_STATE_ACTIVE)
        return NX_STATUS_BUSY;
    return nx_journal_mount(journal);
}

nx_status nx_journal_begin(nx_journal *journal,
                           const nx_digest256 *expected_root_digest) {
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    if (journal->state != NX_JOURNAL_STATE_CLEAN || journal->active != 0u)
        return NX_STATUS_BUSY;
    if (expected_root_digest == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (!nx_journal_digest_equal(&journal->root_digest,
                                 expected_root_digest))
        return NX_STATUS_CONFLICT;
    if (journal->generation > UINT64_MAX - 2u ||
        journal->transaction_id == UINT64_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    memset(journal->entries, 0,
           (size_t)journal->entry_capacity * sizeof(*journal->entries));
    journal->write_count = 0u;
    journal->staged_bytes = 0u;
    journal->base_root_digest = journal->root_digest;
    journal->transaction_id++;
    journal->active = 1u;
    journal->state = NX_JOURNAL_STATE_ACTIVE;
    return NX_STATUS_OK;
}

static nx_status nx_journal_write_validate_request(
    nx_journal *journal, uint64_t first_block, uint32_t block_count,
    const void *block_data, uint64_t *payload_bytes, uint64_t *staged_end,
    size_t *source_bytes) {
    nx_status status;
    if (journal->state != NX_JOURNAL_STATE_ACTIVE || journal->active != 1u)
        return NX_STATUS_BUSY;
    if (block_data == NULL || block_count == 0u ||
        block_count > journal->device->max_transfer_blocks)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_journal_device_range(journal, first_block, block_count);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_u64_mul((uint64_t)block_count,
                        (uint64_t)journal->block_size, payload_bytes);
    if (status != NX_STATUS_OK || *payload_bytes > (uint64_t)SIZE_MAX)
        return NX_STATUS_OVERFLOW;
    status = nx_u64_add(journal->staged_bytes, *payload_bytes, staged_end);
    if (status != NX_STATUS_OK)
        return status;
    if (journal->write_count == journal->entry_capacity ||
        *staged_end > (uint64_t)journal->staging_capacity)
        return NX_STATUS_NO_SPACE;
    *source_bytes = (size_t)*payload_bytes;
    if (nx_pointer_ranges_overlap(block_data, *source_bytes, journal,
                                  sizeof(*journal)) != 0 ||
        nx_pointer_ranges_overlap(block_data, *source_bytes, journal->scratch,
                                  journal->scratch_size) != 0 ||
        nx_pointer_ranges_overlap(block_data, *source_bytes, journal->staging,
                                  journal->staging_capacity) != 0 ||
        nx_pointer_ranges_overlap(block_data, *source_bytes, journal->entries,
                                  (size_t)journal->entry_capacity *
                                      sizeof(*journal->entries)) != 0 ||
        nx_pointer_ranges_overlap(block_data, *source_bytes, journal->device,
                                  sizeof(*journal->device)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

nx_status nx_journal_write(nx_journal *journal, uint64_t first_block,
                           uint32_t block_count, const void *block_data) {
    uint64_t payload_bytes;
    uint64_t staged_end;
    size_t source_bytes;
    nx_journal_write_entry *entry;
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_write_validate_request(
        journal, first_block, block_count, block_data, &payload_bytes,
        &staged_end, &source_bytes);
    if (status != NX_STATUS_OK)
        return status;
    memmove(journal->staging + (size_t)journal->staged_bytes,
            block_data, source_bytes);
    entry = &journal->entries[journal->write_count];
    entry->first_block = first_block;
    entry->block_count = block_count;
    entry->staging_offset = journal->staged_bytes;
    entry->payload_bytes = payload_bytes;
    entry->reserved = 0u;
    journal->write_count++;
    journal->staged_bytes = staged_end;
    return NX_STATUS_OK;
}

static nx_status nx_journal_precommit_failure(nx_journal *journal,
                                               nx_status failure) {
    nx_journal_clear_transient(journal);
    journal->state = NX_JOURNAL_STATE_CLEAN;
    journal->transaction_id--;
    return failure;
}

static nx_status nx_journal_commit_validate(
    nx_journal *journal, const nx_digest256 *new_root_digest,
    uint64_t *total_log_blocks) {
    uint32_t index;
    if (journal->state != NX_JOURNAL_STATE_ACTIVE ||
        journal->active != 1u || new_root_digest == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_pointer_ranges_overlap(new_root_digest, sizeof(*new_root_digest),
                                  journal, sizeof(*journal)) != 0 ||
        nx_pointer_ranges_overlap(new_root_digest, sizeof(*new_root_digest),
                                  journal->scratch, journal->scratch_size) != 0 ||
        nx_pointer_ranges_overlap(new_root_digest, sizeof(*new_root_digest),
                                  journal->staging,
                                  journal->staging_capacity) != 0 ||
        nx_pointer_ranges_overlap(new_root_digest, sizeof(*new_root_digest),
                                  journal->entries,
                                  (size_t)journal->entry_capacity *
                                      sizeof(*journal->entries)) != 0 ||
        nx_pointer_ranges_overlap(new_root_digest, sizeof(*new_root_digest),
                                  journal->device,
                                  sizeof(*journal->device)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    *total_log_blocks = 1u;
    for (index = 0u; index < journal->write_count; ++index) {
        uint64_t record_blocks;
        if (journal->entries[index].reserved != 0u ||
            nx_u64_add(UINT64_C(1),
                       (uint64_t)journal->entries[index].block_count,
                       &record_blocks) != NX_STATUS_OK ||
            nx_u64_add(*total_log_blocks, record_blocks,
                       total_log_blocks) != NX_STATUS_OK)
            return NX_STATUS_CORRUPT;
    }
    if (*total_log_blocks >
        journal->block_count - NX_JOURNAL_SUPERBLOCK_COUNT)
        return NX_STATUS_NO_SPACE;
    return NX_STATUS_OK;
}

static nx_status nx_journal_commit_write_records(
    nx_journal *journal, nx_digest256 *transaction_digest,
    uint64_t *data_record_blocks) {
    nx_sha256_context hash;
    uint64_t cursor = 0u;
    uint32_t index;
    nx_status status;
    nx_sha256_init(&hash);
    for (index = 0u; index < journal->write_count; ++index) {
        nx_journal_write_entry *entry = &journal->entries[index];
        nx_digest256 payload_digest;
        uint64_t record_blocks;
        status = nx_sha256_compute(
            (nx_const_span){journal->staging + (size_t)entry->staging_offset,
                            (size_t)entry->payload_bytes}, &payload_digest);
        if (status != NX_STATUS_OK)
            return status;
        status = nx_journal_build_write_record(
            journal, journal->transaction_id, index, entry, &payload_digest);
        if (status != NX_STATUS_OK)
            return status;
        status = nx_block_device_write(
            journal->device,
            journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + cursor,
            1u, journal->scratch);
        if (status != NX_STATUS_OK)
            return status;
        status = nx_block_device_write(
            journal->device,
            journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + cursor + 1u,
            entry->block_count,
            journal->staging + (size_t)entry->staging_offset);
        if (status != NX_STATUS_OK)
            return status;
        status = nx_journal_hash_write(&hash, index, entry->first_block,
                                       entry->block_count, &payload_digest);
        if (status != NX_STATUS_OK)
            return status;
        status = nx_u64_add(UINT64_C(1), (uint64_t)entry->block_count,
                            &record_blocks);
        if (status != NX_STATUS_OK ||
            nx_u64_add(cursor, record_blocks, &cursor) != NX_STATUS_OK)
            return NX_STATUS_CORRUPT;
    }
    *data_record_blocks = cursor;
    return nx_sha256_final(&hash, transaction_digest);
}

static nx_status nx_journal_commit_record_write(
    nx_journal *journal, const nx_digest256 *new_root_digest,
    const nx_digest256 *transaction_digest, uint64_t data_record_blocks) {
    nx_journal_commit_record record;
    nx_status status;
    memset(&record, 0, sizeof(record));
    record.transaction_id = journal->transaction_id;
    record.write_count = journal->write_count;
    record.data_record_blocks = data_record_blocks;
    record.transaction_digest = *transaction_digest;
    record.base_root_digest = journal->base_root_digest;
    record.root_digest = *new_root_digest;
    status = nx_journal_build_commit_record(journal, &record);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_write(
        journal->device,
        journal->first_block + NX_JOURNAL_SUPERBLOCK_COUNT + data_record_blocks,
        1u, journal->scratch);
    if (status != NX_STATUS_OK)
        return status;
    return nx_block_device_flush(journal->device);
}

static nx_status nx_journal_publish_commit(
    nx_journal *journal, const nx_digest256 *new_root_digest,
    const nx_digest256 *transaction_digest, uint64_t total_log_blocks) {
    nx_journal_superblock committed;
    nx_status status;
    memset(&committed, 0, sizeof(committed));
    committed.generation = journal->generation + 1u;
    committed.transaction_id = journal->transaction_id;
    committed.log_blocks = total_log_blocks;
    committed.state = NX_JOURNAL_STATE_DISK_COMMITTED;
    committed.write_count = journal->write_count;
    committed.transaction_digest = *transaction_digest;
    committed.base_root_digest = journal->base_root_digest;
    committed.root_digest = *new_root_digest;
    status = nx_journal_write_superblock(journal, &committed);
    journal->active = 0u;
    if (status != NX_STATUS_OK) {
        journal->state = NX_JOURNAL_STATE_RECOVERY_REQUIRED;
        return status;
    }
    status = nx_block_device_flush(journal->device);
    journal->generation = committed.generation;
    journal->transaction_id = committed.transaction_id;
    journal->committed_log_blocks = committed.log_blocks;
    journal->transaction_digest = committed.transaction_digest;
    journal->root_digest = committed.root_digest;
    journal->state = NX_JOURNAL_STATE_COMMITTED;
    if (status != NX_STATUS_OK) {
        journal->state = NX_JOURNAL_STATE_RECOVERY_REQUIRED;
        return status;
    }
    status = nx_journal_write_superblock_slot(
        journal, &committed, UINT64_C(1) -
                                  (committed.generation & UINT64_C(1)));
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_device_flush(journal->device);
    if (status != NX_STATUS_OK)
        return status;
    return nx_journal_replay(journal, &committed);
}

nx_status nx_journal_commit(nx_journal *journal,
                            const nx_digest256 *new_root_digest) {
    nx_digest256 transaction_digest;
    uint64_t total_log_blocks;
    uint64_t data_record_blocks = 0u;
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_commit_validate(journal, new_root_digest,
                                        &total_log_blocks);
    if (status == NX_STATUS_NO_SPACE)
        return nx_journal_precommit_failure(journal, status);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_journal_commit_write_records(
        journal, &transaction_digest, &data_record_blocks);
    if (status != NX_STATUS_OK)
        return nx_journal_precommit_failure(journal, status);
    status = nx_journal_commit_record_write(
        journal, new_root_digest, &transaction_digest, data_record_blocks);
    if (status != NX_STATUS_OK)
        return nx_journal_precommit_failure(journal, status);
    return nx_journal_publish_commit(
        journal, new_root_digest, &transaction_digest, total_log_blocks);
}

nx_status nx_journal_abort(nx_journal *journal) {
    nx_status status = nx_journal_validate(journal);
    if (status != NX_STATUS_OK)
        return status;
    if (journal->state != NX_JOURNAL_STATE_ACTIVE ||
        journal->active != 1u)
        return NX_STATUS_INVALID_ARGUMENT;
    nx_journal_clear_transient(journal);
    journal->state = NX_JOURNAL_STATE_CLEAN;
    journal->transaction_id--;
    return NX_STATUS_OK;
}

nx_status nx_journal_root_read(const nx_journal *journal,
                               nx_digest256 *root_digest) {
    if (nx_journal_validate(journal) != NX_STATUS_OK ||
        root_digest == NULL ||
        journal->state != NX_JOURNAL_STATE_CLEAN)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_pointer_ranges_overlap(root_digest, sizeof(*root_digest),
                                  journal, sizeof(*journal)) != 0 ||
        nx_pointer_ranges_overlap(root_digest, sizeof(*root_digest),
                                  journal->scratch, journal->scratch_size) != 0 ||
        nx_pointer_ranges_overlap(root_digest, sizeof(*root_digest),
                                  journal->staging,
                                  journal->staging_capacity) != 0 ||
        nx_pointer_ranges_overlap(root_digest, sizeof(*root_digest),
                                  journal->entries,
                                  (size_t)journal->entry_capacity *
                                      sizeof(*journal->entries)) != 0 ||
        nx_pointer_ranges_overlap(root_digest, sizeof(*root_digest),
                                  journal->device,
                                  sizeof(*journal->device)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    *root_digest = journal->root_digest;
    return NX_STATUS_OK;
}
