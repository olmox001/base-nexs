/*
 * core/buddy.c — Buddy Allocator Implementation
 * ================================================
 * Pool: 4 MB, MIN_BLOCK=32 bytes, power-of-2 alignment.
 */

#include "include/nexs_alloc.h"
#include "include/nexs_common.h"
#include "include/nexs_utils.h"

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
  size_t offset = (uint8_t *)ptr - memory_pool;
  if (offset >= POOL_SIZE)
    return;
  buddy_free_node(0, POOL_SIZE, 0, offset);
}

/* =========================================================
   DIE / WARN
   ========================================================= */

void die(const char *msg) {
  fprintf(stderr, "\033[1;31m[NEXS FATAL]\033[0m %s\n", msg);
  exit(EXIT_FAILURE);
}

void nexs_warn(const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "\033[1;33m[NEXS WARN]\033[0m ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
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
  fprintf(out, "[Buddy] free=%zu used=%zu split=%zu pool=%dKB\n",
          free_blocks, used_blocks, split_blocks, POOL_SIZE / 1024);
}
