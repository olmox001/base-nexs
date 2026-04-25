/*
 * core/pager.c — Page Allocator + Unified nexs_alloc / nexs_free
 * =================================================================
 * On hosted (POSIX) systems uses mmap/munmap.
 * On bare-metal (NEXS_BAREMETAL) uses a static bump allocator.
 */

#include "include/nexs_alloc.h"
#include "include/nexs_common.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* =========================================================
   PAGE SLOT TABLE (tracks allocated regions for is_page_ptr)
   ========================================================= */

typedef struct {
  void  *base;
  size_t pages;
  int    used;
} PageSlot;

static PageSlot page_table[MAX_PAGE_ALLOCS];

static int page_slot_find_free(void) {
  for (int i = 0; i < MAX_PAGE_ALLOCS; i++)
    if (!page_table[i].used)
      return i;
  return -1;
}

/* =========================================================
   HOSTED IMPLEMENTATION (mmap)
   ========================================================= */

#ifndef NEXS_BAREMETAL

#include <sys/mman.h>

void *page_alloc(size_t n_pages) {
  if (n_pages == 0)
    return NULL;
  size_t bytes = n_pages * NEXS_PAGE_SIZE;
  void *p = mmap(NULL, bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return NULL;

  int slot = page_slot_find_free();
  if (slot >= 0) {
    page_table[slot].base  = p;
    page_table[slot].pages = n_pages;
    page_table[slot].used  = 1;
  }
  return p;
}

void page_free(void *ptr, size_t n_pages) {
  if (!ptr || n_pages == 0)
    return;
  munmap(ptr, n_pages * NEXS_PAGE_SIZE);
  for (int i = 0; i < MAX_PAGE_ALLOCS; i++) {
    if (page_table[i].used && page_table[i].base == ptr) {
      page_table[i].used = 0;
      page_table[i].base = NULL;
      page_table[i].pages = 0;
      break;
    }
  }
}

/* =========================================================
   BARE-METAL IMPLEMENTATION (static bump allocator)
   ========================================================= */

#else /* NEXS_BAREMETAL */

#define NEXS_LARGE_POOL_PAGES 4096
static uint8_t large_pool[NEXS_LARGE_POOL_PAGES * NEXS_PAGE_SIZE]
    __attribute__((aligned(NEXS_PAGE_SIZE)));
static size_t large_brk = 0;

void *page_alloc(size_t n_pages) {
  if (n_pages == 0)
    return NULL;
  size_t bytes = n_pages * NEXS_PAGE_SIZE;
  if (large_brk + bytes > sizeof(large_pool))
    return NULL; /* OOM */

  void *p = large_pool + large_brk;
  large_brk += bytes;
  memset(p, 0, bytes);

  int slot = page_slot_find_free();
  if (slot >= 0) {
    page_table[slot].base  = p;
    page_table[slot].pages = n_pages;
    page_table[slot].used  = 1;
  }
  return p;
}

void page_free(void *ptr, size_t n_pages) {
  /* Bare-metal bump allocator: cannot reclaim individual pages */
  (void)ptr;
  (void)n_pages;
  /* Mark slot as free so the region is no longer tracked */
  for (int i = 0; i < MAX_PAGE_ALLOCS; i++) {
    if (page_table[i].used && page_table[i].base == ptr) {
      page_table[i].used = 0;
      break;
    }
  }
}

#endif /* NEXS_BAREMETAL */

/* =========================================================
   is_page_ptr — detect page-allocated pointers
   ========================================================= */

int is_page_ptr(void *ptr) {
  if (!ptr)
    return 0;
  for (int i = 0; i < MAX_PAGE_ALLOCS; i++) {
    if (page_table[i].used && page_table[i].base == ptr)
      return 1;
  }
  return 0;
}

/* =========================================================
   UNIFIED ALLOCATOR
   ========================================================= */

void *nexs_alloc(size_t size) {
  if (size > LARGE_ALLOC_THRESH) {
    size_t n_pages = (size + NEXS_PAGE_SIZE - 1) / NEXS_PAGE_SIZE;
    void *p = page_alloc(n_pages);
    if (!p)
      die("nexs_alloc: page_alloc failed (large allocation)");
    return p;
  }
  return xmalloc(size);
}

void nexs_free(void *ptr, size_t size) {
  if (!ptr)
    return;
  if (is_page_ptr(ptr)) {
    size_t n_pages = (size + NEXS_PAGE_SIZE - 1) / NEXS_PAGE_SIZE;
    page_free(ptr, n_pages);
    return;
  }
  xfree(ptr);
}
