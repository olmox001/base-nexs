/*
 * core/buddy.c — Buddy Allocator Implementation
 * ================================================
 * Pool: 4 MB, MIN_BLOCK=32 bytes, power-of-2 alignment.
 */

#include "include/nexs_alloc.h"
#include "include/nexs_common.h"
#include "include/nexs_utils.h"

#ifdef NEXS_BAREMETAL
#include "../hal/include/nexs_hal.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

uint8_t memory_pool[POOL_SIZE];
uint8_t buddy_tree[TREE_NODES];

/* =========================================================
   HARDEN MEMORY SAFETY (C11 Static Assertions)
   ========================================================= */

/* Verifica che POOL_SIZE sia potenza di 2 */
_Static_assert((POOL_SIZE & (POOL_SIZE - 1)) == 0, "Buddy Allocator: POOL_SIZE must be a power of 2");

/* Verifica che MIN_BLOCK sia potenza di 2 */
_Static_assert((MIN_BLOCK & (MIN_BLOCK - 1)) == 0, "Buddy Allocator: MIN_BLOCK must be a power of 2");

/* Verifica che il numero di foglie sia potenza di 2 per un albero bilanciato */
_Static_assert(((POOL_SIZE / MIN_BLOCK) & ((POOL_SIZE / MIN_BLOCK) - 1)) == 0, 
               "Buddy Allocator: POOL_SIZE / MIN_BLOCK must be a power of 2");

/* Validazione dimensione albero */
_Static_assert(TREE_NODES == (2 * (POOL_SIZE / MIN_BLOCK) - 1), 
               "Buddy Allocator: TREE_NODES mismatch with POOL/MIN_BLOCK formula");

/* =========================================================
   BUDDY ALLOCATOR
   ========================================================= */

size_t buddy_next_pow2(size_t x) {
  size_t p = MIN_BLOCK;
  while (p < x)
    p <<= 1;
  return p;
}

static int buddy_alloc_node(size_t node, size_t node_size, size_t node_offset,
                             size_t req_size, size_t *out_offset) {
  if (node >= TREE_NODES)
    return 0;
  if (buddy_tree[node] == BNODE_USED)
    return 0;
  if (node_size < req_size)
    return 0;

  if (node_size == req_size && buddy_tree[node] == BNODE_FREE) {
    buddy_tree[node] = BNODE_USED;
    *out_offset = node_offset;
    return 1;
  }
  if (buddy_tree[node] == BNODE_FREE) {
    if (node_size <= MIN_BLOCK)
      return 0;
    buddy_tree[node] = BNODE_SPLIT;
  }
  size_t cs = node_size / 2;
  if (buddy_alloc_node(2 * node + 1, cs, node_offset, req_size, out_offset))
    return 1;
  if (buddy_alloc_node(2 * node + 2, cs, node_offset + cs, req_size, out_offset))
    return 1;
  return 0;
}

void *buddy_alloc(size_t size) {
  if (size == 0 || size > POOL_SIZE)
    return NULL;
  size_t req = buddy_next_pow2(size);
  size_t offset = 0;
  if (buddy_alloc_node(0, POOL_SIZE, 0, req, &offset))
    return memory_pool + offset;
  return NULL;
}

static void buddy_free_node(size_t node, size_t node_size, size_t node_offset,
                             size_t target_offset) {
  if (node >= TREE_NODES)
    return;
  if (buddy_tree[node] == BNODE_FREE)
    return;

  if (buddy_tree[node] == BNODE_USED) {
    if (node_offset == target_offset)
      buddy_tree[node] = BNODE_FREE;
    return;
  }
  size_t cs = node_size / 2;
  if (target_offset < node_offset + cs)
    buddy_free_node(2 * node + 1, cs, node_offset, target_offset);
  else
    buddy_free_node(2 * node + 2, cs, node_offset + cs, target_offset);

  if (buddy_tree[node] == BNODE_SPLIT) {
    if ((2 * node + 2) < TREE_NODES &&
        buddy_tree[2 * node + 1] == BNODE_FREE &&
        buddy_tree[2 * node + 2] == BNODE_FREE)
      buddy_tree[node] = BNODE_FREE;
  }
}

void buddy_free(void *ptr) {
  if (!ptr)
    return;
  /* Guard: ensure pointer is within our memory pool boundaries.
   * Use uintptr_t to avoid unsigned underflow if ptr < memory_pool. */
  uintptr_t p   = (uintptr_t)ptr;
  uintptr_t lo  = (uintptr_t)memory_pool;
  uintptr_t hi  = lo + POOL_SIZE;
  if (p < lo || p >= hi)
    return;
  size_t offset = (size_t)(p - lo);
  buddy_free_node(0, POOL_SIZE, 0, offset);
}

/* =========================================================
   DIE / WARN
   ========================================================= */

void die(const char *msg) {
  nexs_fprintf(NULL, "\033[1;31m[NEXS FATAL]\033[0m %s\n", msg);
#ifdef NEXS_BAREMETAL
  nexs_hal_halt();
#else
  exit(EXIT_FAILURE);
#endif
}

void nexs_warn(const char *fmt, ...) {
  va_list ap;
  nexs_fprintf(NULL, "\033[1;33m[NEXS WARN]\033[0m ");
  va_start(ap, fmt);
  /* nexs_fprintf doesn't have a va_list version yet, but we can just use the buffer trick or add one.
   * For now, let's just use it as is or implement a vsnprintf version.
   */
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  nexs_fprintf(NULL, "%s\n", buf);
  va_end(ap);
}

/* =========================================================
   xmalloc / xfree / buddy_strdup
   ========================================================= */

void *xmalloc(size_t size) {
  if (size == 0)
    size = 1;
  void *p = buddy_alloc(size);
  if (!p)
    die("Buddy alloc failed (pool exhausted or fragmented)");
  memset(p, 0, size);
  return p;
}

void xfree(void *ptr) { buddy_free(ptr); }

char *buddy_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = xmalloc(n);
  memcpy(d, s, n);
  return d;
}

void buddy_dump_stats(FILE *out) {
  size_t free_blocks = 0, used_blocks = 0, split_blocks = 0;
  for (size_t i = 0; i < TREE_NODES; i++) {
    if (buddy_tree[i] == BNODE_FREE)
      free_blocks++;
    else if (buddy_tree[i] == BNODE_USED)
      used_blocks++;
    else
      split_blocks++;
  }
  nexs_fprintf(out, "[Buddy] free=%d used=%d split=%d pool=%dKB\n",
          (int)free_blocks, (int)used_blocks, (int)split_blocks, (int)(POOL_SIZE / 1024));
}
