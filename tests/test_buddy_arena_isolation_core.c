/* Two truly independent caller-backed buddy domains, SAME product algorithm.
 * No fake malloc, no second allocator, no process-global instance shortcuts.
 */
#include "nexs_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define A_BYTES 4096U
#define B_BYTES 8192U
#define A_NODES (2U*(A_BYTES/MIN_BLOCK)-1U)
#define B_NODES (2U*(B_BYTES/MIN_BLOCK)-1U)

static _Alignas(MIN_BLOCK) uint8_t a_bytes[A_BYTES];
static _Alignas(MIN_BLOCK) uint8_t b_bytes[B_BYTES];
static uint8_t a_tree[A_NODES];
static uint8_t b_tree[B_NODES];
static unsigned checks=0U;
static unsigned failed=0U;

static void verify(int good,const char *label) {
  ++checks;
  if (!good) {
    ++failed;
    fprintf(stderr,"BUDDY_ARENA_ISOLATION_FAIL %s\n",label);
  }
}

static void test_independent_provider_ownership(void) {
  NexsBuddyArena a={0};
  NexsBuddyArena b={0};
  verify(nexs_buddy_arena_init(&a,a_bytes,A_BYTES,a_tree,sizeof(a_tree))==0,
         "first provider domain admitted");
  verify(nexs_buddy_arena_init(&b,b_bytes,B_BYTES,b_tree,sizeof(b_tree))==0,
         "second provider domain admitted");
  uint8_t *one=(uint8_t *)nexs_buddy_arena_alloc(&a,A_BYTES);
  uint8_t *two=(uint8_t *)nexs_buddy_arena_alloc(&b,B_BYTES);
  verify(one==a_bytes,"first arena uses supplied memory not global pool");
  verify(two==b_bytes,"second arena uses independent memory and metadata");
  if (one!=NULL && two!=NULL) {
    one[0]=0xa1U;
    two[0]=0xb2U;
    verify(nexs_buddy_arena_free(&b,one)!=0,
           "second arena cannot release first arena ownership");
    verify(nexs_buddy_arena_free(&a,two)!=0,
           "first arena cannot release second arena ownership");
    verify(nexs_buddy_arena_free(&a,one+32U)!=0,
           "interior pointer is never valid ownership");
    verify(nexs_buddy_arena_alloc(&a,MIN_BLOCK)==NULL,
           "cross-arena wrong free did not reclaim first provider");
    verify(nexs_buddy_arena_alloc(&b,MIN_BLOCK)==NULL,
           "cross-arena wrong free did not reclaim second provider");
    verify(one[0]==0xa1U && two[0]==0xb2U,
           "independent alive payload unchanged");
  }
  if (one!=NULL)
    verify(nexs_buddy_arena_free(&a,one)==0,"first owner reclaims one");
  if (two!=NULL)
    verify(nexs_buddy_arena_free(&b,two)==0,"second owner reclaims two");
  verify(nexs_buddy_arena_free(&b,two)!=0,
         "double free in second domain rejected");
  one=(uint8_t *)nexs_buddy_arena_alloc(&a,A_BYTES);
  two=(uint8_t *)nexs_buddy_arena_alloc(&b,B_BYTES);
  verify(one!=NULL && two!=NULL,"both domains recover independently");
  if (one!=NULL)
    verify(nexs_buddy_arena_free(&a,one)==0,"first domain full free");
  if (two!=NULL)
    verify(nexs_buddy_arena_free(&b,two)==0,"second domain full free");
}

static void test_malformed_provider_admission_is_atomic(void) {
  NexsBuddyArena arena={0};
  verify(nexs_buddy_arena_init(&arena,a_bytes,A_BYTES,a_tree,A_NODES-1U)!=0,
         "insufficient metadata is refused");
  verify(arena.storage==NULL,"failed init did not publish an arena");
  verify(nexs_buddy_arena_init(&arena,a_bytes,A_BYTES+1U,a_tree,A_NODES)!=0,
         "non-power-of-two provider capacity denied");
  verify(arena.storage==NULL,"malformed capacity did not publish");
  verify(nexs_buddy_arena_init(&arena,a_bytes,A_BYTES,a_bytes,A_NODES)!=0,
         "metadata overlapping physical page backing rejected");
  verify(arena.storage==NULL,"overlap failed without mutation");
  verify(nexs_buddy_arena_alloc(&arena,MIN_BLOCK)==NULL,
         "uninitialized arena cannot allocate");
  verify(nexs_buddy_arena_free(&arena,a_bytes)!=0,
         "uninitialized arena cannot release");
  verify(nexs_buddy_arena_init(NULL,a_bytes,A_BYTES,a_tree,A_NODES)!=0,
         "null owner rejected");
  verify(nexs_buddy_arena_init(&arena,(uint8_t *)a_bytes+1U,
                              A_BYTES,a_tree,A_NODES)!=0,
         "unaligned backing provider rejected");
  verify(nexs_buddy_arena_init((NexsBuddyArena *)(void *)a_bytes,
                              a_bytes,A_BYTES,a_tree,A_NODES)!=0,
         "arena control structure inside data backing is not admitted");
  verify(nexs_buddy_arena_init((NexsBuddyArena *)(void *)a_tree,
                              a_bytes,A_BYTES,a_tree,A_NODES)!=0,
         "arena control structure inside metadata is not admitted");
}

static void test_reentrant_isolation_without_global_reset(void) {
  NexsBuddyArena a={0},b={0};
  verify(nexs_buddy_arena_init(&a,a_bytes,A_BYTES,a_tree,A_NODES)==0,
         "repeat admission for one explicit owner");
  verify(nexs_buddy_arena_init(&b,b_bytes,B_BYTES,b_tree,B_NODES)==0,
         "repeat admission for other explicit owner");
  void *live=nexs_buddy_arena_alloc(&a,256U);
  verify(live!=NULL,"first instance live allocation");
  if (live!=NULL)
    memset(live,0x71,256U);
  void *many[16]={0};
  for (size_t i=0U;i<16U;++i) {
    many[i]=nexs_buddy_arena_alloc(&b,512U);
    verify(many[i]!=NULL,"second instance admits independent blocks");
  }
  if (live!=NULL)
    verify(((unsigned char *)live)[255]==0x71U,
           "second instance work cannot touch first instance payload");
  for (size_t i=0U;i<16U;++i) {
    if (many[i]!=NULL)
      verify(nexs_buddy_arena_free(&b,many[i])==0,
             "second instance exact block release");
  }
  if (live!=NULL)
    verify(nexs_buddy_arena_free(&a,live)==0,
           "first instance exact block release");
}

int main(void) {
  test_independent_provider_ownership();
  test_malformed_provider_admission_is_atomic();
  test_reentrant_isolation_without_global_reset();
  fprintf(stdout,"BUDDY_ARENA_ISOLATION %s checks=%u failures=%u\n",
          failed==0U?"PASS":"FAIL",checks,failed);
  return failed==0U?0:1;
}
