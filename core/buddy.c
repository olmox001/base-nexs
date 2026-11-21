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
#include <string.h>
#include <stdatomic.h>

/* Allocation profiling taps the actual default Buddy and page providers.
 * Platform-independent, opt-in, no allocation or external metrics backend. */
static _Atomic uint32_t allocator_observation_enabled;
static _Atomic uint64_t observed_buddy_requests;
static _Atomic uint64_t observed_buddy_success;
static _Atomic uint64_t observed_buddy_failures;
static _Atomic uint64_t observed_buddy_free_success;
static _Atomic uint64_t observed_buddy_requested_bytes;
static _Atomic uint64_t observed_page_requests;
static _Atomic uint64_t observed_page_success;
static _Atomic uint64_t observed_page_failures;
static _Atomic uint64_t observed_page_free_success;
static _Atomic uint64_t observed_page_requested_pages;

/* Internal Page provider uses this tap; public snapshots go through the
 * common declaration in nexs_alloc.h. */
void nexs_allocator_observe(int page, size_t bytes, int success, int release) {
  if (atomic_load_explicit(&allocator_observation_enabled,
                           memory_order_relaxed) == 0U)
    return;
  if (page != 0) {
    if (release != 0) {
      if (success != 0)
        atomic_fetch_add_explicit(&observed_page_free_success, 1U,
                                  memory_order_relaxed);
      return;
    }
    atomic_fetch_add_explicit(&observed_page_requests, 1U,
                              memory_order_relaxed);
    /* On failure, requested size may equal SIZE_MAX. Only successfully
     * admitted storage contributes to the page-commitment counter. */
    if (success != 0)
      atomic_fetch_add_explicit(&observed_page_requested_pages,
                                (uint64_t)bytes, memory_order_relaxed);
    atomic_fetch_add_explicit(success != 0 ? &observed_page_success
                                           : &observed_page_failures, 1U,
                              memory_order_relaxed);
    return;
  }
  if (release != 0) {
    if (success != 0)
      atomic_fetch_add_explicit(&observed_buddy_free_success, 1U,
                                memory_order_relaxed);
    return;
  }
  atomic_fetch_add_explicit(&observed_buddy_requests, 1U,
                            memory_order_relaxed);
  if (success != 0)
    atomic_fetch_add_explicit(&observed_buddy_requested_bytes,
                              (uint64_t)bytes, memory_order_relaxed);
  atomic_fetch_add_explicit(success != 0 ? &observed_buddy_success
                                         : &observed_buddy_failures, 1U,
                            memory_order_relaxed);
}

void nexs_allocator_observation_enable(int enabled) {
  atomic_store_explicit(&allocator_observation_enabled,
                        enabled != 0 ? 1U : 0U, memory_order_release);
}

void nexs_allocator_metrics_read(NexsAllocatorMetrics *out) {
  if (out == NULL)
    return;
  out->buddy_requests = atomic_load_explicit(&observed_buddy_requests,
                                             memory_order_relaxed);
  out->buddy_success = atomic_load_explicit(&observed_buddy_success,
                                            memory_order_relaxed);
  out->buddy_failures = atomic_load_explicit(&observed_buddy_failures,
                                             memory_order_relaxed);
  out->buddy_free_success = atomic_load_explicit(&observed_buddy_free_success,
                                                 memory_order_relaxed);
  out->buddy_requested_bytes =
      atomic_load_explicit(&observed_buddy_requested_bytes,
                           memory_order_relaxed);
  out->page_requests = atomic_load_explicit(&observed_page_requests,
                                            memory_order_relaxed);
  out->page_success = atomic_load_explicit(&observed_page_success,
                                           memory_order_relaxed);
  out->page_failures = atomic_load_explicit(&observed_page_failures,
                                            memory_order_relaxed);
  out->page_free_success = atomic_load_explicit(&observed_page_free_success,
                                                memory_order_relaxed);
  out->page_requested_pages =
      atomic_load_explicit(&observed_page_requested_pages,
                           memory_order_relaxed);
}

/* =========================================================
   GLOBAL STATE
   ========================================================= */

#ifdef NEXS_SEL4
uint8_t *memory_pool = (uint8_t *)0x40000000UL;
#else
_Alignas(MIN_BLOCK) uint8_t memory_pool[POOL_SIZE];
#endif
uint8_t buddy_tree[TREE_NODES];

/* The same real algorithm serves independent caller-admitted arenas and
 * the still-legacy global default. No second registry/allocator is added.
 * RuntimeInstance is not yet migrated: caller ownership is mandatory. */
static NexsBuddyArena default_buddy;
static int default_buddy_ready = 0;

