/*
 * fs/9p.c — 9P2000 server
 *
 * Handles: Tversion, Tattach, Twalk, Topen, Tread, Twrite, Tclunk, Tstat.
 * Server state is published to /sys/9p/ in the registry.
 * Requests arrive as IPC messages on /sys/9p/inbox.
 */

#include "include/nexs_fs9p.h"
#include "../kernel/include/nexs_vfs.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   FID TABLE
   ========================================================= */

#define P9_MAX_FIDS 64
#define P9_MSIZE    8192

typedef struct {
    int     in_use;
    uint32_t fid;
    char     path[REG_PATH_MAX];
    int      fd;
    int      open;
} P9Fid;

static P9Fid s_fids[P9_MAX_FIDS];
static uint32_t s_msize = P9_MSIZE;

static P9Fid *fid_find(uint32_t fid) {
    for (int i = 0; i < P9_MAX_FIDS; i++)
        if (s_fids[i].in_use && s_fids[i].fid == fid) return &s_fids[i];
    return NULL;
}

static P9Fid *fid_alloc(uint32_t fid) {
    for (int i = 0; i < P9_MAX_FIDS; i++) {
        if (!s_fids[i].in_use) {
            s_fids[i].in_use = 1;
            s_fids[i].fid    = fid;
            s_fids[i].open   = 0;
            s_fids[i].fd     = -1;
            return &s_fids[i];
        }
    }
    return NULL;
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

/* =========================================================
   INIT
   ========================================================= */

void p9_server_init(void) {
    memset(s_fids, 0, sizeof(s_fids));
    reg_ipc_init_queue("/sys/9p/inbox", 64);
    reg_set("/sys/9p/msize",  val_int(s_msize), RK_READ);
    reg_set("/sys/9p/status", val_str("ok"),     RK_READ);
}

/* =========================================================
   HANDLE ONE MESSAGE
   Minimal frame: [size:4][type:1][tag:2][body...]
   ========================================================= */

int p9_server_handle(const uint8_t *req, size_t req_len,
                     uint8_t *resp, size_t resp_max) {
    if (req_len < 7) return -1;

    uint32_t msg_size = read_u32(req);
    uint8_t  msg_type = req[4];
    uint16_t tag      = read_u16(req + 5);
    const uint8_t *body = req + 7;
    (void)msg_size;

    uint8_t rtype = msg_type + 1; /* Rxxx = Txxx + 1 */
    size_t  rlen  = 0;

    switch (msg_type) {

    case P9_TVERSION: {
        /* Negotiate msize and protocol */
        uint32_t client_msize = read_u32(body);
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
        break;
    }

    case P9_TATTACH: {
        uint32_t fid = read_u32(body);
        /* Skip afid(4), uname(2+str), aname(2+str) */
        const uint8_t *p = body + 4;
        uint16_t uname_len = read_u16(p); p += 2 + uname_len;
        uint16_t aname_len = read_u16(p); p += 2 + aname_len;
        (void)p;

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
            break;
        }
        strncpy(f->path, "/", sizeof(f->path) - 1);

        /* Rattach: size[4] type[1] tag[2] qid[13] */
        rlen = 20;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        memset(resp + 7, 0, 13); /* qid = root dir */
        resp[7] = 0x80; /* QTDIR */
        break;
    }

    case P9_TWALK: {
        uint32_t fid  = read_u32(body);
        uint32_t newfid = read_u32(body + 4);
        uint16_t nwname = read_u16(body + 8);
        const uint8_t *p = body + 10;

        P9Fid *src = fid_find(fid);
        if (!src) { rlen = 0; break; }

        char path[REG_PATH_MAX];
        strncpy(path, src->path, sizeof(path) - 1);

        for (uint16_t i = 0; i < nwname; i++) {
            uint16_t wlen = read_u16(p); p += 2;
            char wname[256] = {0};
            if (wlen < sizeof(wname)) {
                memcpy(wname, p, wlen);
                wname[wlen] = '\0';
            }
            p += wlen;
            if (strcmp(wname, "..") == 0) {
                char *slash = strrchr(path, '/');
                if (slash && slash != path) *slash = '\0';
            } else {
                size_t plen = strlen(path);
                if (path[plen - 1] != '/')
                    strncat(path, "/", sizeof(path) - plen - 1);
                strncat(path, wname, sizeof(path) - strlen(path) - 1);
            }
        }

        P9Fid *nf = (newfid != fid) ? fid_alloc(newfid) : src;
        if (nf && nf != src) strncpy(nf->path, path, sizeof(nf->path) - 1);

        /* Rwalk: size[4] type[1] tag[2] nwqid[2] (nwname × qid[13]) */
        rlen = 9 + (size_t)nwname * 13;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        write_u16(resp + 7, nwname);
        memset(resp + 9, 0, (size_t)nwname * 13);
        break;
    }

    case P9_TOPEN: {
        uint32_t fid  = read_u32(body);
        uint8_t  mode = body[4];
        P9Fid *f = fid_find(fid);
        if (!f) { rlen = 0; break; }
        f->fd   = vfs_open(f->path, (int)mode);
        f->open = 1;
        rlen = 24; /* size[4] type[1] tag[2] qid[13] iounit[4] */
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        memset(resp + 7, 0, 13);
        write_u32(resp + 20, s_msize - 24);
        break;
    }

    case P9_TREAD: {
        uint32_t fid    = read_u32(body);
        /* offset[8] count[4] */
        uint32_t count  = read_u32(body + 12);
        P9Fid *f = fid_find(fid);
        if (!f || !f->open || f->fd < 0) { rlen = 0; break; }
        if (count > s_msize - 11) count = s_msize - 11;
        rlen = 11 + count;
        if (rlen > resp_max) { rlen = 0; break; }
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        int n = vfs_read(f->fd, resp + 11, count);
        if (n < 0) n = 0;
        write_u32(resp + 7, (uint32_t)n);
        rlen = 11 + (uint32_t)n;
        write_u32(resp, (uint32_t)rlen);
        break;
    }

    case P9_TWRITE: {
        uint32_t fid   = read_u32(body);
        uint32_t count = read_u32(body + 12);
        const uint8_t *data = body + 16;
        P9Fid *f = fid_find(fid);
        int n = 0;
        if (f && f->open && f->fd >= 0)
            n = vfs_write(f->fd, data, count);
        rlen = 11;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        write_u32(resp + 7, (uint32_t)(n < 0 ? 0 : n));
        break;
    }

    case P9_TCLUNK: {
        uint32_t fid = read_u32(body);
        P9Fid *f = fid_find(fid);
        if (f) {
            if (f->open && f->fd >= 0) vfs_close(f->fd);
            memset(f, 0, sizeof(*f));
        }
        rlen = 7;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        break;
    }

    case P9_TSTAT: {
        rlen = 7;
        if (rlen > resp_max) return -1;
        write_u32(resp, (uint32_t)rlen);
        resp[4] = rtype;
        write_u16(resp + 5, tag);
        break;
    }

    default:
        return -1;
    }

    return (int)rlen;
}
