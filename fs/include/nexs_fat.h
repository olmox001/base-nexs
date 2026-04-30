/*
 * fs/include/nexs_fat.h — FAT16 read-only filesystem (initrd)
 */
#ifndef NEXS_FAT_H
#define NEXS_FAT_H
#pragma once
#include <stdint.h>
#include <stddef.h>

#define FAT_NAME_MAX  256

typedef struct {
    int      handle;
    uint32_t cluster;
    uint32_t size;
    uint32_t pos;
    int      is_dir;
    char     name[FAT_NAME_MAX];
} FatEntry;

int  fat_init(uint32_t dev_id);
int  fat_open(const char *path);
int  fat_read(int handle, void *buf, size_t n);
int  fat_readdir(int handle, FatEntry *entry_out);
void fat_close(int handle);

#endif
