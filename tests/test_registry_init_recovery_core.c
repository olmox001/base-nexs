/* Partial startup rollback and reentry tests against the actual registry.
 * A 4KiB Buddy profile is intentionally exhausted without any fault mock.
 */
#include "nexs_registry.h"
#include "nexs_alloc.h"

#include <stdio.h>
#include <stdint.h>

static unsigned checks;
static unsigned failures;

static void verify(int yes, const char *name) {
  ++checks;
  if (!yes) {
    ++failures;
    fprintf(stderr, "REGISTRY_STARTUP_FAIL %s\n", name);
  }
}

int main(void) {
  void *held[256] = {0};
  size_t held_count = 0U;
  size_t block = buddy_next_pow2(sizeof(RegKey));
  verify(POOL_SIZE == 4096U && block != 0U,
         "real 4KiB profile selected for startup admission");
  while (held_count < sizeof(held)/sizeof(held[0])) {
    void *next = buddy_alloc(block);
    if (next == NULL)
      break;
    held[held_count] = next;
    ++held_count;
  }
  verify(held_count >= 5U && buddy_alloc(block) == NULL,
         "real allocator exhausted at exact RegKey reservation size");

  if (held_count >= 2U) {
    buddy_free(held[held_count - 1U]);
    buddy_free(held[held_count - 2U]);
    held[held_count - 1U] = NULL;
    held[held_count - 2U] = NULL;

    verify(reg_init() != 0,
           "partial root initialization refuses provider exhaustion");
    verify(g_registry.root == NULL && g_registry.total_keys == 0U,
           "failed init reclaims root and all linked children");
    verify(reg_lookup("/") == NULL && reg_lookup("/local") == NULL,
           "no partial namespace visible after failed init");
    verify(reg_init() != 0,
           "repeated denied startup does not leak reserved extents");
    verify(g_registry.root == NULL && g_registry.total_keys == 0U,
           "repeated failed init keeps zero live objects");
  }

  for (size_t i = 0U; i < held_count; ++i)
    if (held[i] != NULL)
      buddy_free(held[i]);

  verify(reg_init() == 0,
         "same real Buddy can initialize after all reservations released");
  verify(g_registry.root != NULL && g_registry.total_keys == 7U,
         "all seven canonical root objects published atomically");
  verify(reg_lookup("/local") != NULL && reg_lookup("/fn") != NULL &&
         reg_lookup("/sys") != NULL && reg_lookup("/mod") != NULL &&
         reg_lookup("/env") != NULL && reg_lookup("/type") != NULL,
         "every canonical registry path published after complete admission");
  verify(reg_set("/alive", val_int(42), RK_ALL) == 0,
         "admitted registry accepts ordinary product values");
  size_t before = g_registry.total_keys;
  RegKey *root = g_registry.root;
  verify(reg_init() != 0,
         "second init cannot silently overwrite a live namespace");
  Value existing = reg_get("/alive");
  verify(g_registry.root == root && g_registry.total_keys == before &&
         existing.type == TYPE_INT && existing.ival == 42,
         "rejected reentry preserves the existing object and contents");
  val_free(&existing);
  printf("REGISTRY_STARTUP_ROLLBACK %s checks=%u failures=%u\n",
         failures == 0U ? "PASS" : "FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
