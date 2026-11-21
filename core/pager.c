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
#include <stdio.h>
/* Same provider-level counters as core/buddy.c; no second allocator. */
extern void nexs_allocator_observe(int page, size_t bytes,
                                   int success, int release);

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

/* Release only a precise, live provider extent; a caller-provided
 * pointer and byte count are NOT sufficient authority to unmap memory. */
static int page_slot_find_exact(const void *ptr, size_t n_pages) {
  if (ptr == NULL || n_pages == 0)
    return -1;
  for (int i = 0; i < MAX_PAGE_ALLOCS; ++i) {
    if (page_table[i].used && page_table[i].base == ptr &&
        page_table[i].pages == n_pages)
      return i;
  }
  return -1;
}

static void page_slot_retire(int slot) {
  page_table[slot].base = NULL;
  page_table[slot].pages = 0U;
  page_table[slot].used = 0;
}

/* =========================================================
   HOSTED IMPLEMENTATION (mmap)
   ========================================================= */

#ifndef NEXS_BAREMETAL

#include <sys/mman.h>

static void *page_alloc_impl(size_t n_pages) {
  if (n_pages == 0 || n_pages > SIZE_MAX / NEXS_PAGE_SIZE)
    return NULL;
  int slot = page_slot_find_free();
  if (slot < 0)
    return NULL;
  size_t bytes = n_pages * NEXS_PAGE_SIZE;
  void *p = mmap(NULL, bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return NULL;

  page_table[slot].base  = p;
  page_table[slot].pages = n_pages;
  page_table[slot].used  = 1;

  /* mmap returns a hosted virtual address, NOT a physical frame or a
   * capability. The provider slot is the sole local ownership record;
   * never publish synthetic /mem/virt/.../phys authority into NXREG. */
  return p;
}

static int page_free_checked_impl(void *ptr, size_t n_pages) {
  int slot = page_slot_find_exact(ptr, n_pages);
  if (slot < 0)
    return -1;
  /* Never unmap a guessed length or an unowned pointer. */
  size_t bytes = page_table[slot].pages * NEXS_PAGE_SIZE;
  if (munmap(page_table[slot].base, bytes) != 0)
    return -1;
  page_slot_retire(slot);
  return 0;
}

void page_free(void *ptr, size_t n_pages) {
  (void)page_free_checked(ptr, n_pages);
}

/* =========================================================
   BARE-METAL IMPLEMENTATION (static bump allocator)
   ========================================================= */

#else /* NEXS_BAREMETAL */

#ifdef NEXS_SEL4
static uint8_t *large_pool = (uint8_t *)0x44000000UL;
#else
static uint8_t large_pool[LARGE_POOL_PAGES * NEXS_PAGE_SIZE]
    __attribute__((aligned(NEXS_PAGE_SIZE)));
#endif

/* Bitmap for page tracking: 1 bit per page */
static uint32_t page_bitmap[LARGE_POOL_PAGES / 32];

static void bitmap_set(uint32_t bit) {
    page_bitmap[bit / 32] |= (1U << (bit % 32));
}
static void bitmap_clear(uint32_t bit) {
    page_bitmap[bit / 32] &= ~(1U << (bit % 32));
}
static int bitmap_test(uint32_t bit) {
    return page_bitmap[bit / 32] & (1U << (bit % 32));
}

static void *page_alloc_impl(size_t n_pages) {
  if (n_pages == 0 || n_pages > LARGE_POOL_PAGES)
    return NULL;

  /* Search for n_pages contiguous free bits */
  for (uint32_t i = 0; i <= LARGE_POOL_PAGES - n_pages; ++i) {
    /* Bounded membership scan: no break/early unexplained escape. */
    size_t j = 0U;
    while (j < n_pages && !bitmap_test((uint32_t)(i + j)))
      ++j;
    if (j == n_pages) {
      int slot = page_slot_find_free();
      if (slot < 0) return NULL;

      void *p = large_pool + (i * NEXS_PAGE_SIZE);
      for (uint32_t j = 0; j < n_pages; j++) bitmap_set(i + j);
      
      memset(p, 0, n_pages * NEXS_PAGE_SIZE);
      page_table[slot].base  = p;
      page_table[slot].pages = n_pages;
      page_table[slot].used  = 1;
      return p;
    }
    i += (uint32_t)j; /* skip past the first unavailable page */
  }
  return NULL; /* OOM */
}

static int page_free_checked_impl(void *ptr, size_t n_pages) {
  int slot = page_slot_find_exact(ptr, n_pages);
  if (slot < 0)
    return -1;
  uintptr_t address = (uintptr_t)page_table[slot].base;
  uintptr_t pool = (uintptr_t)large_pool;
  if (address < pool || n_pages > LARGE_POOL_PAGES ||
      address - pool >= LARGE_POOL_PAGES * NEXS_PAGE_SIZE ||
      (address - pool) % NEXS_PAGE_SIZE != 0U)
    return -1;
  size_t first = (size_t)((address - pool) / NEXS_PAGE_SIZE);
  if (first > LARGE_POOL_PAGES - n_pages)
    return -1;
  for (size_t j = 0U; j < n_pages; ++j) {
    if (!bitmap_test((uint32_t)(first + j)))
      return -1;
  }
  for (size_t j = 0U; j < n_pages; ++j)
    bitmap_clear((uint32_t)(first + j));
  page_slot_retire(slot);
  return 0;
}

void page_free(void *ptr, size_t n_pages) {
  (void)page_free_checked(ptr, n_pages);
}

#endif /* NEXS_BAREMETAL */

void *page_alloc(size_t n_pages) {
  void *out = page_alloc_impl(n_pages);
  nexs_allocator_observe(1, n_pages, out != NULL, 0);
  return out;
}

int page_free_checked(void *ptr, size_t n_pages) {
  int status = page_free_checked_impl(ptr, n_pages);
  nexs_allocator_observe(1, n_pages, status == 0, 1);
  return status;
}

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
    if (size > SIZE_MAX - (NEXS_PAGE_SIZE - 1U))
      return NULL;
    size_t n_pages = (size + NEXS_PAGE_SIZE - 1U) / NEXS_PAGE_SIZE;
    return page_alloc(n_pages);
  }
  /* Kernel/process resource admission is recoverable even when the
   * existing buddy provider has no block. Do NOT invoke legacy fatal
   * xmalloc in an API whose callers handle allocation failure. */
  void *p = buddy_alloc(size == 0U ? 1U : size);
  if (p != NULL)
    memset(p, 0, size);
  return p;
}

void nexs_free(void *ptr, size_t size) {
  if (!ptr)
    return;
  if (is_page_ptr(ptr)) {
    if (size > SIZE_MAX - (NEXS_PAGE_SIZE - 1U))
      return;
    size_t n_pages = (size + NEXS_PAGE_SIZE - 1U) / NEXS_PAGE_SIZE;
    page_free(ptr, n_pages);
    return;
  }
  xfree(ptr);
}
