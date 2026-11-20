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

/* QEMU virt boots RAM at 0x40000000 while UART/GIC occupy MMIO below
 * 0x40000000. AArch64 with T0SZ=25 starts at L1 (1 GiB per entry):
 * one L1 table and one L2 table for each populated GiB segment.
 *
 * All tables live in the kernel image, in real guest RAM. The legacy absolute
 * physical addresses 0x1000/0x2000/0x3000 were outside QEMU virt RAM and
 * caused an EL1 translation fault immediately after enabling the MMU.
 */
#define NEXS_ARM64_L2_BLOCK_BYTES (2UL * 1024UL * 1024UL)
#define NEXS_ARM64_RAM_BASE       (1UL << 30)
#define NEXS_ARM64_TABLE_ENTRIES  (512U)

static uint64_t s_l1_root[NEXS_ARM64_TABLE_ENTRIES]
    __attribute__((aligned(4096)));
static uint64_t s_l2_mmio[NEXS_ARM64_TABLE_ENTRIES]
    __attribute__((aligned(4096)));
static uint64_t s_l2_ram[NEXS_ARM64_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

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

static void table_clear(uint64_t *table) {
    for (uint32_t i = 0; i < NEXS_ARM64_TABLE_ENTRIES; ++i)
        table[i] = 0;
}

static void table_map_blocks(uint64_t *table, uint64_t phys_base,
                             uint64_t attributes) {
    for (uint32_t i = 0; i < NEXS_ARM64_TABLE_ENTRIES; ++i) {
        uint64_t phys = phys_base + ((uint64_t)i * NEXS_ARM64_L2_BLOCK_BYTES);
        table[i] = phys | PTE_VALID | PTE_BLOCK | PTE_AF |
                   PTE_AP_RW_EL1 | attributes;
    }
}

void mmu_init(void) {
    table_clear(s_l1_root);
    table_clear(s_l2_mmio);
    table_clear(s_l2_ram);

    /* [0, 1 GiB): peripheral/MMIO ranges, including PL011 and GIC. */
    table_map_blocks(s_l2_mmio, 0, PTE_DEVICE);
    /* [1 GiB, 2 GiB): QEMU virt DRAM, including the entire Base-NEXS image. */
    table_map_blocks(s_l2_ram, NEXS_ARM64_RAM_BASE,
                     PTE_NORMAL | PTE_SH_INNER);

    s_l1_root[0] = (uintptr_t)s_l2_mmio | PTE_VALID | PTE_TABLE;
    s_l1_root[1] = (uintptr_t)s_l2_ram | PTE_VALID | PTE_TABLE;

    /* MAIR_EL1: index 0 normal write-back; index 1 device nGnRnE. */
    const uint64_t mair = MAIR_NORMAL_WB | (MAIR_DEVICE << 8);
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    __asm__ volatile("msr tcr_el1, %0" :: "r"((uint64_t)TCR_VAL));
    __asm__ volatile("msr ttbr0_el1, %0" ::
                     "r"((uintptr_t)s_l1_root) : "memory");
    __asm__ volatile("dsb sy; isb" ::: "memory");

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);
    __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");
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
