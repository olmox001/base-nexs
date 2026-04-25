/*
 * hal/amd64/acpi.c — ACPI RSDP → RSDT/XSDT → MADT parser
 * ==========================================================
 * STEP 04: scan 0xE0000-0xFFFFF for "RSD PTR ", parse MADT.
 * Results published to /hal/acpi/ in the registry.
 */

#include "../include/nexs_acpi.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>
#include <string.h>

/* ── ACPI structures ──────────────────────────────────────── */
typedef struct {
    char     sig[8];        /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;      /* 0=ACPI 1.0, 2=ACPI 2.0+ */
    uint32_t rsdt_addr;
    /* ACPI 2.0+ fields */
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) Rsdp;

typedef struct {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_rev;
} __attribute__((packed)) AcpiHeader;

/* MADT (Multiple APIC Description Table) */
typedef struct {
    AcpiHeader hdr;         /* sig = "APIC" */
    uint32_t   lapic_addr;
    uint32_t   flags;
} __attribute__((packed)) Madt;

/* MADT record types */
#define MADT_LAPIC   0
#define MADT_IOAPIC  1
#define MADT_ISO     2      /* Interrupt Source Override */

typedef struct { uint8_t type; uint8_t len; } __attribute__((packed)) MadtRec;

typedef struct {
    MadtRec  hdr;
    uint8_t  acpi_proc_id;
    uint8_t  apic_id;
    uint32_t flags;         /* bit 0=enabled */
} __attribute__((packed)) MadtLapic;

typedef struct {
    MadtRec  hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed)) MadtIoapic;

/* ── State ────────────────────────────────────────────────── */
static int      s_cpu_count  = 0;
static uint64_t s_ioapic_base = 0xFEC00000UL;

/* ── Checksum ─────────────────────────────────────────────── */
static int acpi_checksum(const void *p, uint32_t len) {
    uint8_t sum = 0;
    const uint8_t *b = (const uint8_t *)p;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + b[i]);
    return sum == 0;
}

/* ── Find RSDP in BIOS area ───────────────────────────────── */
static const Rsdp *find_rsdp(void) {
    /* Search EBDA first (first 1 KB at 0x40E << 4) */
    uint16_t ebda_seg = *(volatile uint16_t *)0x40E;
    uint64_t ebda = (uint64_t)ebda_seg << 4;
    for (uint64_t a = ebda; a < ebda + 1024; a += 16) {
        if (memcmp((void *)a, "RSD PTR ", 8) == 0 &&
            acpi_checksum((void *)a, 20))
            return (const Rsdp *)a;
    }
    /* BIOS ROM 0xE0000 – 0xFFFFF */
    for (uint64_t a = 0xE0000; a < 0x100000; a += 16) {
        if (memcmp((void *)a, "RSD PTR ", 8) == 0 &&
            acpi_checksum((void *)a, 20))
            return (const Rsdp *)a;
    }
    return NULL;
}

/* ── Parse MADT ───────────────────────────────────────────── */
static void parse_madt(const Madt *madt) {
    uint32_t lapic_phys = madt->lapic_addr;
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%x", lapic_phys);
    reg_set("/hal/acpi/lapic_override", val_str(buf), RK_READ);

    const uint8_t *p   = (const uint8_t *)(madt + 1);
    const uint8_t *end = (const uint8_t *)madt + madt->hdr.length;

    while (p < end) {
        const MadtRec *rec = (const MadtRec *)p;
        if (rec->len < 2) break;

        if (rec->type == MADT_LAPIC) {
            const MadtLapic *ml = (const MadtLapic *)rec;
            if (ml->flags & 1) {
                snprintf(buf, sizeof(buf), "/hal/acpi/cpu/%d/apic_id", s_cpu_count);
                reg_set(buf, val_int(ml->apic_id), RK_READ);
                s_cpu_count++;
            }
        } else if (rec->type == MADT_IOAPIC) {
            const MadtIoapic *mi = (const MadtIoapic *)rec;
            s_ioapic_base = mi->ioapic_addr;
            snprintf(buf, sizeof(buf), "0x%x", mi->ioapic_addr);
            reg_set("/hal/acpi/ioapic_base", val_str(buf), RK_READ);
            reg_set("/hal/acpi/ioapic_gsi_base", val_int(mi->gsi_base), RK_READ);
        }
        p += rec->len;
    }

    reg_set("/hal/acpi/cpu_count", val_int(s_cpu_count), RK_READ);
}

/* ── Parse SDT tables ─────────────────────────────────────── */
static void parse_sdt(const AcpiHeader *hdr) {
    if (memcmp(hdr->sig, "APIC", 4) == 0 && acpi_checksum(hdr, hdr->length))
        parse_madt((const Madt *)hdr);
}

/* ── Public API ───────────────────────────────────────────── */
int acpi_cpu_count(void)      { return s_cpu_count; }
uint64_t acpi_ioapic_base(void) { return s_ioapic_base; }

int acpi_init(void) {
    const Rsdp *rsdp = find_rsdp();
    if (!rsdp) {
        reg_set("/hal/acpi/status", val_str("no RSDP"), RK_READ);
        return -1;
    }
    reg_set("/hal/acpi/revision", val_int(rsdp->revision), RK_READ);

    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        /* XSDT: 64-bit pointers */
        const AcpiHeader *xsdt = (const AcpiHeader *)rsdp->xsdt_addr;
        if (acpi_checksum(xsdt, xsdt->length)) {
            uint32_t n = (xsdt->length - sizeof(AcpiHeader)) / 8;
            const uint64_t *ptrs = (const uint64_t *)(xsdt + 1);
            for (uint32_t i = 0; i < n; i++)
                parse_sdt((const AcpiHeader *)ptrs[i]);
        }
    } else if (rsdp->rsdt_addr) {
        /* RSDT: 32-bit pointers */
        const AcpiHeader *rsdt = (const AcpiHeader *)(uint64_t)rsdp->rsdt_addr;
        if (acpi_checksum(rsdt, rsdt->length)) {
            uint32_t n = (rsdt->length - sizeof(AcpiHeader)) / 4;
            const uint32_t *ptrs = (const uint32_t *)(rsdt + 1);
            for (uint32_t i = 0; i < n; i++)
                parse_sdt((const AcpiHeader *)(uint64_t)ptrs[i]);
        }
    }

    /* Notify APIC driver of real IOAPIC base */
    extern void apic_set_ioapic_base(uint64_t);
    apic_set_ioapic_base(s_ioapic_base);

    reg_set("/hal/acpi/status", val_str("ok"), RK_READ);
    return 0;
}
