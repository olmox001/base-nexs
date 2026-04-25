/*
 * kernel/include/nexs_blk.h — Block device / buffer cache
 */
#ifndef NEXS_BLK_H
#define NEXS_BLK_H
#pragma once
#include <stdint.h>

#define BLK_SIZE    4096
#define BLK_CACHE   256   /* LRU cache entries */

typedef struct {
    uint64_t lba;
    uint8_t  data[BLK_SIZE];
    int      dirty;
    int      valid;
    uint32_t dev_id;
    uint64_t last_access;
} BlkBuf;

int  blk_read(uint32_t dev, uint64_t lba, void *buf);
int  blk_write(uint32_t dev, uint64_t lba, const void *buf);
void blk_sync(void);
void blk_init(void);

#endif
