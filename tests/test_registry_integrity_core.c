/*
 * Actual canonical NEXS registry integrity tests, linked with production
 * registry, Value, DynArray and buddy allocation. No mocks or assert().
 *
 * This protects RegKey data while NXOLMO StateStore replaces that backend.
 * Not a claim of NXREG capability/transaction implementation.
 */
#include "nexs_registry.h"
#include "nexs_alloc.h"
#include "nexs_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks = 0U;
static unsigned failures = 0U;

static int verify(int condition, const char *label) {
  ++checks;
  if (!condition) {
    ++failures;
    fprintf(stderr, "REGISTRY_INTEGRITY_FAIL: %s\n", label);
    return 0;
  }
  return 1;
}

static int test_exact_path_rejection(void) {
  char path[REG_PATH_MAX + 70U];
  size_t old_count = g_registry.total_keys;
  memset(path, 'a', sizeof(path));
  path[0] = '/';
  path[sizeof(path) - 1U] = '\0';
  int ok = verify(reg_mkpath(path, RK_ALL) == NULL,
                  "oversized path must not silently truncate");
  ok &= verify(reg_lookup(path) == NULL,
               "oversized path must not alias a shorter path");
  ok &= verify(reg_set(path, val_int(67), RK_ALL) != 0,
               "oversized path assignment fails closed");
  ok &= verify(g_registry.total_keys == old_count,
               "invalid length does not create partial keys");
  return ok;
}

static int test_oversized_segment_rejection(void) {
  char name[NAME_LEN + 24U];
  memset(name, 'b', sizeof(name));
  name[0] = '/';
  name[sizeof(name) - 1U] = '\0';
  size_t old_count = g_registry.total_keys;
  int ok = verify(reg_mkpath(name, RK_ALL) == NULL,
                  "overlong single segment rejected");
  ok &= verify(reg_set(name, val_int(18), RK_ALL) != 0,
               "overlong segment not admitted");
  ok &= verify(reg_lookup(name) == NULL, "overlong lookup rejected");
  ok &= verify(g_registry.total_keys == old_count,
               "overlong segment must not create truncated alias");
  return ok;
}

static int test_root_delete_forbidden(void) {
  size_t old_count = g_registry.total_keys;
  int ok = verify(reg_delete("/") != 0, "root cannot be deleted");
  ok &= verify(reg_lookup("/") == g_registry.root && g_registry.root != NULL,
               "root still valid after rejected delete");
  ok &= verify(g_registry.total_keys == old_count,
               "root deletion rejection preserves key count");
  return ok;
}

static int test_deep_subtree_delete(void) {
  char path[REG_PATH_MAX] = "/integrity/deep";
  if (!verify(reg_mkpath(path, RK_ALL) != NULL,
              "create parent for deep subtree"))
    return 0;
  size_t before = g_registry.total_keys;
  for (size_t i = 0U; i < 96U; ++i) {
    size_t used = strlen(path);
    if (used + 3U >= sizeof(path))
      return verify(0, "deep test path fits");
    memcpy(path + used, "/a", 3U);
    if (!verify(reg_set(path, val_int((int64_t)i), RK_ALL) == 0,
                "deep descendant allocated"))
      return 0;
  }
  int ok = verify(g_registry.total_keys == before + 96U,
                  "deep tree count exact");
  ok &= verify(reg_delete("/integrity/deep") == 0,
               "deep subtree deleted");
  ok &= verify(reg_lookup("/integrity/deep") == NULL,
               "deep subtree no longer reachable");
  ok &= verify(g_registry.total_keys == before - 1U,
               "deep subtree frees all descendant keys");
  return ok;
}

static int test_wide_subtree_delete(void) {
  if (!verify(reg_mkpath("/integrity/wide", RK_ALL) != NULL,
              "create parent for wide subtree"))
    return 0;
  size_t before = g_registry.total_keys;
  for (size_t i = 0U; i < 2400U; ++i) {
    char name[REG_PATH_MAX];
    snprintf(name, sizeof(name), "/integrity/wide/entry_%04zu", i);
    if (!verify(reg_set(name, val_int((int64_t)i), RK_ALL) == 0,
                "wide descendant allocated"))
      return 0;
  }
  int ok = verify(g_registry.total_keys == before + 2400U,
                  "2400 descendants counted without global table limit");
  ok &= verify(reg_delete("/integrity/wide") == 0, "wide tree deleted");
  ok &= verify(reg_lookup("/integrity/wide") == NULL,
               "wide subtree inaccessible after delete");
  ok &= verify(g_registry.total_keys == before - 1U,
               "every wide descendant released (no 1024-node stack drop)");
  return ok;
}

