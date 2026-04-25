/* ===========================================================================
 * ufs_tree.c — File Tree Operations
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#include "include/ufs_tree.h"
#include "include/ufs_block.h"
#include "include/ufs_perm.h"
#include <string.h>

static u8 io_buf[UFS_BLOCK_SIZE] __attribute__((aligned(512)));

/* ---- Inode I/O --------------------------------------------------------- */

USB_Status ufs_read_inode(UFS_Tree *t, u64 block, UFS_Inode *out) {
    USB_Status r = ufs_block_read(t, block, io_buf);
    if (r != USB_OK) return r;
    memcpy(out, io_buf, sizeof(UFS_Inode));
    return USB_OK;
}

USB_Status ufs_write_inode(UFS_Tree *t, u64 block, const UFS_Inode *ino) {
    memset(io_buf, 0, UFS_BLOCK_SIZE);
    memcpy(io_buf, ino, sizeof(UFS_Inode));
    return ufs_block_write(t, block, io_buf);
}

/* ---- Directory Helpers ------------------------------------------------- */

static bool name_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static usize slen(const char *s) { usize n = 0; while(s[n]) n++; return n; }

/*
 * Search a directory inode's data blocks for an entry named 'name'.
 * Returns the block number of the matching inode, or 0 if not found.
 */
static u64 dir_lookup(UFS_Tree *t, UFS_Inode *dir, const char *name) {
    for (u32 d = 0; d < UFS_DIRECT_BLOCKS; d++) {
        if (dir->direct[d] == 0) continue;
        if (ufs_block_read(t, dir->direct[d], io_buf) != USB_OK) continue;

        u32 off = 0;
        while (off + sizeof(UFS_DirEntry) <= UFS_BLOCK_SIZE) {
            UFS_DirEntry *e = (UFS_DirEntry *)(io_buf + off);
            if (e->name_len == 0) break;
            if (name_eq(e->name, name))
                return e->qid.path; /* path stores the inode block */
            off += sizeof(UFS_DirEntry);
        }
    }
    return 0;
}

/*
 * Add an entry to a directory. Finds first empty slot or allocates new block.
 */
static USB_Status dir_add_entry(UFS_Tree *t, u64 dir_block,
                                 const char *name, UFS_Qid *qid) {
    UFS_Inode dir;
    USB_Status r = ufs_read_inode(t, dir_block, &dir);
    if (r != USB_OK) return r;

    for (u32 d = 0; d < UFS_DIRECT_BLOCKS; d++) {
        if (dir.direct[d] == 0) {
            /* Allocate a new data block for dir entries */
            dir.direct[d] = ufs_block_alloc(t);
            if (dir.direct[d] == 0) return USB_ERR_NOMEM;
            memset(io_buf, 0, UFS_BLOCK_SIZE);
        } else {
            if (ufs_block_read(t, dir.direct[d], io_buf) != USB_OK)
                continue;
        }

        u32 off = 0;
        while (off + sizeof(UFS_DirEntry) <= UFS_BLOCK_SIZE) {
            UFS_DirEntry *e = (UFS_DirEntry *)(io_buf + off);
            if (e->name_len == 0) {
                /* Empty slot found */
                e->qid = *qid;
                e->name_len = (u16)slen(name);
                memcpy(e->name, name, e->name_len);
                e->name[e->name_len] = '\0';

                ufs_block_write(t, dir.direct[d], io_buf);
                ufs_write_inode(t, dir_block, &dir);
                return USB_OK;
            }
            off += sizeof(UFS_DirEntry);
        }
    }
    return USB_ERR_NOMEM; /* No space */
}

/* ---- Walk --------------------------------------------------------------- */

i64 ufs_walk(UFS_Tree *t, const char *path, UFS_Qid *out) {
    u64 cur_block = t->sb.inode_start; /* Root inode block */
    UFS_Inode cur;

    if (ufs_read_inode(t, cur_block, &cur) != USB_OK)
        return -1;

    if (out) *out = cur.qid;

    /* Skip leading '/' */
    while (*path == '/') path++;
    if (*path == '\0') return (i64)cur_block;

    while (*path) {
        /* Extract next component */
        char comp[UFS_NAME_MAX + 1];
        u32 i = 0;
        while (*path && *path != '/' && i < UFS_NAME_MAX)
            comp[i++] = *path++;
        comp[i] = '\0';
        while (*path == '/') path++;

        if (!(cur.mode & DMDIR)) return -1; /* Not a directory */

        u64 found = dir_lookup(t, &cur, comp);
        if (found == 0) return -1; /* Not found */

        cur_block = found;
        if (ufs_read_inode(t, cur_block, &cur) != USB_OK)
            return -1;
    }

    if (out) *out = cur.qid;
    return (i64)cur_block;
}

/* ---- Create ------------------------------------------------------------- */

USB_Status ufs_create(UFS_Tree *t, u64 parent_block,
                       const char *name, u32 mode, UFS_Qid *out) {
    /* Allocate inode block */
    u64 new_block = ufs_block_alloc(t);
    if (new_block == 0) return USB_ERR_NOMEM;

    /* Build inode */
    UFS_Inode ino;
    memset(&ino, 0, sizeof(ino));
    ino.qid.type = (mode & DMDIR) ? QTDIR : QTFILE;
    ino.qid.version = 0;
    ino.qid.path = new_block;
    ino.mode = mode;
    ino.nlink = 1;

    /* Default owner/group */
    const char *def = "sys";
    for (u32 i = 0; def[i]; i++) {
        ino.owner[i] = def[i];
        ino.group[i] = def[i];
    }

    USB_Status r = ufs_write_inode(t, new_block, &ino);
    if (r != USB_OK) return r;

    /* Add directory entry in parent */
    r = dir_add_entry(t, parent_block, name, &ino.qid);
    if (r != USB_OK) return r;

    if (out) *out = ino.qid;
    return USB_OK;
}

