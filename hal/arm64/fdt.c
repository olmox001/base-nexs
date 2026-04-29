/*
 * hal/arm64/fdt.c — Flattened Device Tree (FDT) parser
 * =======================================================
 * STEP 06: Parse FDT blob passed by QEMU/U-Boot in x0 at boot.
 *
 * boot.S must save x0 (FDT pointer) before clobbering registers
 * and pass it to nexs_hal_init() or directly to fdt_init().
 *
 * We parse:
 *   /memory → ram_base, ram_size
 *   /chosen → bootargs (cmdline)
 *   /pl011@* → uart_base
 *   /intc    → gic distributor + cpu interface bases
 *
 * FDT spec: https://www.devicetree.org/specifications/
 */

#include "../include/nexs_fdt.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>
#include <string.h>

/* ── FDT header (big-endian on wire) ─────────────────────── */
#define FDT_MAGIC     0xD00DFEED
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} __attribute__((packed)) FdtHeader;

/* ── Byte-swap helpers (FDT is big-endian) ───────────────── */
static inline uint32_t be32(uint32_t v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | (v >> 24);
}
static inline uint64_t be64(uint64_t v) {
    return ((uint64_t)be32((uint32_t)(v >> 32))) |
           ((uint64_t)be32((uint32_t)v) << 32);
}

/* ── State ────────────────────────────────────────────────── */
static const FdtHeader *s_hdr = NULL;
static uint64_t s_ram_base  = 0x40000000ULL; /* QEMU virt default */
static uint64_t s_ram_size  = 128ULL * 1024 * 1024;
static char     s_cmdline[256] = "";
static uint64_t s_uart_base = 0x09000000ULL;
static uint64_t s_gic_dist  = 0x08000000ULL;
static uint64_t s_gic_cpu   = 0x08010000ULL;

/* ── Accessors ────────────────────────────────────────────── */
uint64_t    fdt_get_ram_base(void)       { return s_ram_base; }
uint64_t    fdt_get_ram_size(void)       { return s_ram_size; }
const char *fdt_get_cmdline(void)        { return s_cmdline; }
uint64_t    fdt_get_uart_base(void)      { return s_uart_base; }
uint64_t    fdt_get_gic_dist_base(void)  { return s_gic_dist; }
uint64_t    fdt_get_gic_cpu_base(void)   { return s_gic_cpu; }

/* ── Parser ───────────────────────────────────────────────── */
void fdt_init(void *blob) {
    if (!blob) return;
    s_hdr = (const FdtHeader *)blob;
    if (be32(s_hdr->magic) != FDT_MAGIC) return;

    const uint8_t *struct_base  = (const uint8_t *)blob + be32(s_hdr->off_dt_struct);
    const char    *strings_base = (const char *)blob + be32(s_hdr->off_dt_strings);

    const uint32_t *p   = (const uint32_t *)struct_base;
    char node_name[128] = "";
    int  depth = 0;

    while (1) {
        uint32_t token = be32(*p++);
        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;
            strncpy(node_name, name, sizeof(node_name) - 1);
            /* Advance past name (aligned to 4 bytes) */
            size_t slen = strlen(name) + 1;
            p = (const uint32_t *)((const uint8_t *)p + ((slen + 3) & ~3));
            depth++;
            break;
        }
        case FDT_END_NODE:
            depth--;
            if (depth == 0) node_name[0] = '\0';
            break;

        case FDT_PROP: {
            uint32_t len     = be32(*p++);
            uint32_t nameoff = be32(*p++);
            const char *prop_name = strings_base + nameoff;
            const uint8_t *data   = (const uint8_t *)p;

            /* /memory@* reg property → base+size (2×uint64 cells) */
            if (strncmp(node_name, "memory", 6) == 0 &&
                strcmp(prop_name, "reg") == 0 && len >= 16) {
                s_ram_base = be64(*(const uint64_t *)data);
                s_ram_size = be64(*(const uint64_t *)(data + 8));
            }
            /* /chosen bootargs */
            if (strcmp(node_name, "chosen") == 0 &&
                strcmp(prop_name, "bootargs") == 0) {
                strncpy(s_cmdline, (const char *)data, sizeof(s_cmdline) - 1);
            }
            /* UART compatible pl011 */
            if (strcmp(prop_name, "compatible") == 0 &&
                strncmp((const char *)data, "arm,pl011", 9) == 0) {
                /* reg property follows in the same node */
            }
            if ((strncmp(node_name, "pl011", 5) == 0 ||
                 strncmp(node_name, "uart", 4) == 0) &&
                strcmp(prop_name, "reg") == 0 && len >= 16) {
                s_uart_base = be64(*(const uint64_t *)data);
            }
            /* GIC distributor */
            if ((strncmp(node_name, "intc", 4) == 0 ||
                 strncmp(node_name, "gic", 3) == 0) &&
                strcmp(prop_name, "reg") == 0 && len >= 32) {
                s_gic_dist = be64(*(const uint64_t *)data);
                s_gic_cpu  = be64(*(const uint64_t *)(data + 16));
            }

            /* Advance past data (aligned) */
            p = (const uint32_t *)((const uint8_t *)p + ((len + 3) & ~3));
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            goto done;
        default:
            goto done;
        }
    }
done:
    /* Publish to registry */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)s_ram_base);
        reg_set("/hal/fdt/ram_base",  val_str(buf),       RK_READ);
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)(s_ram_size >> 20));
        reg_set("/hal/fdt/ram_mb",    val_str(buf),       RK_READ);
        reg_set("/hal/fdt/cmdline",   val_str(s_cmdline), RK_READ);
        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)s_uart_base);
        reg_set("/hal/fdt/uart_base", val_str(buf),       RK_READ);
        reg_set("/hal/fdt/status",    val_str("ok"),       RK_READ);
    }
}
