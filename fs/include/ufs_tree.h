/* ===========================================================================
 * ufs_tree.h — File Tree Operations
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#ifndef UFS_TREE_H
#define UFS_TREE_H

#include "ufs_types.h"

/* Walk a path from the root. Returns block of target inode. */
i64  ufs_walk(UFS_Tree *t, const char *path, UFS_Qid *out);

/* Create a file or directory. Returns qid of created entry. */
USB_Status ufs_create(UFS_Tree *t, u64 parent_block,
                       const char *name, u32 mode, UFS_Qid *out);

/* Read file data. Returns bytes read. */
i64  ufs_read(UFS_Tree *t, u64 inode_block, u64 offset,
              void *buf, u32 count);

/* Write file data. Returns bytes written. */
i64  ufs_write(UFS_Tree *t, u64 inode_block, u64 offset,
                const void *buf, u32 count);

/* Get file stat (inode metadata). */
USB_Status ufs_stat(UFS_Tree *t, u64 inode_block, UFS_Inode *out);

/* Remove a file or empty directory. */
USB_Status ufs_remove(UFS_Tree *t, u64 parent_block, const char *name);

/* List directory entries. Returns count found. */
i32  ufs_readdir(UFS_Tree *t, u64 dir_block,
                 UFS_DirEntry *entries, u32 max);

/* Read an inode from disk. */
USB_Status ufs_read_inode(UFS_Tree *t, u64 block, UFS_Inode *out);

/* Write an inode to disk. */
USB_Status ufs_write_inode(UFS_Tree *t, u64 block, const UFS_Inode *ino);

#endif /* UFS_TREE_H */
