#ifndef UFS_TYPES_H
#define UFS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef uint64_t  usize;

#ifndef NULL
#define NULL ((void *)0)
#endif

// Status codes
typedef enum {
    USB_OK            = 0,
    USB_ERR_DISK      = -1,
    USB_ERR_MEM       = -2,
    USB_ERR_FS        = -3,
    USB_ERR_GPT       = -4,
    USB_ERR_SIG       = -5,
    USB_ERR_PERM      = -6,
    USB_ERR_NODEV     = -7,
    USB_ERR_NOMEM     = -8,
    USB_ERR_PARAM     = -9,
    USB_ERR_TIMEOUT   = -10,
} USB_Status;

#define UFS_BLOCK_SIZE      4096
#define UFS_NAME_MAX        255
#define UFS_PATH_MAX        1024
#define UFS_DIRECT_BLOCKS   12
#define UFS_MAGIC           0x554653394F530000ULL
#define UFS_VERSION         1

typedef struct {
    u8   type;
    u32  version;
    u64  path;
} __attribute__((packed)) UFS_Qid;

#define QTDIR     0x80
#define QTAPPEND  0x40
#define QTEXCL    0x20
#define QTAUTH    0x08
#define QTFILE    0x00

#define DMDIR     0x80000000
#define DMAPPEND  0x40000000
#define DMEXCL    0x20000000
#define DMAUTH    0x08000000
#define DMREAD    0x4
#define DMWRITE   0x2
#define DMEXEC    0x1

typedef struct {
    UFS_Qid  qid;
    u32      mode;
    u32      atime;
    u32      mtime;
    u64      size;
    char     owner[32];
    char     group[32];
    u64      direct[UFS_DIRECT_BLOCKS];
    u64      indirect;
    u64      dindirect;
    u32      nlink;
    u32      _pad;
} UFS_Inode;

typedef struct {
    UFS_Qid  qid;
    u16      name_len;
    char     name[UFS_NAME_MAX + 1];
} __attribute__((packed)) UFS_DirEntry;

typedef struct {
    u64      magic;
    u32      version;
    u32      block_size;
    u64      total_blocks;
    u64      free_blocks;
    u64      bitmap_start;
    u64      bitmap_blocks;
    u64      inode_start;
    UFS_Qid  root_qid;
    u32      fs_type;
    u32      _pad;
} UFS_Superblock;

typedef struct {
    u32      disk_id;
    u64      part_start_lba;
    u64      part_size_lba;
    UFS_Superblock sb;
} UFS_Tree;

typedef struct {
    u32      fid;
    UFS_Qid  qid;
    u64      inode_block;
    u32      mode;
    u64      offset;
    bool     open;
} UFS_Fid;

#define OREAD   0
#define OWRITE  1
#define ORDWR   2
#define OEXEC   3
#define OTRUNC  0x10

#endif
