/*
 * fs/fat.c — FAT16 read-only filesystem for initrd
 *
 * Minimal implementation: reads BPB, FAT table, and directory entries.
 * Designed for the initrd partition passed by the bootloader.
 * All state is static (single mount point).
 */

#include "include/nexs_fat.h"
#include "../kernel/include/nexs_blk.h"
#include <string.h>
#include <stdio.h>

/* =========================================================
   FAT16 BPB
   ========================================================= */

typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  n_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t n_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) Fat16BPB;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t file_size;
} __attribute__((packed)) Fat16DirEntry;

/* =========================================================
   STATE
   ========================================================= */

#define FAT_MAX_HANDLES 16

typedef struct {
    int      in_use;
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t pos;
    int      is_dir;
    uint32_t dir_idx;
} FatHandle;

static uint32_t  s_dev            = 0;
static uint32_t  s_fat_start_lba  = 0;
static uint32_t  s_root_start_lba = 0;
static uint32_t  s_data_start_lba = 0;
static uint32_t  s_cluster_size   = 0;
static uint32_t  s_root_entries   = 0;
static int       s_mounted        = 0;
static FatHandle s_handles[FAT_MAX_HANDLES];

static uint16_t fat_entry(uint32_t cluster) {
    /* Read the FAT sector containing cluster's entry */
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_lba    = s_fat_start_lba + fat_offset / 512;
    uint8_t  sector[BLK_SIZE];
    blk_read(s_dev, fat_lba, sector);
    uint32_t off = fat_offset % 512;
    return (uint16_t)(sector[off] | ((uint16_t)sector[off + 1] << 8));
}

/* =========================================================
   PUBLIC API
   ========================================================= */

int fat_init(uint32_t dev_id) {
    uint8_t boot[BLK_SIZE];
    if (blk_read(dev_id, 0, boot) != 0) return -1;

    Fat16BPB *bpb = (Fat16BPB *)boot;
    if (bpb->bytes_per_sector != 512) return -1;

    s_dev            = dev_id;
    s_fat_start_lba  = bpb->reserved_sectors;
    s_root_start_lba = s_fat_start_lba + (uint32_t)bpb->n_fats * bpb->fat_size_16;
    s_root_entries   = bpb->root_entry_count;
    s_cluster_size   = bpb->sectors_per_cluster;
    s_data_start_lba = s_root_start_lba + (s_root_entries * 32 + 511) / 512;
    s_mounted        = 1;
    memset(s_handles, 0, sizeof(s_handles));
    return 0;
}

static int handle_alloc(void) {
    for (int i = 0; i < FAT_MAX_HANDLES; i++)
        if (!s_handles[i].in_use) return i;
    return -1;
}

static Fat16DirEntry *find_in_root(const char *name, uint8_t *sector_buf) {
    uint32_t n_sectors = (s_root_entries * 32 + 511) / 512;
    for (uint32_t s = 0; s < n_sectors; s++) {
        blk_read(s_dev, s_root_start_lba + s, sector_buf);
        Fat16DirEntry *d = (Fat16DirEntry *)sector_buf;
        int entries_in_sector = 512 / 32;
        for (int i = 0; i < entries_in_sector; i++) {
            if (d[i].name[0] == 0x00) return NULL;
            if ((uint8_t)d[i].name[0] == 0xE5) continue;
            if (d[i].attr & 0x08) continue; /* volume label */

            char fname[13];
            int f = 0;
            for (int j = 0; j < 8 && d[i].name[j] != ' '; j++) fname[f++] = d[i].name[j];
            if (d[i].ext[0] != ' ') {
                fname[f++] = '.';
                for (int j = 0; j < 3 && d[i].ext[j] != ' '; j++) fname[f++] = d[i].ext[j];
            }
            fname[f] = '\0';
            if (strcmp(fname, name) == 0) return &d[i];
        }
    }
    return NULL;
}

int fat_open(const char *path) {
    if (!s_mounted) return -1;
    int h = handle_alloc();
    if (h < 0) return -1;

    const char *name = path;
    if (*name == '/') name++;

    uint8_t sector[BLK_SIZE];
    Fat16DirEntry *e = find_in_root(name, sector);
    if (!e) return -1;

    s_handles[h].in_use        = 1;
    s_handles[h].first_cluster = e->first_cluster;
    s_handles[h].file_size     = e->file_size;
    s_handles[h].pos           = 0;
    s_handles[h].is_dir        = (e->attr & 0x10) ? 1 : 0;
    s_handles[h].dir_idx       = 0;
    return h;
}

int fat_read(int handle, void *buf, size_t n) {
    if (handle < 0 || handle >= FAT_MAX_HANDLES) return -1;
    FatHandle *fh = &s_handles[handle];
    if (!fh->in_use) return -1;

    size_t remaining = fh->file_size - fh->pos;
    if (n > remaining) n = remaining;
    if (n == 0) return 0;

    /* Traverse FAT chain to current cluster */
    uint32_t cluster_idx = fh->pos / (s_cluster_size * 512);
    uint32_t cluster = fh->first_cluster;
    for (uint32_t i = 0; i < cluster_idx; i++) {
        uint16_t next = fat_entry(cluster);
        if (next >= 0xFFF8) return 0;
        cluster = next;
    }

    uint8_t  sector[BLK_SIZE];
    uint8_t *out = (uint8_t *)buf;
    size_t   read_total = 0;

    while (read_total < n && cluster < 0xFFF8) {
        uint32_t lba = s_data_start_lba + (cluster - 2) * s_cluster_size;
        blk_read(s_dev, lba, sector);
        uint32_t offset_in_cluster = fh->pos % (s_cluster_size * 512);
        size_t   can_read = (s_cluster_size * 512) - offset_in_cluster;
        if (can_read > n - read_total) can_read = n - read_total;
        memcpy(out + read_total, sector + offset_in_cluster, can_read);
        read_total += can_read;
        fh->pos    += (uint32_t)can_read;
        if (fh->pos % (s_cluster_size * 512) == 0) {
            uint16_t next = fat_entry(cluster);
            cluster = (next >= 0xFFF8) ? 0xFFF8 : next;
        }
    }
    return (int)read_total;
}

int fat_readdir(int handle, FatEntry *entry_out) {
    (void)handle; (void)entry_out;
    return -1; /* stub */
}

void fat_close(int handle) {
    if (handle >= 0 && handle < FAT_MAX_HANDLES)
        s_handles[handle].in_use = 0;
}