static int test_move_reject_collision_without_mutation(void) {
  int ok = verify(reg_set("/integrity/src", val_int(21), RK_ALL) == 0,
                  "create source for collision");
  ok &= verify(reg_set("/integrity/dst", val_int(29), RK_ALL) == 0,
               "create destination for collision");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_move("/integrity/src", "/integrity/dst") != 0,
               "existing destination must not be overwritten or duplicated");
  Value source = reg_get("/integrity/src");
  Value target = reg_get("/integrity/dst");
  ok &= verify(source.type == TYPE_INT && source.ival == 21,
               "collision leaves source unchanged");
  ok &= verify(target.type == TYPE_INT && target.ival == 29,
               "collision leaves existing destination unchanged");
  ok &= verify(g_registry.total_keys == before,
               "collision does not change object count");
  val_free(&source);
  val_free(&target);
  return ok;
}

static int test_move_reject_descendant(void) {
  int ok = verify(reg_mkpath("/integrity/ancestor/child", RK_ALL) != NULL,
                  "create ancestor and descendant");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_move("/integrity/ancestor",
                        "/integrity/ancestor/child/renamed") != 0,
               "move into own descendant rejected");
  ok &= verify(reg_lookup("/integrity/ancestor/child") != NULL,
               "rejected cycle preserves original subtree");
  ok &= verify(g_registry.total_keys == before,
               "rejected cycle leaves count unchanged");
  return ok;
}

static int test_move_reject_overlong_descendant_path(void) {
  int ok = verify(reg_set("/integrity/move_deep/child", val_int(57),
                           RK_ALL) == 0, "create source with child");
  char dst[REG_PATH_MAX];
  memset(dst, 'z', sizeof(dst));
  memcpy(dst, "/integrity/", 11U);
  dst[sizeof(dst) - 1U] = '\0';
  size_t before = g_registry.total_keys;
  ok &= verify(reg_move("/integrity/move_deep", dst) != 0,
               "move fails before target descendants truncate");
  Value old = reg_get("/integrity/move_deep/child");
  ok &= verify(old.type == TYPE_INT && old.ival == 57,
               "failed move keeps source reachable");
  val_free(&old);
  ok &= verify(g_registry.total_keys == before,
               "failed move causes no partial destination mutations");
  return ok;
}

static int test_large_tree_move_updates_every_path(void) {
  int ok = verify(reg_mkpath("/integrity/from", RK_ALL) != NULL,
                  "create source for tree move");
  for (size_t i = 0U; i < 1700U; ++i) {
    char name[REG_PATH_MAX];
    snprintf(name, sizeof(name), "/integrity/from/key_%04zu", i);
    if (!verify(reg_set(name, val_int((int64_t)i), RK_ALL) == 0,
                "move source descendant created"))
      return 0;
  }
  size_t before = g_registry.total_keys;
  ok &= verify(reg_move("/integrity/from", "/integrity/to") == 0,
               "relocate 1700 children");
  ok &= verify(reg_lookup("/integrity/from") == NULL,
               "old path invalidated after move");
  ok &= verify(g_registry.total_keys == before,
               "move does not create or delete objects");
  for (size_t i = 0U; i < 1700U; ++i) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/integrity/to/key_%04zu", i);
    RegKey *key = reg_lookup(path);
    if (!verify(key != NULL && strcmp(key->path, path) == 0 &&
                key->val.type == TYPE_INT && key->val.ival == (int64_t)i,
                "all moved descendants retain full new path and value"))
      return 0;
  }
  ok &= verify(reg_delete("/integrity/to") == 0,
               "moving a subtree preserves later deletion");
  ok &= verify(g_registry.total_keys == before - 1701U,
               "moved descendants fully released");
  return ok;
}

static int test_unmount_preserves_exact_counter(void) {
  int ok = verify(reg_mkpath("/integrity/unmount", RK_ALL) != NULL,
                  "create unmount parent");
  ok &= verify(reg_set("/integrity/unmount/a", val_int(1), RK_ALL) == 0,
               "create unmount child A");
  ok &= verify(reg_set("/integrity/unmount/b", val_int(2), RK_ALL) == 0,
               "create unmount child B");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_unmount(NULL, "/integrity/unmount") == 0,
               "unmount all children");
  ok &= verify(g_registry.total_keys == before - 2U,
               "unmount count correct");
  ok &= verify(reg_lookup("/integrity/unmount/a") == NULL &&
               reg_lookup("/integrity/unmount/b") == NULL,
               "unmount removes both children");
  return ok;
}