/* ---- Stat --------------------------------------------------------------- */

USB_Status ufs_stat(UFS_Tree *t, u64 inode_block, UFS_Inode *out) {
    return ufs_read_inode(t, inode_block, out);
}

/* ---- Read --------------------------------------------------------------- */

i64 ufs_read(UFS_Tree *t, u64 inode_block, u64 offset,
              void *buf, u32 count) {
    UFS_Inode ino;
    if (ufs_read_inode(t, inode_block, &ino) != USB_OK)
        return -1;

    if (offset >= ino.size) return 0;
    if (offset + count > ino.size) count = (u32)(ino.size - offset);

    u8 *dst = (u8 *)buf;
    u32 done = 0;

    while (done < count) {
        u64 block_idx = (offset + done) / UFS_BLOCK_SIZE;
        u32 block_off = (offset + done) % UFS_BLOCK_SIZE;
        u32 chunk = UFS_BLOCK_SIZE - block_off;
        if (chunk > count - done) chunk = count - done;

        if (block_idx >= UFS_DIRECT_BLOCKS) break; /* TODO: indirect */
        u64 blk = ino.direct[block_idx];
        if (blk == 0) { memset(dst + done, 0, chunk); }
        else {
            if (ufs_block_read(t, blk, io_buf) != USB_OK) return -1;
            memcpy(dst + done, io_buf + block_off, chunk);
        }
        done += chunk;
    }
    return (i64)done;
}

/* ---- Write -------------------------------------------------------------- */

i64 ufs_write(UFS_Tree *t, u64 inode_block, u64 offset,
               const void *buf, u32 count) {
    UFS_Inode ino;
    if (ufs_read_inode(t, inode_block, &ino) != USB_OK) return -1;

    const u8 *src = (const u8 *)buf;
    u32 done = 0;

    while (done < count) {
        u64 block_idx = (offset + done) / UFS_BLOCK_SIZE;
        u32 block_off = (offset + done) % UFS_BLOCK_SIZE;
        u32 chunk = UFS_BLOCK_SIZE - block_off;
        if (chunk > count - done) chunk = count - done;

        if (block_idx >= UFS_DIRECT_BLOCKS) break; /* TODO: indirect */

        if (ino.direct[block_idx] == 0) {
            ino.direct[block_idx] = ufs_block_alloc(t);
            if (ino.direct[block_idx] == 0) break;
        }

        /* Read-modify-write if partial block */
        if (block_off != 0 || chunk != UFS_BLOCK_SIZE) {
            if (ufs_block_read(t, ino.direct[block_idx], io_buf) != USB_OK)
                memset(io_buf, 0, UFS_BLOCK_SIZE);
        }

        memcpy(io_buf + block_off, src + done, chunk);
        if (ufs_block_write(t, ino.direct[block_idx], io_buf) != USB_OK)
            return -1;

        done += chunk;
    }

    /* Update size */
    if (offset + done > ino.size)
        ino.size = offset + done;
    ino.qid.version++;
    ufs_write_inode(t, inode_block, &ino);

    return (i64)done;
}

/* ---- Remove ------------------------------------------------------------- */

USB_Status ufs_remove(UFS_Tree *t, u64 parent_block, const char *name) {
    UFS_Inode dir;
    if (ufs_read_inode(t, parent_block, &dir) != USB_OK) return USB_ERR_DISK;

    for (u32 d = 0; d < UFS_DIRECT_BLOCKS; d++) {
        if (dir.direct[d] == 0) continue;
        if (ufs_block_read(t, dir.direct[d], io_buf) != USB_OK) continue;

        u32 off = 0;
        while (off + sizeof(UFS_DirEntry) <= UFS_BLOCK_SIZE) {
            UFS_DirEntry *e = (UFS_DirEntry *)(io_buf + off);
            if (e->name_len > 0 && name_eq(e->name, name)) {
                u64 victim = e->qid.path;
                /* Free inode's data blocks */
                UFS_Inode vino;
                if (ufs_read_inode(t, victim, &vino) == USB_OK) {
                    for (u32 i = 0; i < UFS_DIRECT_BLOCKS; i++)
                        if (vino.direct[i]) ufs_block_free(t, vino.direct[i]);
                }
                ufs_block_free(t, victim);
                /* Clear dir entry */
                memset(e, 0, sizeof(UFS_DirEntry));
                ufs_block_write(t, dir.direct[d], io_buf);
                return USB_OK;
            }
            off += sizeof(UFS_DirEntry);
        }
    }
    return USB_ERR_SIG; /* Not found */
}

/* ---- Readdir ------------------------------------------------------------ */

i32 ufs_readdir(UFS_Tree *t, u64 dir_block,
                 UFS_DirEntry *entries, u32 max) {
    UFS_Inode dir;
    if (ufs_read_inode(t, dir_block, &dir) != USB_OK) return -1;

    u32 found = 0;
    for (u32 d = 0; d < UFS_DIRECT_BLOCKS && found < max; d++) {
        if (dir.direct[d] == 0) continue;
        if (ufs_block_read(t, dir.direct[d], io_buf) != USB_OK) continue;

        u32 off = 0;
        while (off + sizeof(UFS_DirEntry) <= UFS_BLOCK_SIZE && found < max) {
            UFS_DirEntry *e = (UFS_DirEntry *)(io_buf + off);
            if (e->name_len == 0) break;
            entries[found++] = *e;
            off += sizeof(UFS_DirEntry);
        }
    }
    return (i32)found;
}
