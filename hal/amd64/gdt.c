/*
 * hal/amd64/gdt.c — x86-64 GDT + TSS
 * =====================================
 * 6 entries:
 *   0x00  null
 *   0x08  kernel code  (64-bit, DPL=0)
 *   0x10  kernel data  (DPL=0)
 *   0x18  user code    (64-bit, DPL=3)
 *   0x20  user data    (DPL=3)
 *   0x28  TSS          (2×8-byte = 16 bytes)
 *
 * TSS carries RSP0 — the ring-0 stack used on interrupt from user mode.
 */

#include "../include/nexs_idt.h"
#include <stdint.h>
#include <string.h>

/* ── GDT entry (8 bytes) ─────────────────────────────────── */
typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;      /* P | DPL | S | Type */
    uint8_t  flags_lim;   /* G | DB | L | AVL | limit[19:16] */
    uint8_t  base_hi;
} __attribute__((packed)) GdtEntry;

/* ── TSS (minimal 64-bit, 104 bytes) ─────────────────────── */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;        /* ring-0 stack */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];      /* IST1-IST7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) Tss64;

/* ── TSS system descriptor (16 bytes in GDT) ─────────────── */
typedef struct {
    uint16_t limit_lo;
    uint16_t base_0_15;
    uint8_t  base_16_23;
    uint8_t  access;      /* 0x89 = present, type=TSS available */
    uint8_t  flags_lim;
    uint8_t  base_24_31;
    uint32_t base_32_63;
    uint32_t reserved;
} __attribute__((packed)) TssDesc;

/* ── GDTR ─────────────────────────────────────────────────── */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) Gdtr;

/* ── Storage ──────────────────────────────────────────────── */
static Tss64    s_tss;

/* All GDT + TSS descriptor in one flat array for lgdt */
static struct {
    GdtEntry entries[5];
    TssDesc  tss;
} __attribute__((packed)) s_gdt_full;

static Gdtr s_gdtr;

static GdtEntry make_entry(uint8_t access, uint8_t flags) {
    GdtEntry e = {0};
    e.limit_lo  = 0xFFFF;
    e.access    = access;
    e.flags_lim = flags | 0x0F;  /* limit[19:16] = 0xF */
    return e;
}

void gdt_init(void) {
    memset(&s_gdt_full, 0, sizeof(s_gdt_full));
    memset(&s_tss, 0, sizeof(s_tss));
    s_tss.iomap_base = sizeof(Tss64);

    /* 0x00 null */
    /* already zeroed */

    /* 0x08 kernel code: P=1 DPL=0 S=1 Type=A|R|X=0x9A, L=1, G=1 */
    s_gdt_full.entries[1] = make_entry(0x9A, 0xA0);

    /* 0x10 kernel data: P=1 DPL=0 S=1 Type=A|W=0x92, DB=1, G=1 */
    s_gdt_full.entries[2] = make_entry(0x92, 0xC0);

    /* 0x18 user code:  P=1 DPL=3 S=1 Type=0x9A → 0xFA, L=1, G=1 */
    s_gdt_full.entries[3] = make_entry(0xFA, 0xA0);

    /* 0x20 user data:  P=1 DPL=3 S=1 Type=0x92 → 0xF2, DB=1, G=1 */
    s_gdt_full.entries[4] = make_entry(0xF2, 0xC0);

    /* TSS descriptor at 0x28 */
    uint64_t tss_base = (uint64_t)&s_tss;
    uint32_t tss_lim  = (uint32_t)(sizeof(Tss64) - 1);
    s_gdt_full.tss.limit_lo   = (uint16_t)(tss_lim & 0xFFFF);
    s_gdt_full.tss.base_0_15  = (uint16_t)(tss_base & 0xFFFF);
    s_gdt_full.tss.base_16_23 = (uint8_t)((tss_base >> 16) & 0xFF);
    s_gdt_full.tss.access     = 0x89; /* present, 64-bit TSS available */
    s_gdt_full.tss.flags_lim  = (uint8_t)((tss_lim >> 16) & 0x0F);
    s_gdt_full.tss.base_24_31 = (uint8_t)((tss_base >> 24) & 0xFF);
    s_gdt_full.tss.base_32_63 = (uint32_t)(tss_base >> 32);

    s_gdtr.limit = (uint16_t)(sizeof(s_gdt_full) - 1);
    s_gdtr.base  = (uint64_t)&s_gdt_full;

    __asm__ volatile(
        "lgdt %0\n\t"
        /* Reload CS via far return */
        "pushq %1\n\t"
        "lea  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        /* Reload segment registers */
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        /* Load TSS */
        "movw %3, %%ax\n\t"
        "ltr  %%ax\n\t"
        :
        : "m"(s_gdtr),
          "i"((uint64_t)GDT_KERN_CODE),
          "i"((uint16_t)GDT_KERN_DATA),
          "i"((uint16_t)GDT_TSS_LOW)
        : "rax", "memory"
    );
}

/* Set RSP0 in TSS (called by scheduler on context switch) */
void gdt_set_rsp0(uint64_t rsp0) {
    s_tss.rsp0 = rsp0;
}
