/*
 * fs/9p.c — 9P2000 server
 *
 * Handles: Tversion, Tattach, Twalk, Topen, Tread, Twrite, Tclunk, Tstat.
 * Server state is published to /sys/9p/ in the registry.
 * Requests arrive as IPC messages on /sys/9p/inbox.
 */

#include "include/nexs_fs9p.h"
#include "nexs_p9_frame.h"
#include "nexs_endian.h"
#include <limits.h>
#include "../kernel/include/nexs_vfs.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_alloc.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   FID TABLE
   ========================================================= */

#define P9_MSIZE    8192

typedef struct {
    int     in_use;
    uint32_t fid;
    char     path[REG_PATH_MAX];
    int      fd;
    int      open;
    uint8_t  mode;
    nx_object_ref object; /* Single canonical NXOLMO target. */
} P9Fid;

/* FID is a locally leased channel key; it is never an object ID. This
 * hosted adapter still needs NXEC-owned domain sessions (P4). */
static P9Fid *s_fids = NULL;
static uint32_t s_fid_capacity = 0u;
static uint32_t s_msize = P9_MSIZE;

static size_t p9_fid_pages(uint32_t capacity) {
    if (capacity == 0u ||
        (size_t)capacity >
            (SIZE_MAX - NEXS_PAGE_SIZE + 1u) / sizeof(P9Fid))
        return 0u;
    return ((size_t)capacity * sizeof(P9Fid) +
            NEXS_PAGE_SIZE - 1u) / NEXS_PAGE_SIZE;
}

static int p9_fid_reserve(uint32_t required) {
    while (required > s_fid_capacity) {
        uint32_t capacity;
        if (s_fid_capacity > UINT32_MAX / 2u)
            return -1;
        capacity = s_fid_capacity == 0u ? 8u : s_fid_capacity * 2u;
        size_t pages = p9_fid_pages(capacity);
        if (pages == 0u)
            return -1;
        P9Fid *storage = page_alloc(pages);
        if (storage == NULL)
            return -1;
        memset(storage, 0, pages * NEXS_PAGE_SIZE);
        if (s_fids != NULL) {
            size_t old_pages = p9_fid_pages(s_fid_capacity);
            memcpy(storage, s_fids,
                   (size_t)s_fid_capacity * sizeof(P9Fid));
            if (page_free_checked(s_fids, old_pages) != 0) {
                (void)page_free_checked(storage, pages);
                return -1;
            }
        }
        s_fids = storage;
        s_fid_capacity = capacity;
    }
    return 0;
}

static P9Fid *fid_find(uint32_t fid) {
    for (uint32_t i = 0u; i < s_fid_capacity; ++i) {
        if (s_fids[i].in_use != 0 && s_fids[i].fid == fid)
            return &s_fids[i];
    }
    return NULL;
}

static P9Fid *fid_alloc(uint32_t fid) {
    uint32_t index = 0u;
    if (fid_find(fid) != NULL)
        return NULL;
    while (index < s_fid_capacity && s_fids[index].in_use != 0)
        ++index;
    if (index >= s_fid_capacity && p9_fid_reserve(index + 1u) != 0)
        return NULL;
    memset(&s_fids[index], 0, sizeof(s_fids[index]));
    s_fids[index].in_use = 1;
    s_fids[index].fid = fid;
    s_fids[index].fd = -1;
    return &s_fids[index];
}

static int p9_fid_live(const P9Fid *fid) {
    nx_state_node node;
    nx_result result;
    return fid != NULL && nx_object_ref_is_valid(fid->object) &&
           nx_state_store_read(&g_registry.state, fid->object,
                               &node, &result) == NX_STATUS_OK;
}

/* Path resolution happens only for walk/open, not on every I/O operation.
 * Per-FD generation/lifetime checks still run in the actual VFS I/O. */
static int p9_fid_path_matches(const P9Fid *fid) {
    VfsInode found;
    return p9_fid_live(fid) && vfs_stat(fid->path, &found) == 0 &&
           nx_object_ref_equal(fid->object, found.object);
}

/* =========================================================
   MESSAGE HELPERS
   ========================================================= */

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8);
}

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

/* A 9P Qid is a scoped wire projection, not an authority or second
 * filesystem identity. The live NXOLMO node owns its generation/revision. */
static int p9_qid_for_path(const char *path, uint8_t qid[13]) {
    VfsInode inode;
    if (vfs_stat(path, &inode) != 0 ||
        !nx_object_ref_is_valid(inode.object))
        return -1;
    qid[0] = (inode.mode & 0040000u) != 0u ? 0x80u : 0u;
    nx_store_le32(qid + 1u, (uint32_t)inode.revision);
    nx_store_le64(qid + 5u, inode.object.id.low);
    return 0;
}

