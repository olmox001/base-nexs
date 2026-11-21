/* kernel/vfs.c — One VFS projection of the ONE NXOLMO Registry.
 * A path /x/y is the actual /x/y RegKey/StateStore object, never a shadow
 * /vfs/files/x/y or a synthetic inode table. Mounts are namespace metadata;
 * the ongoing P4 cutover must also merge native and hosted descriptor owners.
 */

#include "include/nexs_vfs.h"
#include "include/nexs_proc.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_alloc.h"
#include "../hal/include/nexs_hal.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* One NXOLMO name validator for native VFS and the actual RegKey tree.
 * The wrapper only copies that admitted exact name into caller storage. */
static int vfs_named_path(const char *user_path, char *named) {
    if (named == NULL || !reg_path_valid(user_path))
        return -1;
    const size_t length = strlen(user_path);
    memcpy(named, user_path, length + 1u);
    return 0;
}

static RegKey *vfs_named_object(const char *user_path, char named[REG_PATH_MAX]) {
    if (vfs_named_path(user_path, named) != 0)
        return NULL;
    return reg_lookup(named);
}

static int vfs_is_directory(const RegKey *key) {
    if (key == NULL)
        return 0;
    /* Registry namespaces are directories too: typed directories are
     * optional metadata, not alternative object identities. */
    return key->val.type == TYPE_NIL || key->children != NULL ||
           (key->val.type == TYPE_INT &&
            (key->val.ival & 0040000) != 0);
}

static int vfs_parent_exists(const char *named) {
    char parent[REG_PATH_MAX];
    const char *slash = strrchr(named, '/');
    size_t size;
    if (slash == NULL || slash < named)
        return 0;
    if (slash == named)
        return vfs_is_directory(reg_lookup("/"));
    size = (size_t)(slash - named);
    if (size >= sizeof(parent))
        return 0;
    memcpy(parent, named, size);
    parent[size] = 0;
    return vfs_is_directory(reg_lookup(parent));
}

/* =========================================================
   FD TABLE  (per-process, stored in registry)
   ========================================================= */

typedef struct {
    int      in_use;
    nx_object_ref object;
    char     named[REG_PATH_MAX];
    int      flags;
    uint64_t pos;
} VfsFd;

/* A dynamically sized caller-owned descriptor arena, not a second file
 * object index. Only NXOLMO StateStore refs grant file identity. The
 * existing global host descriptor owner still needs NXEC per-domain cutover. */
static VfsFd *s_fd_table = NULL;
static uint32_t s_fd_capacity = 0u;

static size_t vfs_fd_pages(uint32_t capacity) {
    if (capacity == 0u ||
        (size_t)capacity > (SIZE_MAX - NEXS_PAGE_SIZE + 1u) / sizeof(VfsFd))
        return 0u;
    size_t size = (size_t)capacity * sizeof(VfsFd);
    return (size + NEXS_PAGE_SIZE - 1u) / NEXS_PAGE_SIZE;
}

static int vfs_fd_reserve(uint32_t required) {
    while (required > s_fd_capacity) {
        uint32_t capacity;
        if (s_fd_capacity > (uint32_t)INT_MAX / 2u)
            return -1;
        capacity = s_fd_capacity == 0u ? 8u : s_fd_capacity * 2u;
        size_t pages = vfs_fd_pages(capacity);
        if (pages == 0u)
            return -1;
        VfsFd *storage = page_alloc(pages);
        if (storage == NULL)
            return -1;
        memset(storage, 0, pages * NEXS_PAGE_SIZE);
        if (s_fd_table != NULL) {
            size_t old_pages = vfs_fd_pages(s_fd_capacity);
            memcpy(storage, s_fd_table, (size_t)s_fd_capacity * sizeof(VfsFd));
            if (page_free_checked(s_fd_table, old_pages) != 0) {
                (void)page_free_checked(storage, pages);
                return -1;
            }
        }
        s_fd_table = storage;
        s_fd_capacity = capacity;
    }
    return 0;
}

void vfs_init(void) {
    if (s_fd_table != NULL) {
        (void)page_free_checked(s_fd_table, vfs_fd_pages(s_fd_capacity));
        s_fd_table = NULL;
        s_fd_capacity = 0u;
    }
    if (vfs_fd_reserve(3u) != 0)
        return;
    /* reg_init already admitted the single root in NXOLMO StateStore.
     * Never overwrite that root or publish a shadow /vfs/files tree. */
    if (!vfs_is_directory(reg_lookup("/")))
        return;
    for (int i = 0; i < 3; ++i) {
        s_fd_table[i].in_use = 1;
        s_fd_table[i].flags = i == 0 ? 0 : 1;
    }
}