static int nexs_buddy_arena_valid(const NexsBuddyArena *arena) {
  if (arena == NULL || arena->storage == NULL || arena->nodes == NULL ||
      arena->capacity < MIN_BLOCK ||
      (arena->capacity & (arena->capacity - 1U)) != 0U ||
      MIN_BLOCK == 0U || (MIN_BLOCK & (MIN_BLOCK - 1U)) != 0U)
    return 0;
  size_t leaves = arena->capacity / MIN_BLOCK;
  if (leaves == 0U || (leaves & (leaves - 1U)) != 0U ||
      leaves > (SIZE_MAX / 2U) ||
      arena->node_count != 2U * leaves - 1U)
    return 0;
  uintptr_t mem = (uintptr_t)arena->storage;
  uintptr_t tree = (uintptr_t)arena->nodes;
  if (mem % MIN_BLOCK != 0U ||
      mem > UINTPTR_MAX - arena->capacity ||
      tree > UINTPTR_MAX - arena->node_count)
    return 0;
  uintptr_t owner = (uintptr_t)arena;
  if (owner > UINTPTR_MAX - sizeof(*arena))
    return 0;
  uintptr_t owner_end = owner + sizeof(*arena);
  int spans_separate = mem + arena->capacity <= tree ||
                       tree + arena->node_count <= mem;
  int owner_separate = (owner_end <= mem ||
                        mem + arena->capacity <= owner) &&
                       (owner_end <= tree ||
                        tree + arena->node_count <= owner);
  return spans_separate && owner_separate;
}

int nexs_buddy_arena_init(NexsBuddyArena *arena, void *storage,
                          size_t capacity, void *nodes, size_t node_bytes) {
  if (arena == NULL || storage == NULL || nodes == NULL ||
      capacity < MIN_BLOCK || (capacity & (capacity - 1U)) != 0U ||
      capacity % MIN_BLOCK != 0U || capacity / MIN_BLOCK > SIZE_MAX / 2U)
    return -1;
  size_t required = 2U * (capacity / MIN_BLOCK) - 1U;
  if (node_bytes < required)
    return -1;
  NexsBuddyArena candidate = {
    .storage = (uint8_t *)storage,
    .nodes = (uint8_t *)nodes,
    .capacity = capacity,
    .node_count = required
  };
  if (!nexs_buddy_arena_valid(&candidate))
    return -1;
  /* The destination arena pointer itself must not alias either caller-
   * provided data or tree. Checking only the temporary candidate would
   * admit a self-overwriting control structure. */
  uintptr_t owner = (uintptr_t)arena;
  uintptr_t backing = (uintptr_t)candidate.storage;
  uintptr_t metadata = (uintptr_t)candidate.nodes;
  if (owner > UINTPTR_MAX - sizeof(*arena) ||
      !((owner + sizeof(*arena) <= backing ||
         backing + candidate.capacity <= owner) &&
        (owner + sizeof(*arena) <= metadata ||
         metadata + candidate.node_count <= owner)))
    return -1;
  memset(candidate.nodes, BNODE_FREE, candidate.node_count);
  *arena = candidate;
  return 0;
}

/* This adapter preserves the preexisting exported backing arrays until
 * runtime and all raw module consumers have migrated to arena handles. */
static NexsBuddyArena *nexs_default_buddy_arena(void) {
  if (!default_buddy_ready) {
    default_buddy.storage = memory_pool;
    default_buddy.nodes = buddy_tree;
    default_buddy.capacity = POOL_SIZE;
    default_buddy.node_count = TREE_NODES;
    default_buddy_ready = 1;
  }
  return &default_buddy;
}

static size_t nexs_arena_next_pow2(size_t requested, size_t capacity) {
  if (requested > capacity)
    return 0U;
  size_t rounded = MIN_BLOCK;
  for (size_t depth = 0U;
       rounded < requested && depth < sizeof(size_t) * 8U; ++depth) {
    if (rounded > SIZE_MAX / 2U)
      return 0U;
    rounded *= 2U;
  }
  return rounded < requested ? 0U : rounded;
}

size_t buddy_next_pow2(size_t x) {
  if (!nexs_buddy_arena_valid(nexs_default_buddy_arena()))
    return 0U;
  return nexs_arena_next_pow2(x, POOL_SIZE);
}

/* Bounded implicit-tree preorder; no recursion, temporary DFS stack, or
 * per-instance hidden name registry. Fully used subtrees are skipped. */
