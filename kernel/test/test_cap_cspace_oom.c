/* Actual Buddy OOM, no injected allocation failures or CSpace mock. */
#include "nexs_proc.h"
#include "nexs_cap.h"
#include "nexs_alloc.h"
#include "nexs_registry.h"
#include <stdint.h>
#include <stdio.h>

static unsigned checks;
static unsigned failures;
static void check(int yes, const char *label) {
  ++checks;
  if (!yes) { ++failures; fprintf(stderr,"CSPACE_OOM_FAIL %s\n",label); }
}
int main(void) {
  NexsProc process = {0};
  (void)snprintf(process.reg_path,sizeof(process.reg_path),"/proc/103");
  check(POOL_SIZE == 4096U, "real 4KiB Buddy resource selected");
  check(reg_init() == 0, "real registry admitted before exhaustion");
  void *held[64] = {0};
  size_t used = 0U;
  size_t reservation = 16U*sizeof(Cap);
  while (used < sizeof(held)/sizeof(held[0])) {
    void *block = buddy_alloc(reservation);
    if (block == NULL)
      break;
    held[used] = block;
    ++used;
  }
  check(used >= 2U && buddy_alloc(reservation) == NULL,
        "real provider exhausted at exact CSpace reservation size");
  check(cap_init(&process,16U) != 0,
        "fail closed when actual owner reservation cannot be admitted");
  check(process.cspace.caps == NULL && process.cspace.size == 0U &&
        process.cspace.count == 0U,
        "failed init leaves owner unchanged");
  Cap real = {.type=CAP_TCB,.rights=CAP_READ,.badge=7U,.obj=&process};
  check(cap_insert(&process,0U,real) != 0 && process.cspace.count == 0U,
        "failed admission cannot publish a ghost capability");
  if (used > 0U) {
    buddy_free(held[used-1U]);
    held[used-1U]=NULL;
  }
  check(cap_init(&process,16U) == 0,
        "freed provider reservation recovers owner admission");
  check(cap_insert(&process,0U,real) == 0 &&
        process.cspace.count == 1U,
        "owner issues local slot even when diagnostics cannot allocate");
  g_current_proc = &process;
  check(cap_lookup(0U) != NULL && cap_lookup(0U)->obj == &process,
        "real current process resolves its admitted slot");
  check(cap_remove(&process,0U) == 0 && cap_lookup(0U) == NULL,
        "exact revoke clears the object under memory pressure");
  g_current_proc = NULL;
  check(cap_destroy(&process) == 0,
        "provider-backed CSpace owner releases its exact extent");
  for (size_t i=0U;i<used;++i)
    if (held[i] != NULL)
      buddy_free(held[i]);
  /* The canonical RegKey root remains live. A whole-pool allocation
   * must NOT succeed, but the released CSpace can be re-admitted. */
  check(cap_init(&process,16U) == 0 && process.cspace.count == 0U,
        "same owner re-admitted after real CSpace extent reclamation");
  check(cap_destroy(&process) == 0,
        "re-admitted CSpace reclaimed without stale double-release");
  check(cap_destroy(&process) != 0,
        "second destruction denied while registry remains live");
  printf("CSPACE_OOM_RECOVERY %s checks=%u failures=%u\n",
         failures == 0U ? "PASS":"FAIL",checks,failures);
  return failures == 0U ? 0 : 1;
}
