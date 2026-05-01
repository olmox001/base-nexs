/*
 * kernel/include/nexs_vfs.h — VFS layer (inode/dentry/mount)
 */
#ifndef NEXS_VFS_H
#define NEXS_VFS_H
#pragma once
#include "../../core/include/nexs_common.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t ino;
    uint32_t mode;      /* S_IFREG | S_IFDIR | S_IFLNK */
    uint32_t uid, gid;
    uint64_t size;
    uint64_t atime, mtime, ctime;
    char     reg_data_path[REG_PATH_MAX];
} VfsInode;

void vfs_init(void);
int vfs_open(const char *path, int flags);
int vfs_read(int fd, void *buf, size_t n);
int vfs_write(int fd, const void *buf, size_t n);
int vfs_close(int fd);
int vfs_dup(int oldfd, int newfd);     /* -1 for newfd = next available */
int vfs_seek(int fd, int64_t offset, int whence); /* SEEK_SET=0 CUR=1 END=2 */
int vfs_stat(const char *path, VfsInode *out);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_mount(const char *src, const char *dst, const char *fstype);

#endif
