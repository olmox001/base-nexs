/*
 * hal/include/nexs_mmu.h — MMU / page table API
 */
#ifndef NEXS_MMU_H
#define NEXS_MMU_H
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t pfn_t;
typedef uint64_t vaddr_t;
typedef uint64_t paddr_t;

/* Page flags (arch-agnostic) */
#define MMU_PRESENT  (1 << 0)
#define MMU_WRITE    (1 << 1)
#define MMU_USER     (1 << 2)
#define MMU_EXEC     (1 << 3)
#define MMU_NOCACHE  (1 << 4)

void    mmu_init(void);
int     mmu_map_page(uint32_t pid, vaddr_t virt, paddr_t phys, uint32_t flags);
int     mmu_unmap_page(uint32_t pid, vaddr_t virt);
void    mmu_flush_tlb(vaddr_t virt);
paddr_t mmu_virt_to_phys(vaddr_t virt);

/* Page fault handler — called from IDT vector 14 */
void    mmu_page_fault(vaddr_t fault_addr, uint64_t err);

#endif
