/*
 * kernel/journal.c — Circular Write-Ahead Log (WAL)
 *
 * Layout on disk: n_blocks × BLK_SIZE entries starting at journal_lba.
 * Each journal block: [magic:4][seq:8][dev:4][lba:8][data:BLK_SIZE][commit:1]
 *
 * On boot, journal_replay() walks all entries and re-applies uncommitted ones.
 * journal_checkpoint() is called every 64 commits to reclaim space.
 */

#include "include/nexs_journal.h"
#include "include/nexs_blk.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   ON-DISK RECORD FORMAT
   ========================================================= */

#define JOURNAL_MAGIC  0x4E584A4CUL  /* "NXJL" */
#define JOURNAL_BLOCK_HDR 25         /* magic(4)+seq(8)+dev(4)+lba(8)+commit(1) */

typedef struct {
    uint32_t magic;
    uint64_t seq;
    uint32_t dev;
    uint64_t lba;
    uint8_t  commit;
    uint8_t  data[BLK_SIZE];
} __attribute__((packed)) JournalRecord;

/* =========================================================
   STATE
   ========================================================= */

static uint32_t s_dev       = 0;
static uint64_t s_base_lba  = 0;
static uint32_t s_n_blocks  = 0;
static uint64_t s_write_pos = 0;
static uint64_t s_seq       = 0;
static int      s_in_txn    = 0;
static int      s_commit_count = 0;

/* =========================================================
   HELPERS
   ========================================================= */

static uint64_t journal_lba(uint64_t pos) {
    return s_base_lba + (pos % s_n_blocks);
}

static void write_record(uint64_t pos, uint32_t dev, uint64_t lba,
                         const uint8_t *data, uint8_t commit) {
    JournalRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic  = JOURNAL_MAGIC;
    rec.seq    = s_seq;
    rec.dev    = dev;
    rec.lba    = lba;
    rec.commit = commit;
    if (data) memcpy(rec.data, data, BLK_SIZE);
    blk_write(s_dev, journal_lba(pos), &rec);
}

/* =========================================================
   PUBLIC API
   ========================================================= */

int journal_init(uint32_t dev, uint64_t lba, uint32_t n) {
    s_dev       = dev;
    s_base_lba  = lba;
    s_n_blocks  = n;
    s_write_pos = 0;
    s_seq       = 0;
    s_in_txn    = 0;
    s_commit_count = 0;

    reg_set("/sys/journal/status",   val_str("ok"),  RK_READ);
    reg_set("/sys/journal/capacity", val_int((int64_t)n), RK_READ);
    return 0;
}

int journal_begin(void) {
    if (s_in_txn) return -1; /* nested txn not supported */
    s_seq++;
    s_in_txn = 1;
    reg_set("/sys/journal/in_txn", val_int(1), RK_READ | RK_WRITE);
    return 0;
}

int journal_log(uint32_t dev, uint64_t lba) {
    if (!s_in_txn || !s_n_blocks) return -1;
    /* Read the current block content before it gets overwritten */
    uint8_t old_data[BLK_SIZE];
    if (blk_read(dev, lba, old_data) != 0)
        memset(old_data, 0, BLK_SIZE);
    write_record(s_write_pos++, dev, lba, old_data, 0);
    return 0;
}

int journal_commit(void) {
    if (!s_in_txn) return -1;
    write_record(s_write_pos++, s_dev, 0, NULL, 1);
    s_in_txn = 0;
    s_commit_count++;
    reg_set("/sys/journal/in_txn",     val_int(0), RK_READ | RK_WRITE);
    reg_set("/sys/journal/seq",        val_int((int64_t)s_seq), RK_READ);
    reg_set("/sys/journal/commit_cnt", val_int(s_commit_count), RK_READ);
    if (s_commit_count % 64 == 0) journal_checkpoint();
    return 0;
}

int journal_replay(void) {
    if (!s_n_blocks) return 0;
    JournalRecord rec;
    uint64_t pos = 0;
    while (pos < s_n_blocks) {
        if (blk_read(s_dev, journal_lba(pos), &rec) != 0) break;
        if (rec.magic != JOURNAL_MAGIC) break;
        if (!rec.commit) {
            /* Uncommitted — re-apply the saved old_data to restore pre-crash state */
            blk_write(rec.dev, rec.lba, rec.data);
        }
        pos++;
    }
    reg_set("/sys/journal/replayed", val_int((int64_t)pos), RK_READ);
    return 0;
}

void journal_checkpoint(void) {
    s_write_pos  = 0; /* Wrap around — old entries no longer needed */
    s_commit_count = 0;
    blk_sync();
    reg_set("/sys/journal/status", val_str("checkpointed"), RK_READ);
}
