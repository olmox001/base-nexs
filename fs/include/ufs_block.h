/* ===========================================================================
 * ufs_block.h — Block Allocator (Bitmap)
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#ifndef UFS_BLOCK_H
#define UFS_BLOCK_H

#include "ufs_types.h"

/* Allocate a free block. Returns block number, or 0 on failure. */
u64  ufs_block_alloc(UFS_Tree *t);

/* Free a block. */
USB_Status ufs_block_free(UFS_Tree *t, u64 block);

/* Read a block from disk (partition-relative). */
USB_Status ufs_block_read(UFS_Tree *t, u64 block, void *buf);

/* Write a block to disk (partition-relative). */
USB_Status ufs_block_write(UFS_Tree *t, u64 block, const void *buf);

/* Initialize bitmap for a freshly formatted partition. */
USB_Status ufs_block_init_bitmap(UFS_Tree *t);

/* Check if a block is allocated. */
bool ufs_block_is_used(UFS_Tree *t, u64 block);

#endif /* UFS_BLOCK_H */
