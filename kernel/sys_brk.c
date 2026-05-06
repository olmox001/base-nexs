/*
 * kernel/sys_brk.c — Heap Management Syscall
 * ===========================================
 */

#include "include/nexs_proc.h"
#include "../hal/include/nexs_mmu.h"
#include "../core/include/nexs_alloc.h"
#include <stdint.h>
#include <string.h>

/* sys_brk — expands or shrinks the heap of the current process.
 * new_brk is the requested end address of the heap.
 * If new_brk is 0, it returns the current heap_end.
 */
uint64_t nexs_brk(uint64_t new_brk) {
    NexsProc *p = proc_current();
    if (!p) return 0;

    /* Initialize heap if not already done (first call) */
    if (p->heap_start == 0) {
#ifndef NEXS_HEAP_BASE
#define NEXS_HEAP_BASE 0x40000000ULL
#endif
        p->heap_start = NEXS_HEAP_BASE;
        p->heap_end   = p->heap_start;
    }

    if (new_brk == 0) return p->heap_end;

    /* Alignment to 4KB page */
    new_brk = (new_brk + 4095) & ~4095ULL;

    if (new_brk < p->heap_start) return p->heap_end; /* invalid */

    if (new_brk > p->heap_end) {
        /* Expand heap */
        for (uint64_t v = p->heap_end; v < new_brk; v += 4096) {
            void *phys = page_alloc(1);
            if (!phys) break; /* Out of memory */
            memset(phys, 0, 4096);
            mmu_map_page(p->pid, v, (uint64_t)phys, MMU_WRITE | MMU_USER);
            p->heap_end = v + 4096;
        }
    } else if (new_brk < p->heap_end) {
        /* Shrink heap */
        for (uint64_t v = new_brk; v < p->heap_end; v += 4096) {
            /* Deallocate page and update registry for inspectability */
            mm_free_page(p->pid, v);
        }
        p->heap_end = new_brk;
    }

    return p->heap_end;
}
