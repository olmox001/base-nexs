/* ===========================================================================
 * ufs_9p.c — 9P2000 Protocol Codec (Little-Endian Wire Format)
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#include "include/ufs_9p.h"
#include <string.h>

/* ---- Little-Endian Helpers --------------------------------------------- */

static inline void put_u8(u8 *b, u8 v)   { b[0] = v; }
static inline void put_u16(u8 *b, u16 v) { b[0] = v; b[1] = v >> 8; }
static inline void put_u32(u8 *b, u32 v) { b[0]=v;b[1]=v>>8;b[2]=v>>16;b[3]=v>>24; }
static inline void put_u64(u8 *b, u64 v) { put_u32(b,(u32)v); put_u32(b+4,(u32)(v>>32)); }

static inline u8  get_u8(const u8 *b)  { return b[0]; }
static inline u16 get_u16(const u8 *b) { return (u16)b[0] | ((u16)b[1] << 8); }
static inline u32 get_u32(const u8 *b) { return (u32)b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|((u32)b[3]<<24); }
static inline u64 get_u64(const u8 *b) { return (u64)get_u32(b) | ((u64)get_u32(b+4) << 32); }

/* ---- Qid Codec --------------------------------------------------------- */

u32 ufs_9p_pack_qid(u8 *buf, const UFS_Qid *q) {
    put_u8(buf, q->type);
    put_u32(buf + 1, q->version);
    put_u64(buf + 5, q->path);
    return 13;
}

u32 ufs_9p_unpack_qid(const u8 *buf, UFS_Qid *q) {
    q->type    = get_u8(buf);
    q->version = get_u32(buf + 1);
    q->path    = get_u64(buf + 5);
    return 13;
}

/* ---- String Codec ------------------------------------------------------ */

static usize slen(const char *s) { usize n=0; while(s[n])n++; return n; }

u32 ufs_9p_pack_str(u8 *buf, const char *s) {
    u16 len = (u16)slen(s);
    put_u16(buf, len);
    memcpy(buf + 2, s, len);
    return 2 + len;
}

u32 ufs_9p_unpack_str(const u8 *buf, char *out, u32 max) {
    u16 len = get_u16(buf);
    u32 cp = len < max - 1 ? len : max - 1;
    memcpy(out, buf + 2, cp);
    out[cp] = '\0';
    return 2 + len;
}

/* ---- Pack --------------------------------------------------------------- */

u32 ufs_9p_pack(const UFS_9PMsg *msg, u8 *buf, u32 max) {
    u8 *p = buf + 4;  /* Skip size field, fill at end */
    put_u8(p, msg->type); p += 1;
    put_u16(p, msg->tag); p += 2;

    switch (msg->type) {
    case Tversion: case Rversion:
        put_u32(p, msg->version.msize); p += 4;
        p += ufs_9p_pack_str(p, msg->version.version);
        break;
    case Tattach:
        put_u32(p, msg->tattach.fid); p += 4;
        put_u32(p, msg->tattach.afid); p += 4;
        p += ufs_9p_pack_str(p, msg->tattach.uname);
        p += ufs_9p_pack_str(p, msg->tattach.aname);
        break;
    case Rattach:
        p += ufs_9p_pack_qid(p, &msg->rattach.qid);
        break;
    case Rerror:
        p += ufs_9p_pack_str(p, msg->rerror.ename);
        break;
    case Tflush:
        put_u16(p, msg->tflush.oldtag); p += 2;
        break;
    case Rflush: break;
    case Twalk:
        put_u32(p, msg->twalk.fid); p += 4;
        put_u32(p, msg->twalk.newfid); p += 4;
        put_u16(p, msg->twalk.nwname); p += 2;
        for (u16 i = 0; i < msg->twalk.nwname; i++)
            p += ufs_9p_pack_str(p, msg->twalk.wname[i]);
        break;
    case Rwalk:
        put_u16(p, msg->rwalk.nwqid); p += 2;
        for (u16 i = 0; i < msg->rwalk.nwqid; i++)
            p += ufs_9p_pack_qid(p, &msg->rwalk.wqid[i]);
        break;
    case Topen:
        put_u32(p, msg->topen.fid); p += 4;
        put_u8(p, msg->topen.mode); p += 1;
        break;
    case Ropen:
        p += ufs_9p_pack_qid(p, &msg->ropen.qid);
        put_u32(p, msg->ropen.iounit); p += 4;
        break;
    case Tcreate:
        put_u32(p, msg->tcreate.fid); p += 4;
        p += ufs_9p_pack_str(p, msg->tcreate.name);
        put_u32(p, msg->tcreate.perm); p += 4;
        put_u8(p, msg->tcreate.mode); p += 1;
        break;
    case Rcreate:
        p += ufs_9p_pack_qid(p, &msg->rcreate.qid);
        put_u32(p, msg->rcreate.iounit); p += 4;
        break;
    case Tread:
        put_u32(p, msg->tread.fid); p += 4;
        put_u64(p, msg->tread.offset); p += 8;
        put_u32(p, msg->tread.count); p += 4;
        break;
    case Rread:
        put_u32(p, msg->rread.count); p += 4;
        if (msg->rread.data)
            memcpy(p, msg->rread.data, msg->rread.count);
        p += msg->rread.count;
        break;
    case Twrite:
        put_u32(p, msg->twrite.fid); p += 4;
        put_u64(p, msg->twrite.offset); p += 8;
        put_u32(p, msg->twrite.count); p += 4;
        if (msg->twrite.data)
            memcpy(p, msg->twrite.data, msg->twrite.count);
        p += msg->twrite.count;
        break;
    case Rwrite:
        put_u32(p, msg->rwrite.count); p += 4;
        break;
    case Tclunk: case Tremove:
        put_u32(p, msg->tclunk.fid); p += 4;
        break;
    case Rclunk: case Rremove: break;
    case Tstat:
        put_u32(p, msg->tstat.fid); p += 4;
        break;
    case Rstat:
        p += ufs_9p_pack_qid(p, &msg->rstat.qid);
        put_u32(p, msg->rstat.mode); p += 4;
        put_u32(p, msg->rstat.atime); p += 4;
        put_u32(p, msg->rstat.mtime); p += 4;
        put_u64(p, msg->rstat.length); p += 8;
        p += ufs_9p_pack_str(p, msg->rstat.name);
        p += ufs_9p_pack_str(p, msg->rstat.uid);
        p += ufs_9p_pack_str(p, msg->rstat.gid);
        break;
    default: break;
    }

    u32 total = (u32)(p - buf);
    if (total > max) return 0;
    put_u32(buf, total);
    return total;
}

