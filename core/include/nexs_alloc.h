/*
 * nexs_alloc.h — NEXS Allocator API
 * ====================================
 * Buddy allocator + page allocator + unified nexs_alloc/nexs_free.
 * Diagnostic formatting is recoverable; no allocator panic API is exported.
 */

#ifndef NEXS_ALLOC_H
#define NEXS_ALLOC_H
#pragma once

#include "nexs_common.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#ifndef NEXS_BAREMETAL
#include <stdio.h>
#else
typedef struct FILE FILE;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   BUDDY ALLOCATOR STATE
   ========================================================= */

typedef enum { BNODE_FREE = 0, BNODE_SPLIT = 1, BNODE_USED = 2 } BuddyNodeState;

/* ONE Buddy algorithm, owned by a caller-admitted domain. The provider
 * supplies storage and tree metadata with no hidden allocation. Multiple
 * instances have independent lifetimes; this API does not claim concurrent
 * mutation safety, generation/lease authority or hardware page mapping. */
typedef struct {
  uint8_t *storage;
  uint8_t *nodes;
  size_t capacity;
  size_t node_count;
} NexsBuddyArena;

/* Initializes an explicitly provided power-of-two domain; no malloc,
 * no global table, no capacity profile and no production assert trap. */
NEXS_API int nexs_buddy_arena_init(NexsBuddyArena *arena, void *storage,
                                  size_t capacity, void *nodes,
                                  size_t node_bytes);
/* NULL on exhausted or malformed domain, not fatal. The block may contain
 * old provider contents; callers requiring zero-fill must initialize it. */
NEXS_API void *nexs_buddy_arena_alloc(NexsBuddyArena *arena, size_t size);
/* 0 on exact live base free; -1 on cross-arena, interior, stale or malformed. */
NEXS_API int nexs_buddy_arena_free(NexsBuddyArena *arena, void *ptr);


#ifdef NEXS_SEL4
extern uint8_t *memory_pool;
#else
extern uint8_t memory_pool[POOL_SIZE];
#endif
extern uint8_t buddy_tree[TREE_NODES];

/* =========================================================
   BUDDY API
   ========================================================= */

NEXS_API size_t buddy_next_pow2(size_t x);
NEXS_API void  *buddy_alloc(size_t size);
NEXS_API void   buddy_free(void *ptr);

/* xfree: exact Buddy-owned pointer release */
NEXS_API void   xfree(void *ptr);

/* Fallible copy in the selected Buddy arena: NULL on OOM/overflow. */
NEXS_API char  *buddy_strdup(const char *s);
NEXS_API void   buddy_dump_stats(FILE *out);

/* Optional observation of the actual production memory providers.
 * Disabled by default; never authorizes access or changes allocation.
 * Counters are monotonic and include both success and explicit failures.
 * Observed paths: default Buddy allocations/free and page_alloc/page_free.
 * Caller-owned Buddy arenas have separate ownership and are not included.
 * Snapshot consists of independent relaxed loads: it is suitable for one
 * benchmarking thread, not an atomic concurrent multi-field snapshot. */
typedef struct NexsAllocatorMetrics {
  uint64_t buddy_requests;
  uint64_t buddy_success;
  uint64_t buddy_failures;
  uint64_t buddy_free_success;
  uint64_t buddy_requested_bytes;  /* admitted bytes only */
  uint64_t page_requests;
  uint64_t page_success;
  uint64_t page_failures;
  uint64_t page_free_success;
  uint64_t page_requested_pages; /* admitted pages only */
} NexsAllocatorMetrics;
NEXS_API void nexs_allocator_observation_enable(int enabled);
NEXS_API void nexs_allocator_metrics_read(NexsAllocatorMetrics *snapshot);

/* =========================================================
   PAGE ALLOCATOR API (Bitmap-hardened)
   ========================================================= */

/* Allocate n_pages * NEXS_PAGE_SIZE from the selected provider.
 * Hosted mmap returns virtual memory only, never physical-frame authority.
 * Freestanding uses its existing bitmap until provider admission is wired. */
NEXS_API void *page_alloc(size_t n_pages);

/* Free memory and release physical frames to the bitmap. */
NEXS_API void  page_free(void *ptr, size_t n_pages);

/* Exact checked page-span release; 0 success, -1 wrong pointer/count or
 * provider failure. Legacy void wrapper discards status. */
NEXS_API int    page_free_checked(void *ptr, size_t n_pages);

/* Returns 1 if ptr was allocated by page_alloc, 0 otherwise */
NEXS_API int   is_page_ptr(void *ptr);

/* =========================================================
   UNIFIED ALLOCATOR
   ========================================================= */

/* nexs_alloc: recoverable NULL on provider exhaustion/overflow;
 * uses the single existing selected Buddy/page providers. */
NEXS_API void *nexs_alloc(size_t size);

/* nexs_free: auto-detects page vs buddy ptr using is_page_ptr */
NEXS_API void  nexs_free(void *ptr, size_t size);

/* =========================================================
   ERROR REPORTING
   ========================================================= */

/* nexs_warn: print warning to stderr (printf-style) */
NEXS_API void nexs_warn(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* NEXS_ALLOC_H */
