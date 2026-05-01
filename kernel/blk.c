/*
 * kernel/blk.c — 256-entry LRU buffer cache for block devices
 *
 * Each block is 4096 bytes (BLK_SIZE). Cache tracks dev+lba pairs.
 * Dirty blocks are flushed by blk_sync(), called every 5 s by the timer.
 * HAL read/write stubs are no-ops until real drivers register via blk_register_dev().
 */

#include "include/nexs_blk.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   CACHE
   ========================================================= */

static BlkBuf  s_cache[BLK_CACHE];
static uint64_t s_access_clock = 0;

/* =========================================================
   HAL DEVICE INTERFACE
   ========================================================= */

typedef int (*BlkReadFn)(uint32_t dev, uint64_t lba, void *buf);
typedef int (*BlkWriteFn)(uint32_t dev, uint64_t lba, const void *buf);

typedef struct {
    uint32_t   dev_id;
    BlkReadFn  read;
    BlkWriteFn write;
} BlkDev;

#define BLK_MAX_DEVS 8
static BlkDev  s_devs[BLK_MAX_DEVS];
static int     s_dev_count = 0;

void blk_register_dev(uint32_t dev_id, BlkReadFn read_fn, BlkWriteFn write_fn) {
    if (s_dev_count >= BLK_MAX_DEVS) return;
    s_devs[s_dev_count].dev_id = dev_id;
    s_devs[s_dev_count].read   = read_fn;
    s_devs[s_dev_count].write  = write_fn;
    s_dev_count++;
}

static BlkDev *blk_find_dev(uint32_t dev_id) {
    for (int i = 0; i < s_dev_count; i++)
        if (s_devs[i].dev_id == dev_id) return &s_devs[i];
    return NULL;
}

/* =========================================================
   LRU CACHE HELPERS
   ========================================================= */

static void flush_buf(BlkBuf *b) {
    if (!b->valid || !b->dirty) return;
    BlkDev *d = blk_find_dev(b->dev_id);
    if (d && d->write) {
        d->write(b->dev_id, b->lba, b->data);
        b->dirty = 0;
    }
}

static BlkBuf *cache_find(uint32_t dev, uint64_t lba) {
    for (int i = 0; i < BLK_CACHE; i++) {
        if (s_cache[i].valid && s_cache[i].dev_id == dev &&
            s_cache[i].lba == lba)
            return &s_cache[i];
    }
    return NULL;
}

static BlkBuf *cache_evict(void) {
    /* Find least-recently-used valid slot; prefer clean over dirty */
    BlkBuf *best = NULL;
    for (int i = 0; i < BLK_CACHE; i++) {
        if (!s_cache[i].valid) return &s_cache[i]; /* free slot */
        if (!best || (!s_cache[i].dirty && best->dirty) ||
            (s_cache[i].dirty == best->dirty &&
             s_cache[i].last_access < best->last_access))
            best = &s_cache[i];
    }
    return best;
}

/* =========================================================
   PUBLIC API
   ========================================================= */

void blk_init(void) {
    memset(s_cache, 0, sizeof(s_cache));
    s_access_clock = 0;
    s_dev_count    = 0;
    reg_set("/sys/blk/cache_size", val_int(BLK_CACHE), RK_READ);
    reg_set("/sys/blk/status",     val_str("ok"),       RK_READ);
}

int blk_read(uint32_t dev, uint64_t lba, void *buf) {
    BlkBuf *b = cache_find(dev, lba);
    if (b) {
        memcpy(buf, b->data, BLK_SIZE);
        b->last_access = ++s_access_clock;
        return 0;
    }
    /* Cache miss — load from device */
    BlkDev *d = blk_find_dev(dev);
    if (!d || !d->read) return -1;
    if (d->read(dev, lba, buf) != 0) return -1;

    /* Insert into cache */
    b = cache_evict();
    flush_buf(b);
    b->dev_id      = dev;
    b->lba         = lba;
    b->dirty       = 0;
    b->valid       = 1;
    b->last_access = ++s_access_clock;
    memcpy(b->data, buf, BLK_SIZE);
    return 0;
}

int blk_write(uint32_t dev, uint64_t lba, const void *buf) {
    BlkBuf *b = cache_find(dev, lba);
    if (!b) {
        b = cache_evict();
        flush_buf(b);
        b->dev_id = dev;
        b->lba    = lba;
        b->dirty  = 0;
        b->valid  = 1;
    }
    memcpy(b->data, buf, BLK_SIZE);
    b->dirty       = 1;
    b->last_access = ++s_access_clock;

    char path[128];
    snprintf(path, sizeof(path), "/dev/blk/%u/%llu/dirty",
             dev, (unsigned long long)lba);
    reg_set(path, val_int(1), RK_READ | RK_WRITE);
    return 0;
}

void blk_sync(void) {
    for (int i = 0; i < BLK_CACHE; i++) {
        if (s_cache[i].valid && s_cache[i].dirty) {
            BlkDev *d = blk_find_dev(s_cache[i].dev_id);
            if (d && d->write) {
                d->write(s_cache[i].dev_id, s_cache[i].lba, s_cache[i].data);
                s_cache[i].dirty = 0;
                char path[128];
                snprintf(path, sizeof(path), "/dev/blk/%u/%llu/dirty",
                         s_cache[i].dev_id,
                         (unsigned long long)s_cache[i].lba);
                reg_set(path, val_int(0), RK_READ | RK_WRITE);
            }
        }
    }
}
