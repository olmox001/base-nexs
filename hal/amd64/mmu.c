/*
 * hal/amd64/mmu.c — x86-64 Page Tables
 * =======================================
 * PML4 identity map + kernel high mapping.
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
#include "../include/nexs_hal.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include "../../core/include/nexs_alloc.h"
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

/* ── MMU Event Buffer (SPSC Lock-Free) ────────────────────── */
#define MMU_EV_ALLOC  1
#define MMU_EV_FREE   2
#define MMU_EV_FAULT  3
#define MMU_EV_BUF_SZ 256

typedef struct {
    uint32_t pid;
    uint32_t type;
    uint64_t virt;
    uint64_t data; /* phys addr or error code */
} MmuEvent;

static MmuEvent mmu_ev_buffer[MMU_EV_BUF_SZ];
static volatile uint32_t mmu_ev_head = 0;
static uint32_t mmu_ev_tail = 0;

static void mmu_push_event(uint32_t pid, uint32_t type, uint64_t virt, uint64_t data) {
    uint32_t next = (mmu_ev_head + 1) % MMU_EV_BUF_SZ;
    if (next == mmu_ev_tail) return; /* Buffer full, drop event (safety first) */
    
    mmu_ev_buffer[mmu_ev_head].pid  = pid;
    mmu_ev_buffer[mmu_ev_head].type = type;
    mmu_ev_buffer[mmu_ev_head].virt = virt;
    mmu_ev_buffer[mmu_ev_head].data = data;
    
    __asm__ volatile("" ::: "memory"); /* Memory barrier */
    mmu_ev_head = next;
}

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

/* ── Walk PML4 → PT read-only; returns PTE address or 0 ─────── */
static uint64_t pml4_walk_readonly(uint64_t pml4_phys, vaddr_t virt) {
    uint16_t pml4i = (uint16_t)((virt >> 39) & 0x1FF);
    uint16_t pdpti = (uint16_t)((virt >> 30) & 0x1FF);
    uint16_t pdi   = (uint16_t)((virt >> 21) & 0x1FF);
    uint16_t pti   = (uint16_t)((virt >> 12) & 0x1FF);

    volatile uint64_t *pml4e = pte_at(pml4_phys, pml4i);
    if (!(*pml4e & PTE_P)) return 0;
    uint64_t pdpt_phys = PTE_ADDR(*pml4e);

    volatile uint64_t *pdpte = pte_at(pdpt_phys, pdpti);
    if (!(*pdpte & PTE_P)) return 0;
    uint64_t pd_phys = PTE_ADDR(*pdpte);

    volatile uint64_t *pde = pte_at(pd_phys, pdi);
    if (!(*pde & PTE_P)) return 0;
    if (*pde & PTE_PS) return 0; /* 2MB huge page — can't unmap 4KB granule */
    uint64_t pt_phys = PTE_ADDR(*pde);

    return pt_phys + (uint64_t)pti * 8;
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
    uint64_t pte_addr = pml4_walk_readonly(KERNEL_PML4_PHYS, virt);
    if (!pte_addr) return 0; /* already unmapped */
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

    /* Queue fault event instead of direct registry write to avoid ISR deadlock */
    mmu_push_event(0, MMU_EV_FAULT, cr2, f->err);

    /* For kernel faults: halt */
    if (!(f->err & (1ULL << 2))) {
        nexs_hal_print("\r\n*** KERNEL PAGE FAULT ***\r\n");
        __asm__ volatile("cli; hlt");
    }
    /* User fault: kill process (future: send signal via IPC) */
}

/* ── Memory block system ──────────────────────────────────── */

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
    paddr_t phys = s_phys_bump;
    s_phys_bump += 4096;
    /* Queue allocation events */
    mmu_push_event(pid, MMU_EV_ALLOC, virt, phys);
    mmu_push_event(pid, MMU_EV_ALLOC | 0x80, virt, flags); /* encoded flags event */

    return mmu_map_page(pid, virt, phys, flags);
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
    paddr_t phys = mmu_virt_to_phys(virt);

    /* Queue free event */
    mmu_push_event(pid, MMU_EV_FREE, virt, 0);

    int res = mmu_unmap_page(pid, virt);
    if (phys != (paddr_t)-1) {
        page_free((void *)phys, 1);
    }
    return res;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys,
                 uint32_t pages, uint32_t flags) {
    for (uint32_t i = 0; i < pages; i++) {
        if (mmu_map_page(pid, virt + i * 4096, phys + i * 4096, flags) != 0)
            return -1;
    }
    return 0;
}

/* ── MMU Worker (Async Sync to Registry) ──────────────────── */
void mmu_worker_sync(void) {
    while (mmu_ev_tail != mmu_ev_head) {
        MmuEvent ev = mmu_ev_buffer[mmu_ev_tail];
        char path[128];
        char val_buf[64];

        if (ev.type == MMU_EV_FAULT) {
            snprintf(val_buf, sizeof(val_buf), "0x%llx", (unsigned long long)ev.virt);
            reg_set("/hal/mmu/last_fault_addr", val_str(val_buf), RK_READ);
            snprintf(val_buf, sizeof(val_buf), "0x%llx", (unsigned long long)ev.data);
            reg_set("/hal/mmu/last_fault_err",  val_str(val_buf), RK_READ);
        }
        else if (ev.type == MMU_EV_ALLOC) {
            snprintf(path, sizeof(path), "/mem/virt/%u/0x%llx/phys", ev.pid, (unsigned long long)ev.virt);
            reg_set(path, val_int((int64_t)ev.data), RK_READ | RK_WRITE);
        }
        else if (ev.type == (MMU_EV_ALLOC | 0x80)) {
            snprintf(path, sizeof(path), "/mem/virt/%u/0x%llx/flags", ev.pid, (unsigned long long)ev.virt);
            reg_set(path, val_int((int64_t)ev.data), RK_READ | RK_WRITE);
        }
        else if (ev.type == MMU_EV_FREE) {
            snprintf(path, sizeof(path), "/mem/virt/%u/0x%llx/phys", ev.pid, (unsigned long long)ev.virt);
            reg_delete(path);
            snprintf(path, sizeof(path), "/mem/virt/%u/0x%llx/flags", ev.pid, (unsigned long long)ev.virt);
            reg_delete(path);
        }

        mmu_ev_tail = (mmu_ev_tail + 1) % MMU_EV_BUF_SZ;
    }
}