static int test_selective_unmount_preserves_count(void) {
  int ok = verify(reg_set("/integrity/mount_src/red", val_int(1),
                            RK_ALL) == 0, "selective source red");
  ok &= verify(reg_set("/integrity/mount_src/green", val_int(2),
                          RK_ALL) == 0, "selective source green");
  ok &= verify(reg_mount("/integrity/mount_src",
                         "/integrity/mount_dst", 1) == 0,
               "mount two values");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_unmount("/integrity/mount_src",
                           "/integrity/mount_dst") == 0,
               "selective unmount removes copies");
  ok &= verify(g_registry.total_keys == before - 2U,
               "selective unmount correctly accounts detached keys");
  ok &= verify(reg_lookup("/integrity/mount_src/red") != NULL,
               "source unchanged after selective unmount");
  return ok;
}

static int test_iterative_mount_large_tree(void) {
  const char *from = "/integrity/copy_source";
  const char *to = "/integrity/copy_target";
  int ok = verify(reg_mkpath(from, RK_ALL) != NULL,
                  "large mount source exists");
  if (!ok)
    return 0;
  for (size_t i = 0U; i < 1400U; ++i) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/item_%04zu", from, i);
    if (!verify(reg_set(path, val_int((int64_t)i), RK_ALL) == 0,
                "large mount source content"))
      return 0;
  }
  size_t before = g_registry.total_keys;
  ok &= verify(reg_mount(from, to, 1) == 0,
               "1400-node mount uses parent links instead of recursion");
  ok &= verify(g_registry.total_keys == before + 1401U,
               "mount creates exactly one destination and 1400 children");
  for (size_t i = 0U; i < 1400U; ++i) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/item_%04zu", to, i);
    RegKey *key = reg_lookup(path);
    if (!verify(key != NULL && strcmp(key->path, path) == 0 &&
                key->val.type == TYPE_INT && key->val.ival == (int64_t)i,
                "all mirrored nodes have canonical identity"))
      return 0;
  }
  ok &= verify(reg_unmount(from, to) == 0,
               "unmount 1400 mirrored children");
  ok &= verify(g_registry.total_keys == before + 1U,
               "unmount frees each mirror exactly once");
  ok &= verify(reg_delete(to) == 0 &&
               reg_delete(from) == 0,
               "release both root objects");
  ok &= verify(g_registry.total_keys == before - 1401U,
               "mass mount and unmount restore exact object accounting");
  return ok;
}

static int test_mount_before_after_precedence(void) {
  const char *source = "/integrity/copy_priority_src";
  const char *target = "/integrity/copy_priority_dst";
  int ok = verify(reg_set("/integrity/copy_priority_src/shared",
                           val_int(111), RK_ALL) == 0,
                  "priority source shared");
  ok &= verify(reg_set("/integrity/copy_priority_dst/shared",
                       val_int(222), RK_ALL) == 0,
               "priority target shared");
  ok &= verify(reg_set("/integrity/copy_priority_src/extra",
                       val_int(333), RK_ALL) == 0,
               "priority source extra");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_mount(source, target, 0) == 0,
               "after-mount succeeds");
  Value v = reg_get("/integrity/copy_priority_dst/shared");
  ok &= verify(v.type == TYPE_INT && v.ival == 222,
               "after-mount does not replace a destination value");
  val_free(&v);
  v = reg_get("/integrity/copy_priority_dst/extra");
  ok &= verify(v.type == TYPE_INT && v.ival == 333,
               "after-mount includes absent descendants");
  val_free(&v);
  ok &= verify(g_registry.total_keys == before + 1U,
               "only new descendant increases object count");
  ok &= verify(reg_mount(source, target, 1) == 0,
               "before-mount succeeds");
  v = reg_get("/integrity/copy_priority_dst/shared");
  ok &= verify(v.type == TYPE_INT && v.ival == 111,
               "before-mount replaces source-overlapping value");
  val_free(&v);
  ok &= verify(g_registry.total_keys == before + 1U,
               "before-mount reuses existing objects");
  return ok;
}

static int test_bind_replaces_destination_children(void) {
  int ok = verify(reg_set("/integrity/bind_src/fresh", val_int(73),
                            RK_ALL) == 0, "bind source exists");
  ok &= verify(reg_set("/integrity/bind_dst/stale", val_int(9),
                       RK_ALL) == 0, "bind stale destination exists");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_bind("/integrity/bind_src",
                        "/integrity/bind_dst", 0) == 0,
               "bind replace succeeds");
  ok &= verify(reg_lookup("/integrity/bind_dst/stale") == NULL,
               "bind replaces old descendants without leaks");
  Value fresh = reg_get("/integrity/bind_dst/fresh");
  ok &= verify(fresh.type == TYPE_INT && fresh.ival == 73,
               "bind mounts new source value");
  val_free(&fresh);
  ok &= verify(g_registry.total_keys == before,
               "bind replacing one child with one keeps exact count");
  return ok;
}