static int fd_alloc(RegKey *file, const char *named, int flags) {
    uint32_t chosen = 3u;
    if (file == NULL || named == NULL || s_fd_table == NULL)
        return -1;
    while (chosen < s_fd_capacity && s_fd_table[chosen].in_use != 0)
        ++chosen;
    if (chosen >= s_fd_capacity && vfs_fd_reserve(chosen + 1u) != 0)
        return -1;
    size_t length = strlen(named);
    if (length >= sizeof(s_fd_table[chosen].named))
        return -1;
    s_fd_table[chosen].object = file->object;
    memcpy(s_fd_table[chosen].named, named, length + 1u);
    s_fd_table[chosen].flags = flags;
    s_fd_table[chosen].pos = 0u;
    s_fd_table[chosen].in_use = 1;
    return (int)chosen;
}

static VfsFd *fd_get(int fd) {
    if (fd < 0 || (uint32_t)fd >= s_fd_capacity || s_fd_table == NULL ||
        s_fd_table[fd].in_use == 0)
        return NULL;
    return &s_fd_table[fd];
}

static int vfs_fd_is_live(const VfsFd *fd) {
    RegKey *found;
    nx_result result;
    nx_state_node node;
    if (fd == NULL || !nx_object_ref_is_valid(fd->object))
        return 0;
    found = reg_lookup(fd->named);
    return found != NULL && nx_object_ref_equal(found->object, fd->object) &&
           nx_state_store_read(&g_registry.state, fd->object,
                               &node, &result) == NX_STATUS_OK;
}

int vfs_dup(int oldfd, int newfd) {
    VfsFd *source = fd_get(oldfd);
    VfsFd copy;
    if (!vfs_fd_is_live(source) || newfd < -1)
        return -1;
    if (newfd == oldfd)
        return oldfd;
    copy = *source;
    if (newfd == -1) {
        uint32_t chosen = 3u;
        while (chosen < s_fd_capacity && s_fd_table[chosen].in_use != 0)
            ++chosen;
        if (vfs_fd_reserve(chosen + 1u) != 0)
            return -1;
        newfd = (int)chosen;
    } else {
        if (vfs_fd_reserve((uint32_t)newfd + 1u) != 0)
            return -1;
    }
    s_fd_table[newfd] = copy;
    return newfd;
}

