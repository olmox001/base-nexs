/* Four genuine host pthreads exercising four separate BACKING domains
 * through the SAME production Buddy implementation. Independent providers
 * need no shared allocator mutex; does not certify concurrent shared-arena
 * mutation or full RuntimeInstance multithreading.
 */
#include "nexs_alloc.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define WORKER_COUNT 4U
#define WORKER_STEPS 5000U
#define BACKING_BYTES 8192U
#define TREE_BYTES (2U*(BACKING_BYTES/MIN_BLOCK)-1U)

typedef struct {
  _Alignas(MIN_BLOCK) uint8_t data[BACKING_BYTES];
  uint8_t tree[TREE_BYTES];
  NexsBuddyArena arena;
  unsigned id;
  unsigned allocations;
  int status;
} Worker;

static Worker workers[WORKER_COUNT];

static void *run_worker(void *user) {
  Worker *worker=(Worker *)user;
  worker->status = nexs_buddy_arena_init(
      &worker->arena, worker->data, sizeof(worker->data),
      worker->tree, sizeof(worker->tree));
  if (worker->status != 0)
    return NULL;
  for (size_t step = 0U; step < WORKER_STEPS; ++step) {
    size_t size = MIN_BLOCK << (step % 6U);
    uint8_t *p=(uint8_t *)nexs_buddy_arena_alloc(&worker->arena,size);
    if (p==NULL) {
      worker->status=-1;
      return NULL;
    }
    p[0]=(uint8_t)(worker->id+1U);
    p[size-1U]=(uint8_t)(step % 251U);
    if (p[0]!=(uint8_t)(worker->id+1U) ||
        p[size-1U]!=(uint8_t)(step % 251U) ||
        nexs_buddy_arena_free(&worker->arena,p)!=0) {
      worker->status=-1;
      return NULL;
    }
    ++worker->allocations;
  }
  return NULL;
}

int main(void) {
  pthread_t threads[WORKER_COUNT];
  int failed=0;
  unsigned spawned[WORKER_COUNT]={0U};
  for (unsigned i=0U;i<WORKER_COUNT;++i) {
    workers[i].id=i;
    if (pthread_create(&threads[i],NULL,run_worker,&workers[i])!=0)
      ++failed;
    else
      spawned[i]=1U;
  }
  for (unsigned i=0U;i<WORKER_COUNT;++i) {
    if (spawned[i]!=0U && pthread_join(threads[i],NULL)!=0)
      ++failed;
  }
  for (unsigned i=0U;i<WORKER_COUNT;++i) {
    if (workers[i].status!=0 || workers[i].allocations!=WORKER_STEPS)
      ++failed;
  }
  for (unsigned i=0U;i<WORKER_COUNT;++i) {
    for (unsigned j=i+1U;j<WORKER_COUNT;++j) {
      if (nexs_buddy_arena_free(&workers[i].arena,workers[j].data)==0)
        ++failed;
    }
  }
  printf("BUDDY_THREADED_ARENAS %s workers=%u per_worker=%u failures=%d\n",
         failed==0?"PASS":"FAIL",WORKER_COUNT,WORKER_STEPS,failed);
  return failed==0?0:1;
}