static int nexs_buddy_next_search_node(
    const NexsBuddyArena *arena, size_t *node, size_t *span,
    size_t *offset) {
  for (size_t depth = 0U; depth < sizeof(size_t) * 8U; ++depth) {
    if (*node == 0U)
      return 0;
    if ((*node & 1U) != 0U) {
      ++*node;
      *offset += *span;
      return *node < arena->node_count;
    }
    *node = (*node - 2U) / 2U;
    *offset -= *span;
    *span *= 2U;
  }
  return 0;
}

static int nexs_buddy_alloc_node(NexsBuddyArena *arena, size_t wanted,
                                 size_t *out_offset) {
  size_t node = 0U;
  size_t span = arena->capacity;
  size_t offset = 0U;
  for (size_t visited = 0U; visited < arena->node_count; ++visited) {
    if (node >= arena->node_count)
      return 0;
    if (span == wanted && arena->nodes[node] == BNODE_FREE) {
      arena->nodes[node] = BNODE_USED;
      *out_offset = offset;
      return 1;
    }
    if (span > wanted && (arena->nodes[node] == BNODE_FREE ||
                          arena->nodes[node] == BNODE_SPLIT)) {
      if (arena->nodes[node] == BNODE_FREE)
        arena->nodes[node] = BNODE_SPLIT;
      span /= 2U;
      node = 2U * node + 1U;
    } else if (!nexs_buddy_next_search_node(
                   arena, &node, &span, &offset)) {
      return 0;
    }
  }
  return 0;
}

void *nexs_buddy_arena_alloc(NexsBuddyArena *arena, size_t size) {
  if (!nexs_buddy_arena_valid(arena) ||
      size == 0U || size > arena->capacity)
    return NULL;
  size_t needed = nexs_arena_next_pow2(size, arena->capacity);
  size_t offset = 0U;
  if (needed != 0U && nexs_buddy_alloc_node(arena, needed, &offset))
    return arena->storage + offset;
  return NULL;
}

void *buddy_alloc(size_t size) {
  void *out = nexs_buddy_arena_alloc(nexs_default_buddy_arena(), size);
  nexs_allocator_observe(0, size, out != NULL, 0);
  return out;
}

/* Exact owned-base reclamation with bounded bottom-up sibling coalescence. */
static int nexs_buddy_free_node(NexsBuddyArena *arena, size_t target) {
  size_t node = 0U;
  size_t base = 0U;
  size_t span = arena->capacity;
  for (size_t depth = 0U; depth < sizeof(size_t) * 8U; ++depth) {
    if (node >= arena->node_count)
      return -1;
    if (arena->nodes[node] == BNODE_USED) {
      if (base != target)
        return -1;
      arena->nodes[node] = BNODE_FREE;
      for (size_t up = 0U; node != 0U && up < depth; ++up) {
        size_t parent = (node - 1U) / 2U;
        size_t left = 2U * parent + 1U;
        size_t right = left + 1U;
        if (right >= arena->node_count ||
            arena->nodes[left] != BNODE_FREE ||
            arena->nodes[right] != BNODE_FREE)
          return 0;
        arena->nodes[parent] = BNODE_FREE;
        node = parent;
      }
      return 0;
    }
    if (arena->nodes[node] != BNODE_SPLIT || span <= MIN_BLOCK)
      return -1;
    span /= 2U;
    if (target >= base + span) {
      base += span;
      node = 2U * node + 2U;
    } else {
      node = 2U * node + 1U;
    }
  }
  return -1;
}

int nexs_buddy_arena_free(NexsBuddyArena *arena, void *ptr) {
  if (!nexs_buddy_arena_valid(arena) || ptr == NULL)
    return -1;
  uintptr_t address = (uintptr_t)ptr;
  uintptr_t begin = (uintptr_t)arena->storage;
  if (address < begin || address - begin >= arena->capacity)
    return -1;
  return nexs_buddy_free_node(arena, (size_t)(address - begin));
}

void buddy_free(void *ptr) {
  int status = nexs_buddy_arena_free(nexs_default_buddy_arena(), ptr);
  nexs_allocator_observe(0, 0U, status == 0, 1);
}

/* =========================================================
   Recoverable diagnostics (no allocator-fatal shutdown path)
   ========================================================= */

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
   Fallible Buddy allocation / exact release helpers
   ========================================================= */

void xfree(void *ptr) { buddy_free(ptr); }

char *buddy_strdup(const char *s) {
  if (s == NULL)
    return NULL;
  size_t size = strlen(s);
  if (size == SIZE_MAX)
    return NULL;
  ++size;
  char *copy = buddy_alloc(size);
  if (copy == NULL)
    return NULL;
  memcpy(copy, s, size);
  return copy;
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
