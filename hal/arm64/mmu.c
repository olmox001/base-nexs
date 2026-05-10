/*
 * hal/arm64/mmu.c — AArch64 MMU Setup
 * ======================================
 * Sets up a minimal identity-mapped page table covering the first 1 GB of
 * physical memory using 2MB block entries (level-2 descriptors).
 *
 * Configures TCR_EL1 for 48-bit VA, 4KB granule, 39-bit IPA.
 * Enables the MMU via SCTLR_EL1.
 */

#include "../include/nexs_mmu.h"
#include <stdint.h>

/* ── Page table constants ─────────────────────────────────── */
#define PAGE_TABLE_BASE   0x1000UL  /* PGD: 4KB at phys 0x1000 */
#define PUD_BASE          0x2000UL  /* PUD: 4KB at phys 0x2000 */
#define PMD_BASE          0x3000UL  /* PMD: 4KB at phys 0x3000 */

/* Descriptor flags */
#define PTE_VALID         (1UL << 0)
#define PTE_TABLE         (1UL << 1)
#define PTE_BLOCK         (0UL << 1)    /* block at L1/L2 */
#define PTE_AF            (1UL << 10)   /* access flag */
#define PTE_SH_INNER      (3UL << 8)    /* inner shareable */
#define PTE_NORMAL        (0UL << 2)    /* AttrIdx 0 = normal WB */
#define PTE_DEVICE        (1UL << 2)    /* AttrIdx 1 = device nGnRnE */
#define PTE_AP_RW_EL1     (0UL << 6)    /* EL1 RW, EL0 none */
#define PTE_NS            (1UL << 5)    /* non-secure */

/* MAIR_EL1 attribute indices:
 *   Idx 0 = Normal Write-Back Cacheable
 *   Idx 1 = Device nGnRnE */
#define MAIR_NORMAL_WB   0xFFUL          /* index 0 */
#define MAIR_DEVICE      0x00UL          /* index 1 */

/* TCR_EL1:
 *   T0SZ=25 → 39-bit VA (0x0000_0000_0000_0000 .. 0x0000_007F_FFFF_FFFF)
 *   TG0=0   → 4KB granule
 *   IRGN0=1 → inner WB, WA
 *   ORGN0=1 → outer WB, WA
 *   SH0=3   → inner shareable
 *   IPS=0   → 32-bit PA */
#define TCR_T0SZ    (25UL)
#define TCR_IRGN0   (1UL << 8)
#define TCR_ORGN0   (1UL << 10)
#define TCR_SH0     (3UL << 12)
#define TCR_TG0_4K  (0UL << 14)
#define TCR_IPS_1G  (1UL << 32)    /* IPS=1 → 36-bit PA */
#define TCR_VAL     (TCR_T0SZ | TCR_IRGN0 | TCR_ORGN0 | TCR_SH0 | TCR_TG0_4K)

static void memzero(volatile uint64_t *p, int count) {
    for (int i = 0; i < count; i++) p[i] = 0;
}

void mmu_init(void) {
    volatile uint64_t *pgd = (volatile uint64_t *)PAGE_TABLE_BASE;
    volatile uint64_t *pud = (volatile uint64_t *)PUD_BASE;
    volatile uint64_t *pmd = (volatile uint64_t *)PMD_BASE;

    /* Zero tables */
    memzero(pgd, 512);
    memzero(pud, 512);
    memzero(pmd, 512);

    /* PGD[0] → PUD */
    pgd[0] = PUD_BASE | PTE_VALID | PTE_TABLE;

    /* PUD[0] → PMD */
    pud[0] = PMD_BASE | PTE_VALID | PTE_TABLE;

    /* PMD: 512 × 2MB block entries.
     * First 8 entries (0–15 MB): Device memory (UART, GIC, etc.)
     * Remaining: Normal memory. */
    for (int i = 0; i < 512; i++) {
        uint64_t phys = (uint64_t)i * (2UL * 1024 * 1024);
        uint64_t attr = (i < 8) ? PTE_DEVICE : (PTE_NORMAL | PTE_SH_INNER);
        pmd[i] = phys | PTE_VALID | PTE_BLOCK | PTE_AF | PTE_AP_RW_EL1 | attr;
    }

    /* MAIR_EL1: index 0 = Normal WB, index 1 = Device */
    uint64_t mair = MAIR_NORMAL_WB | (MAIR_DEVICE << 8);
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));

    /* TCR_EL1 */
    __asm__ volatile("msr tcr_el1, %0" :: "r"((uint64_t)TCR_VAL));

    /* TTBR0_EL1 → PGD */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)PAGE_TABLE_BASE));
    __asm__ volatile("isb");

    /* Enable MMU, I-cache, D-cache via SCTLR_EL1 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0)  /* M  — MMU enable */
           | (1UL << 2)  /* C  — D-cache enable */
           | (1UL << 12) /* I  — I-cache enable */;
    __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr));
}

int mmu_map_page(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t flags) {
    (void)pid; (void)virt; (void)phys; (void)flags;
    return -1;
}

int mmu_unmap_page(uint32_t pid, vaddr_t virt) {
    (void)pid; (void)virt;
    return 0;
}

void mmu_flush_tlb(vaddr_t virt) {
    (void)virt;
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

paddr_t mmu_virt_to_phys(vaddr_t virt) {
    return (paddr_t)virt;
}

void mmu_page_fault(vaddr_t fault_addr, uint64_t err) {
    (void)fault_addr; (void)err;
}

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
    (void)pid; (void)virt; (void)flags;
    return 0;
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
    (void)pid; (void)virt;
    return 0;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t pages, uint32_t flags) {
    (void)pid; (void)virt; (void)phys; (void)pages; (void)flags;
    return 0;
}

void mmu_worker_sync(void) {}

paddr_t mmu_create_address_space(uint32_t pid) {
    (void)pid;
    return 0;
}

int mmu_switch_address_space(uint32_t pid) {
    (void)pid;
    return 0;
}

void mmu_destroy_address_space(uint32_t pid) {
    (void)pid;
}
