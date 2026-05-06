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
paddr_t mmu_create_address_space(uint32_t pid);
int     mmu_switch_address_space(uint32_t pid);
void    mmu_destroy_address_space(uint32_t pid);

paddr_t mmu_virt_to_phys(vaddr_t virt);

/* Page fault handler — called from IDT vector 14 */
void    mmu_page_fault(vaddr_t fault_addr, uint64_t err);

/* Memory block system (scaled to POOL_SIZE) */
#define MEM_BLOCK_FAST_SIZE   (POOL_SIZE / 4)   /* 25% of pool for fast blocks */
#define MEM_BLOCK_IO_SIZE     (POOL_SIZE / 2)   /* 50% of pool for I/O buffer  */

int     mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags);
int     mm_free_page(uint32_t pid, vaddr_t virt);
int     mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys,
                     uint32_t pages, uint32_t flags);
void    mmu_worker_sync(void);

#endif
