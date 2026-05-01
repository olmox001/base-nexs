/*
 * kernel/vfs.c — VFS layer (inode / mount table)
 *
 * Mount hierarchy:
 *   /sys          → runtime root registry (RAM)
 *   /mnt/sd00     → drivers
 *   /mnt/sd01     → system files
 *   /mnt/sd02-98  → home / user directories
 *
 * Inodes are stored in the registry under /vfs/inode/<ino>/.
 * Mount table lives at /vfs/mount/<id>/.
 * File descriptors live in /proc/<pid>/fd/<n>/.
 */

#include "include/nexs_vfs.h"
#include "include/nexs_proc.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_alloc.h"
#include "../hal/include/nexs_hal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   INODE ALLOCATOR
   ========================================================= */

static uint64_t s_next_ino = 1;

static uint64_t ino_alloc(void) { return s_next_ino++; }

static void ino_publish(uint64_t ino, const char *path, uint32_t mode) {
    char reg[REG_PATH_MAX];
    snprintf(reg, sizeof(reg), "/vfs/inode/%llu/path",
             (unsigned long long)ino);
    reg_set(reg, val_str(path), RK_READ);
    snprintf(reg, sizeof(reg), "/vfs/inode/%llu/mode",
             (unsigned long long)ino);
    reg_set(reg, val_int((int64_t)mode), RK_READ);
}

/* =========================================================
   FD TABLE  (per-process, stored in registry)
   ========================================================= */

#define VFS_MAX_FD 64

typedef struct {
    int      in_use;
    uint64_t ino;
    int      flags;
    uint64_t pos;
} VfsFd;

static VfsFd s_fd_table[VFS_MAX_FD];

void vfs_init(void) {
    /* Reserve fd 0/1/2 for stdin/stdout/stderr (inode 0/1/2 = sentinel) */
    for (int i = 0; i < 3; i++) {
        s_fd_table[i].in_use = 1;
        s_fd_table[i].ino    = (uint64_t)i;
        s_fd_table[i].flags  = (i == 0) ? 0 : 1;
        s_fd_table[i].pos    = 0;
    }
}

static int fd_alloc(uint64_t ino, int flags) {
    for (int i = 3; i < VFS_MAX_FD; i++) {
        if (!s_fd_table[i].in_use) {
            s_fd_table[i].in_use = 1;
            s_fd_table[i].ino    = ino;
            s_fd_table[i].flags  = flags;
            s_fd_table[i].pos    = 0;
            return i;
        }
    }
    return -1;
}

static VfsFd *fd_get(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD || !s_fd_table[fd].in_use) return NULL;
    return &s_fd_table[fd];
}

int vfs_dup(int oldfd, int newfd) {
    VfsFd *src = fd_get(oldfd);
    if (!src) return -1;
    if (newfd == -1) {
        for (int i = 3; i < VFS_MAX_FD; i++) {
            if (!s_fd_table[i].in_use) { newfd = i; break; }
        }
    }
    if (newfd < 0 || newfd >= VFS_MAX_FD) return -1;
    if (s_fd_table[newfd].in_use) vfs_close(newfd);
    s_fd_table[newfd] = *src;
    return newfd;
}

int vfs_seek(int fd, int64_t offset, int whence) {
    VfsFd *f = fd_get(fd);
    if (!f) return -1;
    int64_t new_pos;
    switch (whence) {
        case 0: new_pos = offset; break;                        /* SEEK_SET */
        case 1: new_pos = (int64_t)f->pos + offset; break;     /* SEEK_CUR */
        case 2: new_pos = offset; break;                        /* SEEK_END — size unknown, treat as SET */
        default: return -1;
    }
    if (new_pos < 0) return -1;
    f->pos = (uint64_t)new_pos;
    return (int)new_pos;
}

/* =========================================================
   MOUNT TABLE
   ========================================================= */

#define VFS_MAX_MOUNTS 16

typedef struct {
    int  in_use;
    char src[REG_PATH_MAX];
    char dst[REG_PATH_MAX];
    char fstype[32];
} VfsMount;

static VfsMount s_mounts[VFS_MAX_MOUNTS];
static int      s_mount_count = 0;

/* =========================================================
   PATH → INODE LOOKUP
   ========================================================= */

static uint64_t path_to_ino(const char *path) {
    char reg_path[REG_PATH_MAX];
    /* Walk /vfs/inode/ looking for matching path */
    for (uint64_t ino = 1; ino < s_next_ino; ino++) {
        snprintf(reg_path, sizeof(reg_path), "/vfs/inode/%llu/path",
                 (unsigned long long)ino);
        Value v = reg_get(reg_path);
        int match = (v.type == TYPE_STR && v.data &&
                     strcmp((char *)v.data, path) == 0);
        val_free(&v);
        if (match) return ino;
    }
    return 0;
}

/* =========================================================
   PUBLIC VFS API
   ========================================================= */

