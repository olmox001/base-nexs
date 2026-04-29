/*
 * hal/amd64/mmu.c — x86-64 Page Tables
 * =======================================
 * STEP 03: PML4 identity map + kernel high mapping.
 *
 * Layout:
 *   PML4  at KERNEL_PML4_PHYS (0x1000)  — shared kernel PML4
 *   PDPT  at 0x2000  — identity map low 1 GB (0→1 GB)
 *   PD    at 0x3000  — 512 × 2 MB huge pages
 *   PDPT  at 0x4000  — kernel high (0xFFFFFFFF80000000 → same phys)
 *
 * mmu_map_page() allocates PT entries from a simple bump allocator
 * seeded at PHYS_ALLOC_START; each process will later get its own PML4.
 */

#include "../include/nexs_mmu.h"
#include "../include/nexs_idt.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>
#include <string.h>

/* ── Physical address layout ──────────────────────────────── */
#define KERNEL_PML4_PHYS   0x1000ULL
#define PDPT_LOW_PHYS      0x2000ULL
#define PD_LOW_PHYS        0x3000ULL
#define PDPT_HIGH_PHYS     0x4000ULL
#define PD_HIGH_PHYS       0x5000ULL
/* Bump allocator for new page tables */
#define PHYS_ALLOC_START   0x10000ULL
static uint64_t s_phys_bump = PHYS_ALLOC_START;

/* ── Page table entry flags ───────────────────────────────── */
#define PTE_P    (1ULL << 0)   /* present */
#define PTE_W    (1ULL << 1)   /* writable */
#define PTE_U    (1ULL << 2)   /* user */
#define PTE_PS   (1ULL << 7)   /* page size (2MB) */
#define PTE_NX   (1ULL << 63)  /* no-execute */
#define PTE_ADDR(e) ((e) & 0x000FFFFFFFFFF000ULL)

/* ── Helpers ──────────────────────────────────────────────── */
static volatile uint64_t *pte_at(uint64_t phys_base, uint16_t idx) {
    return (volatile uint64_t *)(phys_base + (uint64_t)idx * 8);
}

static uint64_t phys_alloc_page(void) {
    uint64_t p = s_phys_bump;
    s_phys_bump += 4096;
    /* Zero the new page */
    memset((void *)p, 0, 4096);
    return p;
}

/* ── Walk / allocate PML4 → PDPT → PD → PT ──────────────── */
static uint64_t pml4_walk_alloc(uint64_t pml4_phys, vaddr_t virt) {
    uint16_t pml4i = (uint16_t)((virt >> 39) & 0x1FF);
    uint16_t pdpti = (uint16_t)((virt >> 30) & 0x1FF);
    uint16_t pdi   = (uint16_t)((virt >> 21) & 0x1FF);
    uint16_t pti   = (uint16_t)((virt >> 12) & 0x1FF);

    /* PML4 → PDPT */
    volatile uint64_t *pml4e = pte_at(pml4_phys, pml4i);
    if (!(*pml4e & PTE_P)) {
        uint64_t pdpt = phys_alloc_page();
        *pml4e = pdpt | PTE_P | PTE_W;
    }
    uint64_t pdpt_phys = PTE_ADDR(*pml4e);

    /* PDPT → PD */
    volatile uint64_t *pdpte = pte_at(pdpt_phys, pdpti);
    if (!(*pdpte & PTE_P)) {
        uint64_t pd = phys_alloc_page();
        *pdpte = pd | PTE_P | PTE_W;
    }
    uint64_t pd_phys = PTE_ADDR(*pdpte);

    /* PD → PT */
    volatile uint64_t *pde = pte_at(pd_phys, pdi);
    if (!(*pde & PTE_P)) {
        uint64_t pt = phys_alloc_page();
        *pde = pt | PTE_P | PTE_W;
    }
    /* Must not be a 2MB entry */
    if (*pde & PTE_PS) return 0; /* already huge-mapped */
    uint64_t pt_phys = PTE_ADDR(*pde);

    return pt_phys + (uint64_t)pti * 8; /* address of final PTE */
}

