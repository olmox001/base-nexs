/* ===========================================================================
 * ufs_9p.h — 9P2000 Protocol Codec
 * ===========================================================================
 * BSD 3-Clause — Copyright (c) 2025, NeXs Operating System
 * ======================================================================== */

#ifndef UFS_9P_H
#define UFS_9P_H

#include "ufs_types.h"

/* ---- 9P Message Types -------------------------------------------------- */
enum {
    Tversion = 100, Rversion = 101,
    Tauth    = 102, Rauth    = 103,
    Tattach  = 104, Rattach  = 105,
    Terror   = 106, Rerror   = 107,
    Tflush   = 108, Rflush   = 109,
    Twalk    = 110, Rwalk    = 111,
    Topen    = 112, Ropen    = 113,
    Tcreate  = 114, Rcreate  = 115,
    Tread    = 116, Rread    = 117,
    Twrite   = 118, Rwrite   = 119,
    Tclunk   = 120, Rclunk   = 121,
    Tremove  = 122, Rremove  = 123,
    Tstat    = 124, Rstat    = 125,
    Twstat   = 126, Rwstat   = 127,
};

#define P9_NOTAG   0xFFFF
#define P9_NOFID   0xFFFFFFFF
#define P9_MAX_WELEM 16
#define P9_MSIZE   8192

/* ---- 9P Message Structure ---------------------------------------------- */
typedef struct {
    u32  size;
    u8   type;
    u16  tag;

    union {
        /* Tversion / Rversion */
        struct { u32 msize; char version[32]; } version;

        /* Tattach */
        struct { u32 fid; u32 afid; char uname[32]; char aname[64]; } tattach;
        /* Rattach */
        struct { UFS_Qid qid; } rattach;

        /* Rerror */
        struct { char ename[128]; } rerror;

        /* Tflush */
        struct { u16 oldtag; } tflush;

        /* Twalk */
        struct { u32 fid; u32 newfid; u16 nwname; char wname[P9_MAX_WELEM][UFS_NAME_MAX+1]; } twalk;
        /* Rwalk */
        struct { u16 nwqid; UFS_Qid wqid[P9_MAX_WELEM]; } rwalk;

        /* Topen */
        struct { u32 fid; u8 mode; } topen;
        /* Ropen */
        struct { UFS_Qid qid; u32 iounit; } ropen;

        /* Tcreate */
        struct { u32 fid; char name[UFS_NAME_MAX+1]; u32 perm; u8 mode; } tcreate;
        /* Rcreate */
        struct { UFS_Qid qid; u32 iounit; } rcreate;

        /* Tread */
        struct { u32 fid; u64 offset; u32 count; } tread;
        /* Rread */
        struct { u32 count; u8 *data; } rread;

        /* Twrite */
        struct { u32 fid; u64 offset; u32 count; const u8 *data; } twrite;
        /* Rwrite */
        struct { u32 count; } rwrite;

        /* Tclunk / Tremove */
        struct { u32 fid; } tclunk;

        /* Tstat / Rstat — simplified stat buffer */
        struct { u32 fid; } tstat;
        struct {
            UFS_Qid qid; u32 mode; u32 atime; u32 mtime;
            u64 length; char name[UFS_NAME_MAX+1];
            char uid[32]; char gid[32];
        } rstat;
    };
} UFS_9PMsg;

/* ---- API --------------------------------------------------------------- */

/* Pack a 9P message into wire format. Returns bytes written. */
u32  ufs_9p_pack(const UFS_9PMsg *msg, u8 *buf, u32 max);

/* Unpack wire bytes into a 9P message. Returns true on success. */
bool ufs_9p_unpack(const u8 *buf, u32 len, UFS_9PMsg *out);

/* Pack a 9P string: [2-byte len][data]. Returns bytes written. */
u32  ufs_9p_pack_str(u8 *buf, const char *s);

/* Unpack a 9P string. Returns bytes consumed. */
u32  ufs_9p_unpack_str(const u8 *buf, char *out, u32 max);

/* Pack a Qid: [1 type][4 version][8 path]. Returns 13. */
u32  ufs_9p_pack_qid(u8 *buf, const UFS_Qid *q);
u32  ufs_9p_unpack_qid(const u8 *buf, UFS_Qid *q);

#endif /* UFS_9P_H */