int vfs_open(const char *path, int flags) {
    uint64_t ino = path_to_ino(path);
    if (!ino) {
        if (!(flags & 0100)) return -1; /* O_CREAT not set */
        ino = ino_alloc();
        ino_publish(ino, path, 0100644);
    }
    return fd_alloc(ino, flags);
}

int vfs_read(int fd, void *buf, size_t n) {
    if (fd == 0) {
        /* stdin → HAL character input */
        size_t got = 0;
        uint8_t *out = (uint8_t *)buf;
        while (got < n) {
            int c = nexs_hal_getc();
            if (c < 0) break;
            out[got++] = (uint8_t)c;
            if (c == '\n') break;
        }
        return (int)got;
    }
    VfsFd *f = fd_get(fd);
    if (!f) return -1;
    char reg_path[REG_PATH_MAX];
    snprintf(reg_path, sizeof(reg_path), "/vfs/inode/%llu/data",
             (unsigned long long)f->ino);
    Value v = reg_get(reg_path);
    if (v.type != TYPE_STR || !v.data) { val_free(&v); return 0; }
    const char *data = (const char *)v.data;
    size_t total = strlen(data);
    if (f->pos >= total) { val_free(&v); return 0; }
    size_t avail = total - (size_t)f->pos;
    if (n > avail) n = avail;
    memcpy(buf, data + f->pos, n);
    f->pos += n;
    val_free(&v);
    return (int)n;
}

int vfs_write(int fd, const void *buf, size_t n) {
    if (fd == 1 || fd == 2) {
        /* stdout / stderr → HAL print */
        const char *s = (const char *)buf;
        for (size_t i = 0; i < n; i++) nexs_hal_putc(s[i]);
        return (int)n;
    }
    VfsFd *f = fd_get(fd);
    if (!f) return -1;
    char reg_path[REG_PATH_MAX];
    snprintf(reg_path, sizeof(reg_path), "/vfs/inode/%llu/data",
             (unsigned long long)f->ino);
    /* Append write: read existing, append, write back */
    Value v = reg_get(reg_path);
    char *old = (v.type == TYPE_STR && v.data) ? (char *)v.data : "";
    size_t old_len = strlen(old);
    char *combined = (char *)nexs_alloc(old_len + n + 1);
    if (!combined) { val_free(&v); return -1; }
    memcpy(combined, old, old_len);
    memcpy(combined + old_len, buf, n);
    combined[old_len + n] = '\0';
    reg_set(reg_path, val_str(combined), RK_READ | RK_WRITE);
    nexs_free(combined, old_len + n + 1);
    val_free(&v);
    f->pos += n;
    return (int)n;
}

int vfs_close(int fd) {
    if (fd >= 0 && fd < 3) return 0; /* never close stdin/stdout/stderr */
    VfsFd *f = fd_get(fd);
    if (!f) return -1;
    f->in_use = 0;
    return 0;
}

int vfs_stat(const char *path, VfsInode *out) {
    uint64_t ino = path_to_ino(path);
    if (!ino) return -1;
    char reg_path[REG_PATH_MAX];
    out->ino = ino;
    snprintf(reg_path, sizeof(reg_path), "/vfs/inode/%llu/mode",
             (unsigned long long)ino);
    Value v = reg_get(reg_path);
    out->mode = (v.type == TYPE_INT) ? (uint32_t)v.ival : 0100644;
    val_free(&v);
    out->uid = out->gid = 0;
    out->size = 0;
    out->atime = out->mtime = out->ctime = 0;
    char data_path[REG_PATH_MAX];
    snprintf(data_path, sizeof(data_path), "/vfs/inode/%llu/data",
             (unsigned long long)ino);
    strncpy(out->reg_data_path, data_path, sizeof(out->reg_data_path) - 1);
    return 0;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (path_to_ino(path)) return 0; /* already exists */
    uint64_t ino = ino_alloc();
    ino_publish(ino, path, mode | 0040000); /* S_IFDIR */
    return 0;
}

int vfs_mount(const char *src, const char *dst, const char *fstype) {
    if (s_mount_count >= VFS_MAX_MOUNTS) return -1;
    VfsMount *m = &s_mounts[s_mount_count++];
    m->in_use = 1;
    strncpy(m->src,    src,    sizeof(m->src) - 1);
    strncpy(m->dst,    dst,    sizeof(m->dst) - 1);
    strncpy(m->fstype, fstype, sizeof(m->fstype) - 1);

    char reg_base[REG_PATH_MAX];
    snprintf(reg_base, sizeof(reg_base), "/vfs/mount/%d", s_mount_count - 1);
    reg_set(reg_base,           val_str(src),    RK_READ);
    char tmp[REG_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s/dst",    reg_base); reg_set(tmp, val_str(dst),    RK_READ);
    snprintf(tmp, sizeof(tmp), "%s/fstype", reg_base); reg_set(tmp, val_str(fstype), RK_READ);
    return 0;
}
