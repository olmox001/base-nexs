/* ===========================================================================
 * ufs_partition.h — Three-Tier Partition Manager
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#ifndef UFS_PARTITION_H
#define UFS_PARTITION_H

#include "ufs_types.h"

/* ---- API --------------------------------------------------------------- */

/*
 * Format a partition with UFS.
 *
 * disk_id       — target drive
 * start_lba     — first sector of this partition
 * size_sectors  — number of sectors in this partition
 * fs_type       — 1=ROOT, 2=USER
 */
USB_Status ufs_format_partition(u32 disk_id, u64 start_lba,
                                 u64 size_sectors, u32 fs_type);

/*
 * Mount a partition into a UFS_Tree handle.
 * Reads superblock, validates, returns ready-to-use tree.
 */
USB_Status ufs_partition_mount(u32 disk_id, u64 start_lba,
                                u64 size_sectors, UFS_Tree *tree);

/*
 * Create a new user namespace inside the USER partition.
 * Creates /user/<uid>/ with standard subdirectories.
 */
USB_Status ufs_partition_create_user(UFS_Tree *tree, const char *uid);

#endif /* UFS_PARTITION_H */
