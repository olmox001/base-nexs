/* Production RegKey mutation/rollback under actual exhausted Buddy memory.
 * Does not simulate allocator failure or fake a StateStore/authority.
 * All checks execute the one real registry and Buddy algorithm.
 */
#include "nexs_alloc.h"
#include "nexs_registry.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void verify(int condition, const char *description) {
  ++checks;
  if (!condition) {
    ++failures;
    fprintf(stderr, "REGISTRY_OOM_ROLLBACK_FAIL %s\n", description);
  }
}

int main(void) {
  void *held[256] = {0};
  size_t count = 0U;
  reg_init();
  verify(g_registry.root != NULL, "registry bootstraps in 32KiB profile");
  verify(reg_mkpath("/anchor", RK_ALL) != NULL,
         "existing namespace prefix available before exhaustion");
  verify(reg_mkpath("/reserve", RK_ALL) != NULL,
         "second existing path reserved for queue admission OOM");
  verify(reg_ipc_init_queue("/anchor", 2) == 0,
         "real IPC queue admitted before allocator exhaustion");
  size_t keys_before = g_registry.total_keys;

  while (count < sizeof(held) / sizeof(held[0])) {
    void *block = buddy_alloc(sizeof(RegKey));
    if (block == NULL)
      break;
    held[count] = block;
    ++count;
  }
  /* A page of RegKey-sized blocks can leave a few smaller Buddy extents.
   * Fill those too before testing small IPC and scope admissions. */
  void *small_held[256] = {0};
  size_t small_count = 0U;
  while (small_count < sizeof(small_held) / sizeof(small_held[0])) {
    void *block = buddy_alloc(MIN_BLOCK);
    if (block == NULL)
      break;
    small_held[small_count] = block;
    ++small_count;
  }
  verify(count > 2U, "test consumed actual provider capacity");
  verify(buddy_alloc(MIN_BLOCK) == NULL,
         "allocator exhausted down to smallest real leaf");
  verify(reg_ipc_init_queue("/reserve", 1) != 0 &&
         reg_lookup("/reserve")->queue == NULL,
         "failed IPC queue admission publishes no partial queue");
  verify(reg_ipc_send("/anchor", val_int(123)) != 0 &&
         reg_ipc_pending("/anchor") == 0,
         "failed IPC send never enqueues or mutates queue count");
  verify(buddy_alloc(sizeof(RegKey)) == NULL,
         "real Buddy reports exhaustion without exit");
  verify(reg_push_scope("no_room") == NULL,
         "function scope creation declines exhausted memory safely");
  FILE *capture = tmpfile();
  verify(capture != NULL, "hosted capture available for actual listing test");
  if (capture != NULL) {
    reg_ls("/", capture);
    rewind(capture);
    char output[1024] = {0};
    size_t received = fread(output, 1U, sizeof(output) - 1U, capture);
    output[received] = '\0';
    verify(strstr(output, "listing unavailable: provider exhausted") != NULL,
           "registry listing fails recoverably under real Buddy OOM");
    fclose(capture);
  }
  verify(reg_mkpath("/unavailable", RK_ALL) == NULL,
         "no single-node allocation when provider exhausted");
  verify(reg_lookup("/unavailable") == NULL,
         "failed single-node path is not observable");
  verify(g_registry.total_keys == keys_before,
         "exhausted admission changes no key count");

  for (size_t i = 0U; i < small_count; ++i)
    buddy_free(small_held[i]);

  if (count >= 2U) {
    buddy_free(held[count - 1U]);
    buddy_free(held[count - 2U]);
    held[count - 1U] = NULL;
    held[count - 2U] = NULL;

    verify(reg_mkpath("/anchor/new_a/new_b/new_c", RK_ALL) == NULL,
           "third new child fails when only two real extents remain");
    verify(reg_lookup("/anchor") != NULL,
           "original parent retained after failed multi-node admission");
    verify(reg_lookup("/anchor/new_a") == NULL,
           "first partially linked child rolled back");
    verify(reg_lookup("/anchor/new_a/new_b") == NULL,
           "second partially linked child rolled back");
    verify(g_registry.total_keys == keys_before,
           "multi-node OOM restores exact original count");

    char *scope = reg_push_scope("restored");
    verify(scope != NULL, "function scope admitted after real release");
    if (scope != NULL) {
      verify(reg_lookup(scope) != NULL,
             "successful scope exists in original namespace");
      reg_pop_scope(scope);
      nexs_free(scope, REG_PATH_MAX);
    }
    verify(g_registry.total_keys == keys_before,
           "scope creation and release restore exact object count");

    verify(reg_mkpath("/anchor/recovered", RK_ALL) != NULL,
           "rollback reclaimed enough memory for a new child");
    verify(reg_delete("/anchor/recovered") == 0,
           "newly admitted child releases cleanly");
    verify(g_registry.total_keys == keys_before,
           "allocation and delete restore exact key count");
  }

  for (size_t i = 0U; i < count; ++i)
    if (held[i] != NULL)
      buddy_free(held[i]);
  verify(reg_mkpath("/anchor/new_a/new_b/new_c", RK_ALL) != NULL,
         "provider again admits complete path after reclaimed allocations");
  verify(reg_delete("/anchor/new_a") == 0,
         "recovered path entire descendant subtree released");
  verify(g_registry.total_keys == keys_before,
         "full success and delete preserve registry accounting");
  verify(reg_ipc_send("/anchor", val_int(123)) == 0,
         "same IPC queue accepts message after provider reclamation");
  Value received = val_nil();
  verify(reg_ipc_recv("/anchor", &received) == 0 &&
         received.type == TYPE_INT && received.ival == 123,
         "recovered IPC queue transfers the actual Value exactly once");
  val_free(&received);
  verify(reg_ipc_pending("/anchor") == 0,
         "successful IPC receive reclaims one queue entry");
  printf("REGISTRY_OOM_ROLLBACK %s checks=%u failures=%u\n",
         failures == 0U ? "PASS" : "FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
