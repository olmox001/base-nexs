/*
 * nexs_alloc.h — NEXS Allocator API
 * ====================================
 * Buddy allocator + page allocator + unified nexs_alloc/nexs_free.
 * Also declares die() and nexs_warn().
 */

#ifndef NEXS_ALLOC_H
#define NEXS_ALLOC_H
#pragma once

#include "nexs_common.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   BUDDY ALLOCATOR STATE
   ========================================================= */

typedef enum { BNODE_FREE = 0, BNODE_SPLIT = 1, BNODE_USED = 2 } BuddyNodeState;

extern uint8_t memory_pool[POOL_SIZE];
extern uint8_t buddy_tree[TREE_NODES];

/* =========================================================
   BUDDY API
   ========================================================= */

NEXS_API size_t buddy_next_pow2(size_t x);
NEXS_API void  *buddy_alloc(size_t size);
NEXS_API void   buddy_free(void *ptr);

/* xmalloc: alloc + zero-fill; calls die() on OOM */
NEXS_API void  *xmalloc(size_t size);
/* xfree: alias of buddy_free */
NEXS_API void   xfree(void *ptr);

NEXS_API char  *buddy_strdup(const char *s);
NEXS_API void   buddy_dump_stats(FILE *out);

/* =========================================================
   PAGE ALLOCATOR API
   ========================================================= */

/* Allocate n_pages * NEXS_PAGE_SIZE bytes (mmap on hosted, bump on baremetal) */
NEXS_API void *page_alloc(size_t n_pages);

/* Free memory allocated by page_alloc */
NEXS_API void  page_free(void *ptr, size_t n_pages);

/* Returns 1 if ptr was allocated by page_alloc, 0 otherwise */
NEXS_API int   is_page_ptr(void *ptr);

/* =========================================================
   UNIFIED ALLOCATOR
   ========================================================= */

/* nexs_alloc: uses page_alloc for size > LARGE_ALLOC_THRESH, else xmalloc */
NEXS_API void *nexs_alloc(size_t size);

/* nexs_free: auto-detects page vs buddy ptr using is_page_ptr */
NEXS_API void  nexs_free(void *ptr, size_t size);

/* =========================================================
   ERROR REPORTING
   ========================================================= */

/* die: print fatal message and exit(1). Declared noreturn. */
NEXS_API void die(const char *msg) __attribute__((noreturn));

/* nexs_warn: print warning to stderr (printf-style) */
NEXS_API void nexs_warn(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_ALLOC_H */
