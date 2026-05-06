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
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include "../hal/include/nexs_mmu.h"

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
  if (slot < 0) {
    munmap(p, bytes);
    return NULL;
  }
  page_table[slot].base  = p;
  page_table[slot].pages = n_pages;
  page_table[slot].used  = 1;

  /* Track in registry for parity with baremetal */
  char path[128];
  snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/phys", (unsigned long long)(uintptr_t)p);
  reg_set(path, val_int((int64_t)(uintptr_t)p), RK_READ);
  snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/flags", (unsigned long long)(uintptr_t)p);
  reg_set(path, val_int(MMU_PRESENT | MMU_WRITE), RK_READ);

  return p;
}

void page_free(void *ptr, size_t n_pages) {
  if (!ptr || n_pages == 0)
    return;
  munmap(ptr, n_pages * NEXS_PAGE_SIZE);

  /* Untrack in registry */
  char path[128];
  snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/phys", (unsigned long long)(uintptr_t)ptr);
  reg_delete(path);
  snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/flags", (unsigned long long)(uintptr_t)ptr);
  reg_delete(path);

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
static uint8_t large_pool[LARGE_POOL_PAGES * NEXS_PAGE_SIZE]
    __attribute__((aligned(NEXS_PAGE_SIZE)));

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

void *page_alloc(size_t n_pages) {
  if (n_pages == 0 || n_pages > LARGE_POOL_PAGES)
    return NULL;

  /* Search for n_pages contiguous free bits */
  for (uint32_t i = 0; i <= LARGE_POOL_PAGES - n_pages; i++) {
    int found = 1;
    for (uint32_t j = 0; j < n_pages; j++) {
      if (bitmap_test(i + j)) {
        found = 0;
        i += j; /* skip ahead */
        break;
      }
    }
    if (found) {
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
  }
  return NULL; /* OOM */
}

void page_free(void *ptr, size_t n_pages) {
  if (!ptr || n_pages == 0) return;

  /* Verify pointer belongs to large_pool */
  if ((uint8_t *)ptr < large_pool || 
      (uint8_t *)ptr >= large_pool + (LARGE_POOL_PAGES * NEXS_PAGE_SIZE)) {
    return;
  }

  uint32_t start_bit = ((uint8_t *)ptr - large_pool) / NEXS_PAGE_SIZE;
  for (uint32_t j = 0; j < n_pages; j++) {
    if (start_bit + j < LARGE_POOL_PAGES) {
      bitmap_clear(start_bit + j);
    }
  }

  /* Mark slot as free */
  for (int i = 0; i < MAX_PAGE_ALLOCS; i++) {
    if (page_table[i].used && page_table[i].base == ptr) {
      page_table[i].used = 0;
      page_table[i].base = NULL;
      page_table[i].pages = 0;
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