/* ── Init: identity map + kernel high map ─────────────────── */
void mmu_init(void) {
    /* The boot.S already created a working identity map via 2MB pages.
     * Here we refine it: set up /hal/mmu/ registry entries and the
     * page fault handler, then flush to ensure consistency. */

    /* Register page fault handler */
    extern void nexs_isr_register(uint8_t, void (*)(IsrFrame *));
    void pf_isr(IsrFrame *f);
    nexs_isr_register(14, pf_isr);

    /* Publish map to registry */
    reg_set("/hal/mmu/pml4_phys", val_int((int64_t)KERNEL_PML4_PHYS), RK_READ);
    reg_set("/hal/mmu/identity",  val_str("0→1GB@2MB"), RK_READ);
    reg_set("/hal/mmu/status",    val_str("ok"), RK_READ);

    __asm__ volatile("invlpg (%%rax)" :: "a"(0UL));
}

/* ── Map a single 4KB page ────────────────────────────────── */
int mmu_map_page(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t flags) {
    (void)pid; /* kernel map only for now */
    uint64_t pte_addr = pml4_walk_alloc(KERNEL_PML4_PHYS, virt);
    if (!pte_addr) return -1;

    uint64_t entry = (phys & ~0xFFFULL) | PTE_P;
    if (flags & MMU_WRITE)   entry |= PTE_W;
    if (flags & MMU_USER)    entry |= PTE_U;
    if (!(flags & MMU_EXEC)) entry |= PTE_NX;
    if (flags & MMU_NOCACHE) entry |= (1ULL << 4) | (1ULL << 3); /* PCD | PWT */

    *(volatile uint64_t *)pte_addr = entry;
    mmu_flush_tlb(virt);
    return 0;
}

int mmu_unmap_page(uint32_t pid, vaddr_t virt) {
    (void)pid;
    uint64_t pte_addr = pml4_walk_alloc(KERNEL_PML4_PHYS, virt);
    if (!pte_addr) return -1;
    *(volatile uint64_t *)pte_addr = 0;
    mmu_flush_tlb(virt);
    return 0;
}

void mmu_flush_tlb(vaddr_t virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

paddr_t mmu_virt_to_phys(vaddr_t virt) {
    /* Walk the live PML4 in CR3 */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t pml4_phys = cr3 & ~0xFFFULL;

    uint16_t pml4i = (uint16_t)((virt >> 39) & 0x1FF);
    uint16_t pdpti = (uint16_t)((virt >> 30) & 0x1FF);
    uint16_t pdi   = (uint16_t)((virt >> 21) & 0x1FF);
    uint16_t pti   = (uint16_t)((virt >> 12) & 0x1FF);

    uint64_t pml4e = *pte_at(pml4_phys, pml4i);
    if (!(pml4e & PTE_P)) return (paddr_t)-1;
    uint64_t pdpte = *pte_at(PTE_ADDR(pml4e), pdpti);
    if (!(pdpte & PTE_P)) return (paddr_t)-1;
    if (pdpte & PTE_PS)   return (PTE_ADDR(pdpte) & ~((1ULL<<30)-1)) | (virt & ((1ULL<<30)-1));
    uint64_t pde = *pte_at(PTE_ADDR(pdpte), pdi);
    if (!(pde & PTE_P)) return (paddr_t)-1;
    if (pde & PTE_PS)   return (PTE_ADDR(pde) & ~((1ULL<<21)-1)) | (virt & ((1ULL<<21)-1));
    uint64_t pte = *pte_at(PTE_ADDR(pde), pti);
    if (!(pte & PTE_P)) return (paddr_t)-1;
    return PTE_ADDR(pte) | (virt & 0xFFF);
}

/* ── Page fault handler ───────────────────────────────────── */
void pf_isr(IsrFrame *f) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    /* Publish fault info to registry */
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)cr2);
    reg_set("/hal/mmu/last_fault_addr", val_str(buf), RK_READ);
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)f->err);
    reg_set("/hal/mmu/last_fault_err",  val_str(buf), RK_READ);

    /* For kernel faults: halt */
    if (!(f->err & (1ULL << 2))) {
        extern void nexs_hal_print(const char *);
        nexs_hal_print("\r\n*** KERNEL PAGE FAULT ***\r\n");
        __asm__ volatile("cli; hlt");
    }
    /* User fault: kill process (future: send signal via IPC) */
}
