/*
 * fs/include/nexs_regfs.h — Registry serialized to disk (.nexsreg format)
 */
#ifndef NEXS_REGFS_H
#define NEXS_REGFS_H
#pragma once
#include <stdint.h>

/* Binary format .nexsreg:
 *   Header (32B): "NEXSREG1" | version(4) | n_keys(4) | root_off(8) | crc32(4) | pad(4)
 *   RegRecord[]: path(256B) | type(1B) | rights(1B) | data_len(4B) | data(var) | n_children(4B)
 */
#define REGFS_MAGIC   "NEXSREG1"
#define REGFS_VERSION 1

int  regfs_save(const char *reg_path, const char *file_path);
int  regfs_load(const char *file_path, const char *mount_at);
int  regfs_sync(const char *reg_path, const char *file_path);
void regfs_watch(const char *reg_path, const char *notify_ipc_path);

#endif