static int test_overlap_rejections_are_nondestructive(void) {
  int ok = verify(reg_set("/integrity/overlap/nested/a",
                            val_int(72), RK_ALL) == 0,
                  "overlap scenario source admitted");
  size_t before = g_registry.total_keys;
  ok &= verify(reg_mount("/integrity/overlap",
                         "/integrity/overlap/nested", 1) != 0,
               "recursive self-mount rejected");
  ok &= verify(reg_mount("/", "/integrity/overlap", 1) != 0,
               "root subtree cannot be copied into its own descendants");
  ok &= verify(reg_bind("/integrity/overlap",
                        "/integrity/overlap/nested", 0) != 0,
               "recursive bind rejected");
  ok &= verify(reg_unmount("/integrity/overlap",
                           "/integrity/overlap") != 0,
               "unmounting from itself cannot invalidate iterator");
  ok &= verify(reg_bind("/integrity/overlap",
                        "/integrity/overlap", 0) == 0,
               "self-bind is an explicit no-op");
  Value original = reg_get("/integrity/overlap/nested/a");
  ok &= verify(original.type == TYPE_INT && original.ival == 72,
               "rejected overlaps preserve original contents");
  val_free(&original);
  ok &= verify(g_registry.total_keys == before,
               "rejected overlapping operations preserve object count");
  return ok;
}

static int test_mount_rejects_truncated_descendant(void) {
  int ok = verify(reg_set("/integrity/path_source/child",
                           val_int(8), RK_ALL) == 0,
                  "mount overflow source");
  char dst[REG_PATH_MAX];
  size_t offset = 0U;
  const char *prefix = "/integrity/";
  memcpy(dst, prefix, strlen(prefix));
  offset = strlen(prefix);
  while (offset + 3U < REG_PATH_MAX) {
    dst[offset++] = 'x';
    dst[offset++] = '/';
  }
  dst[offset - 1U] = 'q';
  dst[offset] = '\0';
  size_t before = g_registry.total_keys;
  ok &= verify(reg_mount("/integrity/path_source", dst, 1) != 0,
               "mount does not truncate any descendant path");
  ok &= verify(reg_lookup(dst) == NULL,
               "failed mount does not create placeholder destination");
  ok &= verify(g_registry.total_keys == before,
               "failed mount does not partially insert nodes");
  return ok;
}

static int test_delete_reclaims_queued_values(void) {
  int ok = verify(reg_ipc_init_queue("/integrity/queue",
                                     16) == 0,
                  "queue initialized on the canonical registry key");
  size_t before = g_registry.total_keys;
  for (size_t i = 0U; i < 8U; ++i)
    ok &= verify(reg_ipc_send("/integrity/queue",
                              val_int((int64_t)i)) == 0,
                 "queue contains independently owned messages");
  ok &= verify(reg_delete("/integrity/queue") == 0,
               "queue subtree release closes owned messages");
  ok &= verify(g_registry.total_keys == before - 1U,
               "queue deletion decrements object count exactly once");
  ok &= verify(reg_lookup("/integrity/queue") == NULL,
               "released queue is not found");
  return ok;
}

int main(void) {
  reg_init();
  if (!verify(reg_lookup("/") != NULL &&
              g_registry.total_keys >= 7U,
              "registry initialized"))
    return 2;
  if (!verify(reg_mkpath("/integrity", RK_ALL) != NULL,
              "test namespace admitted"))
    return 2;
  int ok = test_exact_path_rejection();
  ok &= test_oversized_segment_rejection();
  ok &= test_root_delete_forbidden();
  ok &= test_deep_subtree_delete();
  ok &= test_wide_subtree_delete();
  ok &= test_move_reject_collision_without_mutation();
  ok &= test_move_reject_descendant();
  ok &= test_move_reject_overlong_descendant_path();
  ok &= test_large_tree_move_updates_every_path();
  ok &= test_unmount_preserves_exact_counter();
  ok &= test_selective_unmount_preserves_count();
  ok &= test_iterative_mount_large_tree();
  ok &= test_mount_before_after_precedence();
  ok &= test_bind_replaces_destination_children();
  ok &= test_overlap_rejections_are_nondestructive();
  ok &= test_mount_rejects_truncated_descendant();
  ok &= test_delete_reclaims_queued_values();
  if (!ok || failures != 0U) {
    fprintf(stderr,"REGISTRY_INTEGRITY_RESULT_FAIL: checks=%u failures=%u\n",
            checks, failures);
    return 1;
  }
  printf("REGISTRY_INTEGRITY_CORE_PASS checks=%u remaining=%zu\n",
         checks, g_registry.total_keys);
  return 0;
}
