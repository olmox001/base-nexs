/*
 * fs/regfs.c — Registry serialized to disk (.nexsreg)
 *
 * Binary format:
 *   Header (32B): "NEXSREG1" | version(4) | n_keys(4) | reserved(8) | crc32(4) | pad(4)
 *   RegRecord[]: path(256) | type(1) | rights(1) | data_len(4) | data(var)
 *
 * Walks the registry subtree at reg_path, serializes to file_path.
 * On load, restores all keys under mount_at prefix.
 */

#include "include/nexs_regfs.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define REGFS_PATH_MAX  256
#define REGFS_DATA_MAX  4096

/* =========================================================
   SIMPLE CRC32
   ========================================================= */

static uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320UL : 0);
    return crc;
}

/* =========================================================
   HEADER
   ========================================================= */

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t n_keys;
    uint64_t reserved;
    uint32_t crc32;
    uint32_t pad;
} __attribute__((packed)) RegfsHeader;

/* =========================================================
   WALK + SAVE
   ========================================================= */

typedef struct { int count; FILE *fp; uint32_t crc; } RegfsSaveCtx;

static void save_key(RegKey *k, RegfsSaveCtx *ctx) {
    if (!k || !ctx->fp) return;
    char path_buf[REGFS_PATH_MAX] = {0};
    strncpy(path_buf, k->path, REGFS_PATH_MAX - 1);
    fwrite(path_buf, 1, REGFS_PATH_MAX, ctx->fp);

    uint8_t type   = (uint8_t)k->val.type;
    uint8_t rights = k->rights;
    fwrite(&type,   1, 1, ctx->fp);
    fwrite(&rights, 1, 1, ctx->fp);

    char data_buf[REGFS_DATA_MAX] = {0};
    uint32_t data_len = 0;
    if (k->val.type == TYPE_INT) {
        data_len = 8;
        memcpy(data_buf, &k->val.ival, 8);
    } else if (k->val.type == TYPE_FLOAT) {
        data_len = 8;
        memcpy(data_buf, &k->val.fval, 8);
    } else if ((k->val.type == TYPE_STR || k->val.type == TYPE_ERR)
               && k->val.data) {
        data_len = (uint32_t)strlen((char *)k->val.data);
        if (data_len >= REGFS_DATA_MAX) data_len = REGFS_DATA_MAX - 1;
        memcpy(data_buf, k->val.data, data_len);
    }
    fwrite(&data_len, 4, 1, ctx->fp);
    if (data_len > 0) fwrite(data_buf, 1, data_len, ctx->fp);

    ctx->count++;
    for (size_t i = 0; i < REGFS_PATH_MAX; i++)
        ctx->crc = crc32_byte(ctx->crc, (uint8_t)path_buf[i]);
    ctx->crc = crc32_byte(ctx->crc, type);
    ctx->crc = crc32_byte(ctx->crc, rights);
    /* Include data_len and data in CRC to detect corruption */
    uint8_t len_bytes[4];
    memcpy(len_bytes, &data_len, 4);
    for (int _i = 0; _i < 4; _i++)
        ctx->crc = crc32_byte(ctx->crc, len_bytes[_i]);
    for (uint32_t _i = 0; _i < data_len; _i++)
        ctx->crc = crc32_byte(ctx->crc, (uint8_t)data_buf[_i]);

    RegKey *child = k->children;
    while (child) {
        save_key(child, ctx);
        child = child->next;
    }
}

int regfs_save(const char *reg_path, const char *file_path) {
    RegKey *root = reg_lookup(reg_path);
    if (!root) return -1;

    FILE *fp = fopen(file_path, "wb");
    if (!fp) return -1;

    RegfsHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, REGFS_MAGIC, 8);
    hdr.version = REGFS_VERSION;
    fwrite(&hdr, sizeof(hdr), 1, fp);

    RegfsSaveCtx ctx = { 0, fp, 0xFFFFFFFFUL };
    save_key(root, &ctx);

    hdr.n_keys = (uint32_t)ctx.count;
    hdr.crc32  = ctx.crc ^ 0xFFFFFFFFUL;
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, fp);

    fclose(fp);
    return 0;
}

/* =========================================================
   LOAD
   ========================================================= */

int regfs_load(const char *file_path, const char *mount_at) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) return -1;

    RegfsHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) { fclose(fp); return -1; }
    if (memcmp(hdr.magic, REGFS_MAGIC, 8) != 0 ||
        hdr.version != REGFS_VERSION) {
        fclose(fp); return -1;
    }

    size_t prefix_len = mount_at ? strlen(mount_at) : 0;

    for (uint32_t i = 0; i < hdr.n_keys; i++) {
        char path_buf[REGFS_PATH_MAX] = {0};
        uint8_t type, rights;
        uint32_t data_len;

        if (fread(path_buf, 1, REGFS_PATH_MAX, fp) != REGFS_PATH_MAX) break;
        if (fread(&type,    1, 1, fp) != 1) break;
        if (fread(&rights,  1, 1, fp) != 1) break;
        if (fread(&data_len,4, 1, fp) != 1) break;

        char data_buf[REGFS_DATA_MAX] = {0};
        if (data_len > 0) {
            if (data_len >= REGFS_DATA_MAX) { fseek(fp, data_len, SEEK_CUR); continue; }
            if (fread(data_buf, 1, data_len, fp) != data_len) break;
        }

        /* Build target path (strip original prefix, prepend mount_at) */
        char target_path[REG_PATH_MAX];
        if (mount_at && mount_at[0]) {
            snprintf(target_path, sizeof(target_path), "%s%s",
                     mount_at, path_buf + prefix_len);
        } else {
            strncpy(target_path, path_buf, sizeof(target_path) - 1);
        }

        Value v;
        memset(&v, 0, sizeof(v));
        v.type = (ValueType)type;
        if (type == TYPE_INT) {
            memcpy(&v.ival, data_buf, 8);
        } else if (type == TYPE_FLOAT) {
            memcpy(&v.fval, data_buf, 8);
        } else if (type == TYPE_STR || type == TYPE_ERR) {
            v = val_str(data_buf);
        } else {
            v = val_nil();
        }
        reg_set(target_path, v, rights);
        val_free(&v);
    }

    fclose(fp);
    return 0;
}

/* =========================================================
   SYNC + WATCH (stubs)
   ========================================================= */

int regfs_sync(const char *reg_path, const char *file_path) {
    return regfs_save(reg_path, file_path);
}

void regfs_watch(const char *reg_path, const char *notify_ipc_path) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/sys/regfs/watch/%s/notify",
             reg_path[0] == '/' ? reg_path + 1 : reg_path);
    reg_set(path, val_str(notify_ipc_path), RK_READ | RK_WRITE);
}
