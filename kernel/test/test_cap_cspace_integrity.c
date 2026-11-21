/* Real CSpace and real process/current-proc state, not registry authority.
 * All operations link the production kernel/cap.c and kernel/proc.c.
 */
#include "nexs_proc.h"
#include "nexs_cap.h"
#include "nexs_alloc.h"
#include "nexs_registry.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
static void check(int yes, const char *label) {
  ++checks;
  if (!yes) { ++failures; fprintf(stderr,"CSPACE_INTEGRITY_FAIL %s\n",label); }
}

int main(void) {
  NexsProc first = {0};
  NexsProc second = {0};
  (void)snprintf(first.reg_path,sizeof(first.reg_path),"/proc/101");
  (void)snprintf(second.reg_path,sizeof(second.reg_path),"/proc/102");
  check(reg_init() == 0, "one real registry starts");
  check(cap_init(NULL,8U) != 0, "null owner rejected");
  check(cap_init(&first,0U) != 0, "zero capacity rejected");
  check(cap_init(&first,8U) == 0, "owner admitted with actual Buddy backing");
  check(first.cspace.caps != NULL && first.cspace.size == 8U &&
        first.cspace.count == 0U, "initial all-empty CSpace accounting");
  check(cap_init(&first,8U) != 0, "reinitialization cannot overwrite owner");
  check(g_current_proc == NULL && cap_lookup(1U) == NULL,
        "no current process cannot acquire arbitrary cap");
  g_current_proc = &first;
  check(cap_lookup(1U) == NULL, "empty slot never returns an object");
  Value forged = val_str("type=TCB rights=15 badge=0");
  check(forged.type == TYPE_STR &&
        reg_set("/proc/101/caps/1",forged,RK_ALL) == 0,
        "test creates a real but unauthenticated registry path");
  val_free(&forged);
  check(cap_lookup(1U) == NULL,
        "registry path never manufactures a CSpace capability");
  Cap real = { .type = CAP_TCB, .rights = CAP_READ, .badge = 7U,
               .obj = &first };
  Cap invalid_type = real;
  invalid_type.type = CAP_RESERVED;
  Cap invalid_rights = real;
  invalid_rights.rights = 0x80000000U;
  Cap no_object = real;
  no_object.obj = NULL;
  check(cap_insert(&first,1U,invalid_type) != 0,
        "unknown/RESERVED cap type rejected");
  check(cap_insert(&first,1U,invalid_rights) != 0,
        "noncontract rights bits rejected");
  check(cap_insert(&first,1U,no_object) != 0,
        "capability with no actual object denied");
  check(cap_insert(&first,8U,real) != 0,
        "out of bounds slot denied");
  check(first.cspace.count == 0U,
        "invalid inserts never inflate accounting");
  check(cap_insert(&first,1U,real) == 0,
        "real TCB object admitted to requested slot");
  check(first.cspace.count == 1U && cap_lookup(1U) != NULL &&
        cap_lookup(1U)->obj == &first && cap_lookup(1U)->badge == 7U,
        "single live CSpace slot has exact object and badge");
  check(cap_insert(&first,1U,real) != 0 && first.cspace.count == 1U,
        "duplicate insert does not overwrite or increase count");
  check(cap_remove(&first,4U) != 0 && first.cspace.count == 1U,
        "removing empty slot cannot underflow accounting");
  check(cap_remove(&first,8U) != 0,
        "removing out-of-bounds slot denied");
  check(cap_remove(&first,1U) == 0 && first.cspace.count == 0U,
        "live-slot revoke changes count exactly once");
  check(cap_lookup(1U) == NULL &&
        first.cspace.caps[1U].type == CAP_NONE &&
        first.cspace.caps[1U].obj == NULL &&
        first.cspace.caps[1U].rights == 0U,
        "removed cap has no lingering kernel pointer or rights");
  check(cap_remove(&first,1U) != 0 && first.cspace.count == 0U,
        "double revoke cannot underflow accounting");
  check(cap_insert(&first,1U,real) == 0 && first.cspace.count == 1U,
        "genuine freed slot can be reoccupied");
  check(cap_init(&second,8U) == 0,
        "independent process CSpace admitted from same provider");
  g_current_proc = &second;
  check(cap_lookup(1U) == NULL,
        "first process slot does not appear in second process CSpace");
  g_current_proc = &first;
  check(cap_lookup(1U) != NULL,
        "switch back restores local process lookup, not global path lookup");
  check(cap_destroy(&first) != 0 && first.cspace.count == 1U,
        "owner cannot deallocate CSpace with live capability");
  check(cap_remove(&first,1U) == 0,
        "final live slot reclaimed before owner release");
  check(cap_destroy(&first) == 0 && cap_destroy(&second) == 0,
        "both real CSpace owners release exact admitted extents");
  check(cap_destroy(&first) != 0 && first.cspace.caps == NULL,
        "double owner release rejected without stale memory");
  g_current_proc = NULL;
  printf("CSPACE_INTEGRITY %s checks=%u failures=%u\n",
         failures == 0U ? "PASS":"FAIL",checks,failures);
  return failures == 0U ? 0 : 1;
}