/* =========================================================
   INIT
   ========================================================= */

void p9_server_init(void) {
    if (s_fids != NULL) {
        for (uint32_t i = 0u; i < s_fid_capacity; ++i) {
            if (s_fids[i].in_use && s_fids[i].open && s_fids[i].fd >= 0)
                (void)vfs_close(s_fids[i].fd);
        }
        (void)page_free_checked(s_fids, p9_fid_pages(s_fid_capacity));
        s_fids = NULL;
        s_fid_capacity = 0u;
    }
    s_msize = P9_MSIZE;
    int ready = p9_fid_reserve(8u) == 0 &&
                reg_ipc_init_queue("/sys/9p/inbox", 64) == 0;
    (void)reg_set("/sys/9p/msize", val_int(s_msize), RK_READ);
    (void)reg_set("/sys/9p/status",
                  val_str(ready ? "ok" : "admission denied"), RK_READ);
}

/* =========================================================
   HANDLE ONE MESSAGE
   Minimal frame: [size:4][type:1][tag:2][body...]
   ========================================================= */

int p9_server_handle(const uint8_t *req, size_t req_len,
                     uint8_t *resp, size_t resp_max) {
    nx_9p_frame_view frame;
    nx_const_span wire = {req, req_len};
    size_t consumed = 0u;
    if (req == NULL || resp == NULL ||
        nx_pointer_ranges_overlap(req, req_len, resp, resp_max) != 0)
        return -1;
    if (nx_9p_frame_decode(wire, s_msize, &frame, &consumed) != NX_STATUS_OK ||
        consumed != req_len || nx_9p_request_validate(&frame) != NX_STATUS_OK)
        return -1;

    uint8_t msg_type = frame.type;
    uint16_t tag = frame.tag;
    const uint8_t *body = frame.body;

    uint8_t rtype = msg_type + 1; /* Rxxx = Txxx + 1 */
    size_t  rlen  = 0;

    switch (msg_type) {

    case P9_TVERSION: {
        /* Negotiate msize and protocol */
        uint32_t client_msize = read_u32(body);
        if (client_msize < 24u)
            return -1;
        if (client_msize < s_msize) s_msize = client_msize;
        /* Rversion: size[4] type[1] tag[2] msize[4] version[2+string] */
        const char *ver = "9P2000";
        uint16_t vlen   = (uint16_t)strlen(ver);
        rlen = 13 + vlen;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        write_u32(resp + 7, s_msize);
        write_u16(resp + 11, vlen);
        memcpy(resp + 13, ver, vlen);
        return (int)rlen;
    }

    case P9_TATTACH: {
        uint32_t fid = read_u32(body);
        /* Skip afid(4), uname(2+str), aname(2+str) */
        const uint8_t *p = body + 8;
        uint16_t uname_len = read_u16(p); p += 2u + uname_len;
        uint16_t aname_len = read_u16(p); p += 2u + aname_len;
        (void)p; (void)aname_len;

        P9Fid *f = fid_alloc(fid);
        if (!f) { /* Rerror */
            const char *emsg = "out of fids";
            uint16_t elen = (uint16_t)strlen(emsg);
            rlen = 9 + elen;
            if (rlen > resp_max) return -1;
            write_u32(resp, (uint32_t)rlen);
            resp[4] = 107; /* Rerror */
            write_u16(resp + 5, tag);
            write_u16(resp + 7, elen);
            memcpy(resp + 9, emsg, elen);
            return (int)rlen;
        }
        strncpy(f->path, "/", sizeof(f->path) - 1);

        /* Rattach: size[4] type[1] tag[2] qid[13] */
        rlen = 20;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        VfsInode root;
        if (p9_qid_for_path("/", resp + 7) != 0 ||
            vfs_stat("/", &root) != 0) {
            memset(f, 0, sizeof(*f));
            return -1;
        }
        f->object = root.object;
        return (int)rlen;
    }

    case P9_TWALK: {
        uint32_t fid = read_u32(body);
        uint32_t newfid = read_u32(body + 4u);
        uint16_t nwname = read_u16(body + 8u);
        const uint8_t *p = body + 10u;
        P9Fid *src = fid_find(fid);
        char path[REG_PATH_MAX];
        size_t response_bytes;
        uint16_t i;

        if (!p9_fid_path_matches(src) || src->open != 0 ||
            (newfid != fid && fid_find(newfid) != NULL))
            return -1;
        response_bytes = 9u + (size_t)nwname * 13u;
        if (response_bytes > resp_max || response_bytes > s_msize)
            return -1;
        {
            size_t path_size = strlen(src->path);
            if (path_size == 0u || path_size >= sizeof(path))
                return -1;
            memcpy(path, src->path, path_size + 1u);
        }
        for (i = 0u; i < nwname; ++i) {
            uint16_t wlen = read_u16(p);
            size_t current;
            p += 2u;
            if (wlen == 0u || memchr(p, 0, wlen) != NULL ||
                memchr(p, '/', wlen) != NULL)
                return -1;
            if (wlen == 2u && memcmp(p, "..", 2u) == 0) {
                char *slash = strrchr(path, '/');
                if (slash != NULL && slash != path)
                    *slash = 0;
                else
                    memcpy(path, "/", 2u);
            } else if (!(wlen == 1u && p[0] == '.')) {
                size_t slash_bytes;
                current = strlen(path);
                if (current == 0u || current >= sizeof(path))
                    return -1;
                slash_bytes = path[current - 1u] != '/' ? 1u : 0u;
                if ((size_t)wlen > sizeof(path) - current - slash_bytes - 1u)
                    return -1;
                if (slash_bytes != 0u)
                    path[current++] = '/';
                memcpy(path + current, p, wlen);
                path[current + wlen] = 0;
            }
            p += wlen;
            if (p9_qid_for_path(path, resp + 9u + (size_t)i * 13u) != 0)
                return -1; /* Do not invent successful walks or fake Qids. */
        }
        {
            P9Fid *dest = newfid == fid ? src : fid_alloc(newfid);
            if (dest == NULL)
                return -1;
            VfsInode target;
            if (vfs_stat(path, &target) != 0)
                return -1;
            memcpy(dest->path, path, strlen(path) + 1u);
            dest->object = target.object;
        }
        rlen = response_bytes;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5u, tag);
        write_u16(resp + 7u, nwname);
        return (int)rlen;
    }

    case P9_TOPEN: {
        uint32_t fid = read_u32(body);
        uint8_t mode = body[4];
        P9Fid *f = fid_find(fid);
        int fd;
        if (!p9_fid_path_matches(f) || f->open != 0 || (mode & 3u) == 3u ||
            resp_max < 24u || s_msize < 24u ||
            p9_qid_for_path(f->path, resp + 7u) != 0)
            return -1;
        fd = vfs_open(f->path, (int)mode);
        if (fd < 3)
            return -1;
        f->fd = fd;
        f->mode = mode;
        f->open = 1;
        rlen = 24u;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5u, tag);
        write_u32(resp + 20u, s_msize - 24u);
        return (int)rlen;
    }

    case P9_TREAD: {
        uint32_t fid    = read_u32(body);
        uint64_t offset = nx_load_le64(body + 4u);
        uint32_t count  = read_u32(body + 12);
        P9Fid *f = fid_find(fid);
        if (!p9_fid_live(f) || !f->open || f->fd < 0 ||
            (f->mode & 3u) == 1u ||
            offset > (uint64_t)INT64_MAX ||
            vfs_seek(f->fd, (int64_t)offset, 0) < 0) {
            return -1;
        }
        if (count > s_msize - 11) count = s_msize - 11;
        rlen = 11 + count;
        if (rlen > resp_max)
            return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        int n = vfs_read(f->fd, resp + 11, count);
        if (n < 0)
            return -1;
        write_u32(resp + 7, (uint32_t)n);
        rlen = 11 + (uint32_t)n;
        write_u32(resp, (uint32_t)rlen);
        return (int)rlen;
    }

    case P9_TWRITE: {
        uint32_t fid   = read_u32(body);
        uint64_t offset = nx_load_le64(body + 4u);
        uint32_t count = read_u32(body + 12);
        const uint8_t *data = body + 16;
        P9Fid *f = fid_find(fid);
        int n = -1;
        if (p9_fid_live(f) && f->open && f->fd >= 0 &&
            ((f->mode & 3u) == 1u || (f->mode & 3u) == 2u) &&
            offset <= (uint64_t)INT64_MAX &&
            vfs_seek(f->fd, (int64_t)offset, 0) >= 0)
            n = vfs_write(f->fd, data, count);
        if (n < 0)
            return -1;
        rlen = 11;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        write_u32(resp + 7, (uint32_t)(n < 0 ? 0 : n));
        return (int)rlen;
    }

    case P9_TCLUNK: {
        uint32_t fid = read_u32(body);
        P9Fid *f = fid_find(fid);
        if (f == NULL)
            return -1;
        if (f->open && f->fd >= 0 && vfs_close(f->fd) != 0)
            return -1;
        memset(f, 0, sizeof(*f));
        rlen = 7;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        return (int)rlen;
    }

    case P9_TSTAT:
        /* A fabricated empty Rstat would misreport nonexistent metadata.
         * Return explicit unsupported until real 9P stat codec is supplied. */
        return -1;

    default:
        return -1;
    }

    return (int)rlen;
}
