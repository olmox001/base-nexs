/* ===========================================================================
 * ufs_block.c — Bitmap Block Allocator
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#include "include/ufs_block.h"
#include "include/ufs_types.h"
#include <string.h>

#ifndef NEXS_BAREMETAL
#include <stdio.h>
#endif

/* ---- Helpers ----------------------------------------------------------- */

#ifndef NEXS_BAREMETAL
static u64 block_to_lba(UFS_Tree *t, u64 block) {
    return t->part_start_lba + block * (UFS_BLOCK_SIZE / 512);
}
#endif

/* ---- Block I/O --------------------------------------------------------- */

USB_Status ufs_block_read(UFS_Tree *t, u64 block, void *buf) {
#ifndef NEXS_BAREMETAL
    u64 lba = block_to_lba(t, block);
    FILE *f = fopen("disk.img", "r+b");
    if (!f) {
        f = fopen("disk.img", "w+b");
        if (f) {
            /* pre-allocate 64MB */
            fseek(f, 64 * 1024 * 1024 - 1, SEEK_SET);
            fputc(0, f);
            fseek(f, 0, SEEK_SET);
        }
    }
    if (!f) return USB_ERR_DISK;
    fseek(f, lba * 512, SEEK_SET);
    size_t r = fread(buf, 1, UFS_BLOCK_SIZE, f);
    fclose(f);
    if (r != UFS_BLOCK_SIZE) {
        memset(buf, 0, UFS_BLOCK_SIZE);
    }
    return USB_OK;
#else
    (void)t; (void)block; (void)buf;
    return USB_ERR_DISK;
#endif
}

USB_Status ufs_block_write(UFS_Tree *t, u64 block, const void *buf) {
#ifndef NEXS_BAREMETAL
    u64 lba = block_to_lba(t, block);
    FILE *f = fopen("disk.img", "r+b");
    if (!f) f = fopen("disk.img", "w+b");
    if (!f) return USB_ERR_DISK;
    fseek(f, lba * 512, SEEK_SET);
    size_t r = fwrite(buf, 1, UFS_BLOCK_SIZE, f);
    fflush(f);
    fclose(f);
    if (r != UFS_BLOCK_SIZE) return USB_ERR_DISK;
    return USB_OK;
#else
    (void)t; (void)block; (void)buf;
    return USB_ERR_DISK;
#endif
}

/* ---- Bitmap Operations ------------------------------------------------- */

static u8 bitmap_buf[UFS_BLOCK_SIZE] __attribute__((aligned(512)));

u64 ufs_block_alloc(UFS_Tree *t) {
    u64 bm_start = t->sb.bitmap_start;
    u64 bm_blocks = t->sb.bitmap_blocks;
    u64 total = t->sb.total_blocks;

    for (u64 bm = 0; bm < bm_blocks; bm++) {
        if (ufs_block_read(t, bm_start + bm, bitmap_buf) != USB_OK)
            return 0;

        for (u32 byte = 0; byte < UFS_BLOCK_SIZE; byte++) {
            if (bitmap_buf[byte] == 0xFF) continue;

            for (u32 bit = 0; bit < 8; bit++) {
                if (bitmap_buf[byte] & (1 << bit)) continue;

                u64 block_num = bm * UFS_BLOCK_SIZE * 8 + byte * 8 + bit;
                if (block_num >= total) return 0;

                /* Mark as used */
                bitmap_buf[byte] |= (1 << bit);
                ufs_block_write(t, bm_start + bm, bitmap_buf);

                /* Update superblock free count */
                t->sb.free_blocks--;
                static u8 sb_buf[UFS_BLOCK_SIZE] __attribute__((aligned(512)));
                memset(sb_buf, 0, UFS_BLOCK_SIZE);
                memcpy(sb_buf, &t->sb, sizeof(UFS_Superblock));
                ufs_block_write(t, 0, sb_buf);

                return block_num;
            }
        }
    }
    return 0; /* No free blocks */
}

USB_Status ufs_block_free(UFS_Tree *t, u64 block) {
    u64 bm_block_idx = block / (UFS_BLOCK_SIZE * 8);
    u32 byte_in_block = (block % (UFS_BLOCK_SIZE * 8)) / 8;
    u32 bit_in_byte = block % 8;

    if (ufs_block_read(t, t->sb.bitmap_start + bm_block_idx,
                       bitmap_buf) != USB_OK)
        return USB_ERR_DISK;

    bitmap_buf[byte_in_block] &= ~(1 << bit_in_byte);

    USB_Status r = ufs_block_write(t, t->sb.bitmap_start + bm_block_idx,
                                    bitmap_buf);
    if (r != USB_OK) return r;

    t->sb.free_blocks++;
    return USB_OK;
}

bool ufs_block_is_used(UFS_Tree *t, u64 block) {
    u64 bm_block_idx = block / (UFS_BLOCK_SIZE * 8);
    u32 byte_in_block = (block % (UFS_BLOCK_SIZE * 8)) / 8;
    u32 bit_in_byte = block % 8;

    if (ufs_block_read(t, t->sb.bitmap_start + bm_block_idx,
                       bitmap_buf) != USB_OK)
        return true; /* Assume used on error */

    return (bitmap_buf[byte_in_block] & (1 << bit_in_byte)) != 0;
}

USB_Status ufs_block_init_bitmap(UFS_Tree *t) {
    /* Zero all bitmap blocks */
    memset(bitmap_buf, 0, UFS_BLOCK_SIZE);
    for (u64 bm = 0; bm < t->sb.bitmap_blocks; bm++) {
        USB_Status r = ufs_block_write(t, t->sb.bitmap_start + bm, bitmap_buf);
        if (r != USB_OK) return r;
    }

    /* Mark reserved blocks as used: superblock + bitmap + root inode */
    u64 reserved = 1 + t->sb.bitmap_blocks + 1;
    if (ufs_block_read(t, t->sb.bitmap_start, bitmap_buf) != USB_OK)
        return USB_ERR_DISK;

    for (u64 i = 0; i < reserved; i++) {
        u32 byte = (u32)(i / 8);
        u32 bit = (u32)(i % 8);
        bitmap_buf[byte] |= (1 << bit);
    }

    return ufs_block_write(t, t->sb.bitmap_start, bitmap_buf);
}