int vfs_seek(int fd, int64_t offset, int whence) {
    VfsFd *f = fd_get(fd);
    uint64_t base = 0u;
    uint64_t position;
    if (!vfs_fd_is_live(f))
        return -1;
    if (whence == 1) {
        base = f->pos;
    } else if (whence == 2) {
        /* SEEK_END is relative to the actual canonical file length.
         * The legacy string file backend does not support sparse holes. */
        RegKey *key = reg_lookup(f->named);
        if (key == NULL || key->val.type != TYPE_STR ||
            key->val.data == NULL)
            return -1;
        base = strlen((const char *)key->val.data);
    } else if (whence != 0) {
        return -1;
    }
    if (base > (uint64_t)INT_MAX)
        return -1;
    if (offset >= 0) {
        if ((uint64_t)offset > (uint64_t)INT_MAX - base)
            return -1;
        position = base + (uint64_t)offset;
    } else {
        /* Account for INT64_MIN without a signed negation overflow. */
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1u;
        if (magnitude > base)
            return -1;
        position = base - magnitude;
    }
    f->pos = position;
    return (int)position;
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
   PUBLIC VFS: path -> canonical NXOLMO identity, not inode scan.
   ========================================================= */

int vfs_open(const char *path, int flags) {
    char named[REG_PATH_MAX];
    RegKey *file = vfs_named_object(path, named);
    if (vfs_named_path(path, named) != 0)
        return -1;
    if (file == NULL) {
        if ((flags & 0100) == 0 || !vfs_parent_exists(named))
            return -1;
        Value initial = val_str("");
        if (initial.type != TYPE_STR)
            return -1;
        int status = reg_set(named, initial, RK_READ | RK_WRITE);
        val_free(&initial);
        if (status != 0)
            return -1;
        file = reg_lookup(named);
    }
    if (file == NULL ||
        (file->val.type != TYPE_STR &&
         !(vfs_is_directory(file) && (flags & 3) == 0)))
        return -1;
    return fd_alloc(file, named, flags);
}

int vfs_read(int fd, void *buf, size_t n) {
    if (buf == NULL || n > INT_MAX)
        return -1;
    if (fd == 0) {
        size_t got = 0u;
        uint8_t *out = (uint8_t *)buf;
        while (got < n) {
            int c = nexs_hal_getc();
            if (c < 0)
                return (int)got;
            out[got++] = (uint8_t)c;
            if (c == '\n')
                return (int)got;
        }
        return (int)got;
    }
    VfsFd *f = fd_get(fd);
    if (f == NULL || (f->flags & 3) == 1 || !vfs_fd_is_live(f))
        return -1;
    Value v = reg_get(f->named);
    if (v.type != TYPE_STR || v.data == NULL) {
        val_free(&v);
        return -1;
    }
    const char *data = (const char *)v.data;
    size_t total = strlen(data);
    if (f->pos >= total) {
        val_free(&v);
        return 0;
    }
    size_t avail = total - (size_t)f->pos;
    if (n > avail)
        n = avail;
    memcpy(buf, data + (size_t)f->pos, n);
    f->pos += n;
    val_free(&v);
    return (int)n;
}

int vfs_write(int fd, const void *buf, size_t n) {
    if (buf == NULL || n > INT_MAX)
        return -1;
    if (fd == 1 || fd == 2) {
        const char *s = (const char *)buf;
        for (size_t i = 0; i < n; ++i)
            nexs_hal_putc(s[i]);
        return (int)n;
    }
    VfsFd *f = fd_get(fd);
    if (f == NULL || (f->flags & 3) == 0 || !vfs_fd_is_live(f))
        return -1;
    /* The file itself is ONE canonical StateStore node: every write
     * changes its own revision, including the Qid version seen over 9P.
     * The legacy Value string is transitional; true binary zero-copy
     * requires NXOLMO page leases, not a duplicate data child. */
    Value v = reg_get(f->named);
    if (v.type != TYPE_STR || v.data == NULL) {
        val_free(&v);
        return -1;
    }
    const char *old = v.type == TYPE_STR && v.data != NULL
                          ? (const char *)v.data : "";
    size_t old_len = strlen(old);
    if (f->pos > SIZE_MAX || (size_t)f->pos > old_len ||
        n > SIZE_MAX - (size_t)f->pos - 1u ||
        memchr(buf, 0, n) != NULL) {
        val_free(&v);
        return -1;
    }
    size_t end = (size_t)f->pos + n;
    size_t new_len = end > old_len ? end : old_len;
    char *combined = nexs_alloc(new_len + 1u);
    if (combined == NULL) {
        val_free(&v);
        return -1;
    }
    memcpy(combined, old, old_len);
    memcpy(combined + (size_t)f->pos, buf, n);
    combined[new_len] = 0;
    Value replacement = val_str(combined);
    int status = replacement.type == TYPE_STR
                     ? reg_set(f->named, replacement, RK_READ | RK_WRITE) : -1;
    val_free(&replacement);
    nexs_free(combined, new_len + 1u);
    val_free(&v);
    if (status != 0)
        return -1;
    f->pos += n;
    return (int)n;
}

int vfs_close(int fd) {
    if (fd >= 0 && fd < 3) return 0; /* standard handles remain reserved */
    VfsFd *f = fd_get(fd);
    if (!f) return -1;
    f->in_use = 0;
    return 0;
}

int vfs_stat(const char *path, VfsInode *out) {
    char named[REG_PATH_MAX];
    RegKey *file = vfs_named_object(path, named);
    nx_result result;
    nx_state_node node;
    if (out == NULL || file == NULL ||
        (file->val.type != TYPE_STR && !vfs_is_directory(file)) ||
        nx_state_store_read(&g_registry.state, file->object,
                            &node, &result) != NX_STATUS_OK)
        return -1;
    memset(out, 0, sizeof(*out));
    out->object = node.ref;
    out->revision = node.revision;
    out->ino = node.ref.id.low;
    out->mode = vfs_is_directory(file)
                    ? (file->val.type == TYPE_INT
                        ? (uint32_t)file->val.ival : 0040755u)
                    : 0100644u;
    if (file->val.type == TYPE_STR && file->val.data != NULL)
        out->size = strlen((const char *)file->val.data);
    memcpy(out->reg_data_path, named, strlen(named) + 1u);
    return 0;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    char named[REG_PATH_MAX];
    RegKey *dir = vfs_named_object(path, named);
    if (dir != NULL)
        return vfs_is_directory(dir) ? 0 : -1;
    if (vfs_named_path(path, named) != 0 || !vfs_parent_exists(named))
        return -1;
    return reg_set(named, val_int((int64_t)(mode | 0040000u)),
                   RK_READ | RK_WRITE);
}

int vfs_remove(const char *path) {
    char named[REG_PATH_MAX];
    RegKey *entry = vfs_named_object(path, named);
    if (entry == NULL || strcmp(path, "/") == 0)
        return -1;
    return reg_delete(named);
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
