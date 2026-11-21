/* Production Buddy allocator nonrecursive topology/reclaim tests.
 * The 4 KiB provider profile makes every leaf and full exhaustion visible.
 * No fake allocator, assert or hidden test-only implementation.
 */
#include "nexs_alloc.h"

#include <stdint.h>
#include <stdio.h>

static unsigned checks=0U;
static unsigned failed=0U;

static void verify(int good, const char *what) {
  ++checks;
  if (!good) {
    ++failed;
    fprintf(stderr,"BUDDY_ITERATION_FAIL %s\n",what);
  }
}

static void test_geometry_and_checked_rounding(void) {
  verify(POOL_SIZE == 4096U, "correct admitted test domain");
  verify(MIN_BLOCK > 0U && (MIN_BLOCK & (MIN_BLOCK-1U)) == 0U,
         "smallest allocation unit power-of-two");
  verify((POOL_SIZE & (POOL_SIZE-1U)) == 0U,"pool size power-of-two");
  verify(TREE_NODES == 2U*(POOL_SIZE/MIN_BLOCK)-1U,
         "tree matches pool representation");
  verify(buddy_next_pow2(1U)==MIN_BLOCK,"round minimum");
  verify(buddy_next_pow2(MIN_BLOCK)==MIN_BLOCK,"exact minimum");
  verify(buddy_next_pow2(MIN_BLOCK+1U)==2U*MIN_BLOCK,
         "next bucket after minimum");
  verify(buddy_next_pow2(POOL_SIZE)==POOL_SIZE,
         "largest admitted bucket");
  verify(buddy_next_pow2(SIZE_MAX)==0U,
         "overflow sized request rejected without spin");
}

static void test_full_domain_and_coalescence(void) {
  void *whole=buddy_alloc(POOL_SIZE);
  verify(whole!=NULL,"entire physical buddy available initially");
  verify(buddy_alloc(MIN_BLOCK)==NULL,"whole allocation excludes child");
  if (whole!=NULL)
    buddy_free(whole);
  verify(buddy_alloc(0U)==NULL,"zero allocation rejected");
  verify(buddy_alloc(POOL_SIZE+1U)==NULL,"oversized rejected");
  void *nodes[POOL_SIZE/MIN_BLOCK];
  for (size_t i=0U;i<POOL_SIZE/MIN_BLOCK;++i) {
    nodes[i]=buddy_alloc(MIN_BLOCK);
    verify(nodes[i]!=NULL,"leaf allocated");
    if (nodes[i]!=NULL)
      verify((uintptr_t)nodes[i] % MIN_BLOCK == 0U,
             "minimum architecture alignment");
  }
  verify(buddy_alloc(MIN_BLOCK)==NULL,"full leaf domain OOM");
  for (size_t i=0U;i<POOL_SIZE/MIN_BLOCK;i+=2U)
    buddy_free(nodes[i]);
  verify(buddy_alloc(2U*MIN_BLOCK)==NULL,
         "alternating fragments cannot manufacture contiguous double block");
  for (size_t i=1U;i<POOL_SIZE/MIN_BLOCK;i+=2U)
    buddy_free(nodes[i]);
  whole=buddy_alloc(POOL_SIZE);
  verify(whole!=NULL,"postorder merge rebuilds full domain");
  if (whole!=NULL)
    buddy_free(whole);
}

static void test_interior_and_unknown_free_rejection(void) {
  unsigned char *p=(unsigned char *)buddy_alloc(512U);
  verify(p!=NULL,"512 byte owned lease");
  if (p==NULL)
    return;
  buddy_free(p+32U);
  verify(buddy_alloc(POOL_SIZE)==NULL,
         "interior pointer cannot revoke parent allocation");
  buddy_free((void *)(uintptr_t)0x1234U);
  verify(buddy_alloc(POOL_SIZE)==NULL,
         "unknown pointer cannot free or corrupt owned extent");
  buddy_free(p);
  void *whole=buddy_alloc(POOL_SIZE);
  verify(whole!=NULL,"correct release returns full capacity");
  if (whole!=NULL)
    buddy_free(whole);
}

/* Deterministic property stress: every live Buddy allocation is disjoint,
 * minimum-aligned, and retains its content through unrelated releases. */
static void test_randomized_live_extent_separation(void) {
  void *pointers[64] = {0};
  size_t lengths[64] = {0};
  unsigned char signatures[64] = {0};
  uint32_t state = UINT32_C(0xa1b2c3d4);
  for (size_t step = 0U; step < 1200U; ++step) {
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    size_t slot = (size_t)(state % 64U);
    if (pointers[slot] != NULL) {
      verify(((unsigned char *)pointers[slot])[0] == signatures[slot],
             "live allocation canary preserved");
      buddy_free(pointers[slot]);
      pointers[slot] = NULL;
    } else {
      size_t request = MIN_BLOCK << ((state >> 8U) % 6U);
      void *ptr = buddy_alloc(request);
      if (ptr == NULL)
        continue;
      uintptr_t lo = (uintptr_t)ptr;
      uintptr_t hi = lo + request;
      verify(lo % MIN_BLOCK == 0U,"admitted block always aligned");
      for (size_t other = 0U; other < 64U; ++other) {
        if (pointers[other] != NULL) {
          uintptr_t a = (uintptr_t)pointers[other];
          uintptr_t b = a + lengths[other];
          verify(hi <= a || b <= lo,
                 "all live extents nonoverlapping");
        }
      }
      pointers[slot] = ptr;
      lengths[slot] = request;
      signatures[slot] = (unsigned char)(step % 251U + 1U);
      ((unsigned char *)ptr)[0] = signatures[slot];
    }
  }
  for (size_t i = 0U; i < 64U; ++i) {
    if (pointers[i] != NULL) {
      verify(((unsigned char *)pointers[i])[0] == signatures[i],
             "final active allocation preserved");
      buddy_free(pointers[i]);
    }
  }
  void *whole = buddy_alloc(POOL_SIZE);
  verify(whole != NULL, "stress leaves exact original provider domain");
  if (whole != NULL)
    buddy_free(whole);
}

int main(void) {
  test_geometry_and_checked_rounding();
  test_full_domain_and_coalescence();
  test_interior_and_unknown_free_rejection();
  test_randomized_live_extent_separation();
  fprintf(stdout,"BUDDY_ITERATION_CORE %s checks=%u failures=%u\n",
          failed==0U?"PASS":"FAIL",checks,failed);
  return failed==0U?0:1;
}
