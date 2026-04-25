/* ===========================================================================
 * ufs_partition.c — Partition Format & Mount
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#include "include/ufs_partition.h"
#include "include/ufs_tree.h"
#include "include/ufs_block.h"
#include "include/ufs_perm.h"
#include <string.h>

static u8 fmt_buf[UFS_BLOCK_SIZE] __attribute__((aligned(512)));

/* ---- Format ------------------------------------------------------------- */

USB_Status ufs_format_partition(u32 disk_id, u64 start_lba,
                                 u64 size_sectors, u32 fs_type) {
    UFS_Tree tree;
    memset(&tree, 0, sizeof(tree));
    tree.disk_id = disk_id;
    tree.part_start_lba = start_lba;
    tree.part_size_lba = size_sectors;

    u64 total_blocks = (size_sectors * 512) / UFS_BLOCK_SIZE;
    u64 bitmap_blocks = (total_blocks + UFS_BLOCK_SIZE * 8 - 1) /
                        (UFS_BLOCK_SIZE * 8);

    /* Build superblock */
    UFS_Superblock *sb = &tree.sb;
    sb->magic = UFS_MAGIC;
    sb->version = UFS_VERSION;
    sb->block_size = UFS_BLOCK_SIZE;
    sb->total_blocks = total_blocks;
    sb->free_blocks = total_blocks - 1 - bitmap_blocks - 1;
    sb->bitmap_start = 1;
    sb->bitmap_blocks = bitmap_blocks;
    sb->inode_start = 1 + bitmap_blocks;
    sb->root_qid.type = QTDIR;
    sb->root_qid.version = 0;
    sb->root_qid.path = sb->inode_start;
    sb->fs_type = fs_type;

    /* Write superblock (block 0) */
    memset(fmt_buf, 0, UFS_BLOCK_SIZE);
    memcpy(fmt_buf, sb, sizeof(UFS_Superblock));
    USB_Status r = ufs_block_write(&tree, 0, fmt_buf);
    if (r != USB_OK) return r;

    /* Initialize bitmap */
    r = ufs_block_init_bitmap(&tree);
    if (r != USB_OK) return r;

    /* Create root directory inode */
    UFS_Inode root;
    memset(&root, 0, sizeof(root));
    root.qid = sb->root_qid;
    root.mode = DMDIR | 0755;
    root.nlink = 1;
    const char *own = "sys";
    for (u32 i = 0; own[i]; i++) {
        root.owner[i] = own[i];
        root.group[i] = own[i];
    }
    r = ufs_write_inode(&tree, sb->inode_start, &root);
    if (r != USB_OK) return r;

    /* Create standard directory structure */
    if (fs_type == 1) {
        /* ROOT partition: /srv, /dev, /env, /proc */
        const char *dirs[] = {"srv", "dev", "env", "proc"};
        for (u32 i = 0; i < 4; i++) {
            UFS_Qid q;
            ufs_create(&tree, sb->inode_start, dirs[i], DMDIR | 0755, &q);
        }
    } else if (fs_type == 2) {
        /* USER partition: /user */
        UFS_Qid q;
        ufs_create(&tree, sb->inode_start, "user", DMDIR | 0755, &q);
    }

    return USB_OK;
}

/* ---- Mount -------------------------------------------------------------- */

USB_Status ufs_partition_mount(u32 disk_id, u64 start_lba,
                                u64 size_sectors, UFS_Tree *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->disk_id = disk_id;
    tree->part_start_lba = start_lba;
    tree->part_size_lba = size_sectors;

    /* Read superblock */
    USB_Status r = ufs_block_read(tree, 0, fmt_buf);
    if (r != USB_OK) return r;

    memcpy(&tree->sb, fmt_buf, sizeof(UFS_Superblock));

    if (tree->sb.magic != UFS_MAGIC)
        return USB_ERR_FS;
    if (tree->sb.version != UFS_VERSION)
        return USB_ERR_FS;

    return USB_OK;
}

/* ---- Create User Namespace --------------------------------------------- */

USB_Status ufs_partition_create_user(UFS_Tree *tree, const char *uid) {
    /* Walk to /user */
    UFS_Qid user_qid;
    i64 user_block = ufs_walk(tree, "/user", &user_qid);
    if (user_block < 0) return USB_ERR_FS;

    /* Create /user/<uid> */
    UFS_Qid uid_qid;
    USB_Status r = ufs_create(tree, (u64)user_block, uid,
                               DMDIR | 0750, &uid_qid);
    if (r != USB_OK) return r;

    /* Create standard subdirectories */
    const char *subdirs[] = {
        "dev", "proc", "srv", "env", "bin", "lib", "mnt", "home"
    };
    for (u32 i = 0; i < 8; i++) {
        UFS_Qid q;
        ufs_create(tree, uid_qid.path, subdirs[i], DMDIR | 0755, &q);
    }

    return USB_OK;
}