/* ---- Unpack ------------------------------------------------------------- */

bool ufs_9p_unpack(const u8 *buf, u32 len, UFS_9PMsg *out) {
    if (len < 7) return false;
    memset(out, 0, sizeof(*out));

    const u8 *p = buf;
    out->size = get_u32(p); p += 4;
    out->type = get_u8(p);  p += 1;
    out->tag  = get_u16(p); p += 2;

    if (out->size > len) return false;

    switch (out->type) {
    case Tversion: case Rversion:
        out->version.msize = get_u32(p); p += 4;
        p += ufs_9p_unpack_str(p, out->version.version, sizeof(out->version.version));
        break;
    case Tattach:
        out->tattach.fid = get_u32(p); p += 4;
        out->tattach.afid = get_u32(p); p += 4;
        p += ufs_9p_unpack_str(p, out->tattach.uname, sizeof(out->tattach.uname));
        p += ufs_9p_unpack_str(p, out->tattach.aname, sizeof(out->tattach.aname));
        break;
    case Rattach:
        p += ufs_9p_unpack_qid(p, &out->rattach.qid);
        break;
    case Rerror:
        p += ufs_9p_unpack_str(p, out->rerror.ename, sizeof(out->rerror.ename));
        break;
    case Twalk:
        out->twalk.fid = get_u32(p); p += 4;
        out->twalk.newfid = get_u32(p); p += 4;
        out->twalk.nwname = get_u16(p); p += 2;
        for (u16 i = 0; i < out->twalk.nwname && i < P9_MAX_WELEM; i++)
            p += ufs_9p_unpack_str(p, out->twalk.wname[i], UFS_NAME_MAX+1);
        break;
    case Rwalk:
        out->rwalk.nwqid = get_u16(p); p += 2;
        for (u16 i = 0; i < out->rwalk.nwqid && i < P9_MAX_WELEM; i++)
            p += ufs_9p_unpack_qid(p, &out->rwalk.wqid[i]);
        break;
    case Topen:
        out->topen.fid = get_u32(p); p += 4;
        out->topen.mode = get_u8(p); p += 1;
        break;
    case Ropen:
        p += ufs_9p_unpack_qid(p, &out->ropen.qid);
        out->ropen.iounit = get_u32(p); p += 4;
        break;
    case Tclunk: case Tremove:
        out->tclunk.fid = get_u32(p); p += 4;
        break;
    case Tstat:
        out->tstat.fid = get_u32(p); p += 4;
        break;
    default: break;
    }
    return true;
}
