/*
 * registry/registry.c — Hierarchical Registry Implementation
 * ============================================================
 * The "regedit linker" — hierarchical key-value store.
 *
 * The legacy RegKey data model remains the only runtime registry while
 * NXOLMO StateStore/authority replace this implementation in place.
 * All mutations must preserve exact path identities and subtree ownership.
 * No fixed-width DFS stack, no silent truncation and no second name table.
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "include/nexs_registry.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

Registry g_registry;
static int g_scope_counter = 0;

/* NXOLMO state metadata is backed by provider pages, not by the legacy
 * language Buddy pool. This keeps small-memory language allocations isolated
 * and preserves the StateStore caller-owned-array contract. */
static size_t regkey_state_pages(size_t bytes) {
  if (bytes == 0U || bytes > SIZE_MAX - (NEXS_PAGE_SIZE - 1U))
    return 0U;
  return (bytes + NEXS_PAGE_SIZE - 1U) / NEXS_PAGE_SIZE;
}

static void *regkey_state_alloc(size_t bytes) {
  size_t pages = regkey_state_pages(bytes);
  return pages == 0U ? NULL : page_alloc(pages);
}

static void regkey_state_free(void *ptr, size_t bytes) {
  size_t pages = regkey_state_pages(bytes);
  if (ptr != NULL && pages != 0U)
    (void)page_free_checked(ptr, pages);
}

/* Runtime admission delegates identities, generations and topology to the
 * product NXOLMO StateStore. RegKey is only the local name/value projection.
 * The old pointer graph must be eliminated at the full P3 namespace cutover. */
static void regkey_state_storage_release(void) {
  if (g_registry.slots != NULL) {
    regkey_state_free(g_registry.slots,
              (size_t)g_registry.state.slot_capacity * sizeof(nx_state_slot));
    g_registry.slots = NULL;
  }
  if (g_registry.index != NULL) {
    regkey_state_free(g_registry.index,
              (size_t)g_registry.state.index_capacity * sizeof(uint32_t));
    g_registry.index = NULL;
  }
  if (g_registry.rehash_index != NULL) {
    regkey_state_free(g_registry.rehash_index,
              (size_t)g_registry.state.index_capacity * sizeof(uint32_t));
    g_registry.rehash_index = NULL;
  }
}

static int regkey_state_storage_init(void) {
  const uint32_t initial = 8u;
  const uint64_t store_id = UINT64_C(0x4e58535245473131);
  const uint64_t hash_key[2] = {
      UINT64_C(0x6e78726567535431), UINT64_C(0x6e78726567535432)};
  uint32_t index_capacity = 0u;
  nx_result result;
  if (nx_state_store_required_index_capacity(initial,
                                             &index_capacity) != NX_STATUS_OK)
    return -1;
  g_registry.slots = regkey_state_alloc((size_t)initial * sizeof(nx_state_slot));
  g_registry.index = regkey_state_alloc((size_t)index_capacity * sizeof(uint32_t));
  g_registry.rehash_index =
      regkey_state_alloc((size_t)index_capacity * sizeof(uint32_t));
  if (g_registry.slots == NULL || g_registry.index == NULL ||
      g_registry.rehash_index == NULL) {
    if (g_registry.slots != NULL)
      regkey_state_free(g_registry.slots, (size_t)initial * sizeof(nx_state_slot));
    if (g_registry.index != NULL)
      regkey_state_free(g_registry.index, (size_t)index_capacity * sizeof(uint32_t));
    if (g_registry.rehash_index != NULL)
      regkey_state_free(g_registry.rehash_index,
                (size_t)index_capacity * sizeof(uint32_t));
    g_registry.slots = NULL;
    g_registry.index = NULL;
    g_registry.rehash_index = NULL;
    return -1;
  }
  if (nx_state_store_init(&g_registry.state, g_registry.slots, initial,
                          g_registry.index, g_registry.rehash_index,
                          index_capacity, store_id, hash_key,
                          &result) != NX_STATUS_OK) {
    regkey_state_free(g_registry.slots, (size_t)initial * sizeof(nx_state_slot));
    regkey_state_free(g_registry.index, (size_t)index_capacity * sizeof(uint32_t));
    regkey_state_free(g_registry.rehash_index,
              (size_t)index_capacity * sizeof(uint32_t));
    g_registry.slots = NULL;
    g_registry.index = NULL;
    g_registry.rehash_index = NULL;
    return -1;
  }
  return 0;
}

static int regkey_state_storage_admit(void) {
  nx_state_store *state = &g_registry.state;
  if (state->free_head != NX_STATE_STORE_NO_SLOT)
    return 0;
  if (state->slot_capacity > UINT32_MAX / 2u)
    return -1;
  uint32_t new_capacity = state->slot_capacity * 2u;
  uint32_t index_capacity = 0u;
  if (nx_state_store_required_index_capacity(new_capacity,
                                             &index_capacity) != NX_STATUS_OK)
    return -1;
  size_t new_slots_bytes = (size_t)new_capacity * sizeof(nx_state_slot);
  size_t new_index_bytes = (size_t)index_capacity * sizeof(uint32_t);
  nx_state_slot *slots = regkey_state_alloc(new_slots_bytes);
  uint32_t *index = regkey_state_alloc(new_index_bytes);
  uint32_t *rehash_index = regkey_state_alloc(new_index_bytes);
  if (slots == NULL || index == NULL || rehash_index == NULL) {
    if (slots != NULL)
      regkey_state_free(slots, new_slots_bytes);
    if (index != NULL)
      regkey_state_free(index, new_index_bytes);
    if (rehash_index != NULL)
      regkey_state_free(rehash_index, new_index_bytes);
    return -1;
  }
  uint32_t old_capacity = state->slot_capacity;
  uint32_t old_index_capacity = state->index_capacity;
  nx_status status = nx_state_store_grow(state, slots, new_capacity, index,
                                         rehash_index, index_capacity);
  if (status != NX_STATUS_OK) {
    regkey_state_free(slots, new_slots_bytes);
    regkey_state_free(index, new_index_bytes);
    regkey_state_free(rehash_index, new_index_bytes);
    return -1;
  }
  regkey_state_free(g_registry.slots,
            (size_t)old_capacity * sizeof(nx_state_slot));
  regkey_state_free(g_registry.index,
            (size_t)old_index_capacity * sizeof(uint32_t));
  regkey_state_free(g_registry.rehash_index,
            (size_t)old_index_capacity * sizeof(uint32_t));
  g_registry.slots = slots;
  g_registry.index = index;
  g_registry.rehash_index = rehash_index;
  return 0;
}

static nx_state_node_spec regkey_state_spec(const RegKey *node,
                                            const RegKey *parent) {
  nx_state_node_spec spec;
  memset(&spec, 0, sizeof(spec));
  if (parent != NULL)
    spec.parent = parent->object;
  spec.type_id = (uint64_t)node->val.type + UINT64_C(1);
  spec.schema_id = UINT32_C(1);
  spec.schema_version = UINT16_C(1);
  spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
  if (node->val.type == TYPE_FN)
    spec.flags = NX_STATE_NODE_FLAG_EXECUTABLE;
  return spec;
}

static int regkey_state_create(RegKey *node, RegKey *parent) {
  nx_result result;
  if (regkey_state_storage_admit() != 0)
    return -1;
  nx_state_node_spec spec = regkey_state_spec(node, parent);
  nx_object_ref ref;
  if (nx_state_store_create(&g_registry.state, &spec, &ref, &result)
      != NX_STATUS_OK)
    return -1;
  node->object = ref;
  return 0;
}

static int regkey_state_update(RegKey *node, const RegKey *parent,
                               Value *updated_value) {
  nx_state_node current;
  nx_state_node updated;
  nx_result result;
  nx_status status = nx_state_store_read(&g_registry.state, node->object,
                                         &current, &result);
  if (status != NX_STATUS_OK)
    return -1;
  nx_state_node_spec spec = current.spec;
  if (parent != NULL)
    spec.parent = parent->object;
  if (updated_value != NULL) {
    spec.type_id = (uint64_t)updated_value->type + UINT64_C(1);
    spec.flags = updated_value->type == TYPE_FN
                     ? NX_STATE_NODE_FLAG_EXECUTABLE : 0u;
  }
  status = nx_state_store_update(&g_registry.state, node->object,
                                  current.revision, &spec, &updated, &result);
  return status == NX_STATUS_OK ? 0 : -1;
}

nx_status reg_object_read(const char *path, nx_state_node *node,
                          nx_result *result) {
  if (node == NULL || result == NULL)
    return NX_STATUS_INVALID_ARGUMENT;
  RegKey *key = reg_lookup(path);
  if (key == NULL)
    return NX_STATUS_NOT_FOUND;
  return nx_state_store_read(&g_registry.state, key->object, node, result);
}

/* =========================================================
   REGKEY ALLOCATION
   ========================================================= */

static RegKey *regkey_alloc(const char *name, const char *path,
                            uint8_t rights) {
  RegKey *k = nexs_alloc(sizeof(RegKey));
  if (k == NULL)
    return NULL;
  strncpy(k->name, name, NAME_LEN - 1);
  k->name[NAME_LEN - 1] = '\0';
  strncpy(k->path, path, REG_PATH_MAX - 1);
  k->path[REG_PATH_MAX - 1] = '\0';
  memset(&k->object, 0, sizeof(k->object));
  k->val = val_nil();
  k->rights = rights;
  k->parent = NULL;
  k->children = NULL;
  k->next = NULL;
  k->queue = NULL; /* IPC queue — initialised on demand */
  return k;
}

static RegKey *regkey_find_child(RegKey *parent, const char *name) {
  if (!parent || !name)
    return NULL;
  size_t scanned = 0U;
  for (RegKey *c = parent->children;
       c != NULL && scanned < g_registry.total_keys;
       c = c->next, ++scanned) {
    if (strcmp(c->name, name) == 0)
      return c;
  }
  return NULL;
}

static int regkey_add_child(RegKey *parent, RegKey *child) {
  if (parent == NULL || child == NULL)
    return -1;
  if (!nx_object_ref_is_valid(child->object) &&
      regkey_state_create(child, parent) != 0)
    return -1;
  child->parent = parent;
  child->next = parent->children;
  parent->children = child;
  ++g_registry.total_keys;
  return 0;
}

static int regkey_add_child_tail(RegKey *parent, RegKey *child) {
  if (parent == NULL || child == NULL)
    return -1;
  if (!nx_object_ref_is_valid(child->object) &&
      regkey_state_create(child, parent) != 0)
    return -1;
  RegKey **insertion = &parent->children;
  size_t inspected = 0U;
  while (*insertion != NULL && inspected < g_registry.total_keys) {
    insertion = &(*insertion)->next;
    ++inspected;
  }
  if (*insertion != NULL)
    return -1;
  child->parent = parent;
  child->next = NULL;
  *insertion = child;
  ++g_registry.total_keys;
  return 0;
}

/* Unlinking changes topology, NOT ownership or live object count.
 * An actual deletion decrements the count while freeing each object.
 * Moving a linked object changes no count; the existing add helper
 * increments exactly once, so the move balances that increment. */
static int regkey_detach_child(RegKey *parent, RegKey *child) {
  if (parent == NULL || child == NULL)
    return -1;
  RegKey **link = &parent->children;
  for (size_t checked = 0U;
       *link != NULL && checked < g_registry.total_keys; ++checked) {
    if (*link == child) {
      *link = child->next;
      child->next = NULL;
      child->parent = NULL;
      return 0;
    }
    link = &(*link)->next;
  }
  return -1;
}

/* Use existing parent/sibling links as the DFS traversal cursor; this is
 * the same actual registry tree, never an auxiliary object index. */
static RegKey *regkey_next_preorder(RegKey *root, RegKey *current) {
  if (root == NULL || current == NULL)
    return NULL;
  if (current->children != NULL)
    return current->children;
  RegKey *cursor = current;
  for (size_t examined = 0U;
       cursor != root && examined < g_registry.total_keys; ++examined) {
    if (cursor->next != NULL)
      return cursor->next;
    cursor = cursor->parent;
    if (cursor == NULL)
      return NULL;
  }
  return NULL;
}

/* Release queue resources exactly once before the containing node. */
static void regkey_release_owned(RegKey *key) {
  if (key == NULL)
    return;
  RegIpcQueue *queue = key->queue;
  if (queue != NULL) {
    MsgNode *entry = queue->head;
    for (size_t count = 0U;
         entry != NULL && count < (size_t)queue->count; ++count) {
      MsgNode *next = entry->next;
      val_free(&entry->msg);
      xfree(entry);
      entry = next;
    }
    if (queue->use_pipe) {
      if (queue->pipe_fd[0] >= 0)
        close(queue->pipe_fd[0]);
      if (queue->pipe_fd[1] >= 0)
        close(queue->pipe_fd[1]);
    }
    xfree(queue);
    key->queue = NULL;
  }
  val_free(&key->val);
  if (nx_object_ref_is_valid(key->object)) {
    nx_state_node node;
    nx_result result;
    if (nx_state_store_read(&g_registry.state, key->object, &node, &result)
        == NX_STATUS_OK)
      (void)nx_state_store_delete(&g_registry.state, key->object,
                                   node.revision, &result);
  }
  xfree(key);
}

/* Post-order O(1) auxiliary-space release. Each child is detached from
 * the parent's actual list when traversed; parent links remain available
 * until the last child is freed. No 1024-slot stack can silently discard
 * live descendants. Every successful free reduces total_keys once. */
static void regkey_free_recursive(RegKey *root) {
  if (root == NULL)
    return;
  RegKey *node = root;
  size_t budget = g_registry.total_keys;
  for (size_t released = 0U;
       node != NULL && released < budget;) {
    if (node->children != NULL) {
      RegKey *child = node->children;
      node->children = child->next;
      child->next = NULL;
      node = child;
    } else {
      int finished = node == root;
      RegKey *parent = node->parent;
      regkey_release_owned(node);
      if (g_registry.total_keys > 0U)
        --g_registry.total_keys;
      ++released;
      node = finished ? NULL : parent;
    }
  }
}

/* Verify the whole old subtree fits at its new location BEFORE unlinking.
 * The suffix length is already bounded by the original exact path ABI. */
static int regkey_subtree_relocation_fits(RegKey *root, const char *new_path) {
  if (root == NULL || new_path == NULL)
    return 0;
  size_t old_size = strlen(root->path);
  size_t new_size = strlen(new_path);
  RegKey *cursor = root;
  for (size_t checked = 0U;
       cursor != NULL && checked < g_registry.total_keys; ++checked) {
    size_t current_size = strlen(cursor->path);
    if (current_size < old_size ||
        strncmp(cursor->path, root->path, old_size) != 0 ||
        (current_size > old_size && cursor->path[old_size] != '/') ||
        new_size >= REG_PATH_MAX ||
        current_size - old_size >= REG_PATH_MAX - new_size)
      return 0;
    cursor = regkey_next_preorder(root, cursor);
  }
  return cursor == NULL;
}

/* This operation receives a fully prevalidated move. Rewrites the one
 * canonical path in each existing node, without temporary DFS stacks. */
static void regkey_update_paths(RegKey *root, const char *old_path,
                                const char *new_path) {
  if (root == NULL)
    return;
  size_t old_size = strlen(old_path);
  RegKey *cursor = root;
  for (size_t updated = 0U;
       cursor != NULL && updated < g_registry.total_keys; ++updated) {
    char target[REG_PATH_MAX];
    size_t prefix = strlen(new_path);
    size_t suffix = strlen(cursor->path) - old_size;
    memcpy(target, new_path, prefix);
    memcpy(target + prefix, cursor->path + old_size, suffix + 1U);
    memcpy(cursor->path, target, prefix + suffix + 1U);
    cursor = regkey_next_preorder(root, cursor);
  }
}

/* Exact path admission, currently bounded by the shared RegKey ABI.
 * Paths and segments must never be silently shortened; the eventual
 * StateStore migration will remove the fixed-buffer representation.
 * This check is independent of caller trust and authorization. */
int reg_path_valid(const char *path) {
  if (path == NULL || path[0] != '/')
    return 0;
  size_t segment_size = 0U;
  for (size_t i = 0U; i < REG_PATH_MAX; ++i) {
    const char ch = path[i];
    if (ch == '\0' || ch == '/') {
      /* The direct filesystem and RegKey path parser have identical
       * traversal rules. Never store dot aliases as distinct objects. */
      if ((segment_size == 1U && path[i - 1U] == '.') ||
          (segment_size == 2U && path[i - 1U] == '.' &&
           path[i - 2U] == '.'))
        return 0;
      if (ch == '\0')
        return i > 0U && (i == 1U || path[i - 1U] != '/');
      if (i > 0U && segment_size == 0U)
        return 0;
      segment_size = 0U;
    } else {
      ++segment_size;
      if (segment_size >= NAME_LEN)
        return 0;
    }
  }
  return 0;
}

/* =========================================================
   CORE OPERATIONS
   ========================================================= */

RegKey *reg_mkpath(const char *path, uint8_t rights) {
  /* Preflight the complete path before publishing any new object. */
  if (!reg_path_valid(path) || g_registry.root == NULL)
    return NULL;
  size_t length = strlen(path);
  RegKey *current = g_registry.root;
  RegKey *new_branch = NULL;
  for (size_t start = 1U; start < length && current != NULL;) {
    size_t end = start;
    while (end < length && path[end] != '/')
      ++end;
    size_t component_size = end - start;
    char component[NAME_LEN];
    memcpy(component, path + start, component_size);
    component[component_size] = '\0';

    RegKey *child = regkey_find_child(current, component);
    if (child == NULL) {
      char full_path[REG_PATH_MAX];
      memcpy(full_path, path, end);
      full_path[end] = '\0';
      child = regkey_alloc(component, full_path, rights);
      if (child == NULL) {
        /* The first newly linked node owns the newly created suffix.
         * On OOM remove it once; no partial path survives. */
        if (new_branch != NULL && new_branch->parent != NULL &&
            regkey_detach_child(new_branch->parent, new_branch) == 0)
          regkey_free_recursive(new_branch);
        return NULL;
      }
      if (regkey_add_child(current, child) != 0) {
        regkey_release_owned(child);
        if (new_branch != NULL && new_branch->parent != NULL &&
            regkey_detach_child(new_branch->parent, new_branch) == 0)
          regkey_free_recursive(new_branch);
        return NULL;
      }
      if (new_branch == NULL)
        new_branch = child;
    }
    current = child;
    start = end + 1U;
  }
  return current;
}

RegKey *reg_lookup(const char *path) {
  if (!reg_path_valid(path) || g_registry.root == NULL)
    return NULL;
  size_t length = strlen(path);
  RegKey *current = g_registry.root;
  for (size_t start = 1U; start < length && current != NULL;) {
    size_t end = start;
    while (end < length && path[end] != '/')
      ++end;
    size_t component_size = end - start;
    char component[NAME_LEN];
    memcpy(component, path + start, component_size);
    component[component_size] = '\0';
    current = regkey_find_child(current, component);
    start = end + 1U;
  }
  return current;
}

/* Resolve a name using ONE canonical namespace traversal. Ordinary value
 * references accept any type; function calls select only typed callable
 * Values. A local integer named "type" must not erase /sys/type() from
 * the language's callable namespace. No function table is consulted. */
static int reg_matches_value_type(const RegKey *key, int required_type) {
  return key != NULL &&
         (required_type < 0 || key->val.type == (ValueType)required_type);
}

static RegKey *reg_resolve_filtered(const char *name,
                                    const char *scope_path,
                                    int required_type) {
  if (name == NULL || name[0] == '\0')
    return NULL;
  char path[REG_PATH_MAX];

  /* Current scope and ancestors, preserving lexical search order.
   * Wrongly typed bindings do not shadow independently named callables. */
  char sp[REG_PATH_MAX];
  strncpy(sp, scope_path ? scope_path : REG_LOCAL, REG_PATH_MAX - 1);
  sp[REG_PATH_MAX - 1] = '\0';
  /* Every ascent strictly shortens the bounded registry path. Explicit
   * byte budget prevents an accidental cycle without a goto or break. */
  for (size_t depth = 0U; depth < REG_PATH_MAX && sp[0] != '\0'; ++depth) {
    int written = snprintf(path, sizeof(path), "%s/%s",
                           strcmp(sp, REG_ROOT) == 0 ? "" : sp, name);
    if (written >= 0 && (size_t)written < sizeof(path)) {
      RegKey *key = reg_lookup(path);
      if (reg_matches_value_type(key, required_type))
        return key;
    }
    if (strcmp(sp, "/") == 0) {
      sp[0] = '\0';
    } else {
      char parent[REG_PATH_MAX];
      nexs_path_dirname(sp, parent, sizeof(parent));
      if (parent[0] == '\0' || strcmp(parent, sp) == 0) {
        sp[0] = '\0';
      } else {
        strncpy(sp, parent, REG_PATH_MAX - 1U);
        sp[REG_PATH_MAX - 1U] = '\0';
      }
    }
  }

  /* Canonical user callables, then system callables. */
  const char *roots[] = {REG_FN, REG_SYS};
  for (size_t root = 0U; root < 2U; ++root) {
    int written = snprintf(path, sizeof(path), "%s/%s", roots[root], name);
    if (written >= 0 && (size_t)written < sizeof(path)) {
      RegKey *key = reg_lookup(path);
      if (reg_matches_value_type(key, required_type))
        return key;
    }
  }

  /* Modules remain within the same namespace and use the same type gate. */
  RegKey *mod_root = reg_lookup(REG_MOD);
  if (mod_root != NULL) {
    /* Bound the linked-list walk by actual admitted registry objects;
     * cyclic or damaged links cannot create an unbounded traversal. */
    size_t scanned = 0U;
    for (RegKey *mod = mod_root->children;
         mod != NULL && scanned < g_registry.total_keys;
         mod = mod->next, ++scanned) {
      int written = snprintf(path, sizeof(path), "%s/%s/%s",
                             REG_MOD, mod->name, name);
      if (written >= 0 && (size_t)written < sizeof(path)) {
        RegKey *key = reg_lookup(path);
        if (reg_matches_value_type(key, required_type))
          return key;
      }
    }
  }
  return NULL;
}

RegKey *reg_resolve(const char *name, const char *scope_path) {
  return reg_resolve_filtered(name, scope_path, -1);
}

RegKey *reg_resolve_callable(const char *name, const char *scope_path) {
  return reg_resolve_filtered(name, scope_path, TYPE_FN);
}

int reg_set(const char *path, Value val, uint8_t rights) {
  if (!reg_path_valid(path))
    return -1;
  /* Clone BEFORE creating a key or replacing a live value. A failed
   * admission is never allowed to publish an error in place of a value. */
  Value copy = val_nil();
  if (val_clone_checked(&val, &copy) != 0)
    return -1;
  RegKey *k = reg_lookup(path);
  if (k == NULL)
    k = reg_mkpath(path, rights);
  if (k == NULL) {
    val_free(&copy);
    return -1;
  }
  if (regkey_state_update(k, NULL, &copy) != 0) {
    val_free(&copy);
    return -1;
  }
  Value old = k->val;
  k->val = copy;
  k->rights = rights;
  val_free(&old);
  return 0;
}

Value reg_get(const char *path) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return val_err(3, "registry key not found");
  return val_clone(&k->val);
}

int reg_delete(const char *path) {
  RegKey *key = reg_lookup(path);
  /* Root is the namespace anchor: it must never become dangling. */
  if (key == NULL || key->parent == NULL)
    return -1;
  if (regkey_detach_child(key->parent, key) != 0)
    return -1;
  regkey_free_recursive(key);
  return 0;
}

/* =========================================================
   POINTER EXTENSIONS
   ========================================================= */

int reg_set_ptr(const char *path, const char *target) {
  if (path == NULL || target == NULL)
    return -1;
  Value pointer = val_ptr(target);
  if (pointer.type != TYPE_PTR) {
    val_free(&pointer);
    return -1;
  }
  int status = reg_set(path, pointer, RK_ALL);
  val_free(&pointer);
  return status;
}

Value reg_get_deref(const char *path) {
  if (!path)
    return val_err(3, "reg_get_deref: NULL path");

  /* Follow pointer chain (max 64 hops to avoid infinite loops) */
  const char *cur = path;
  char buf[REG_PATH_MAX];
  int hops = 0;

  while (hops < 64) {
    RegKey *k = reg_lookup(cur);
    if (!k)
      return val_err(3, "reg_get_deref: key not found");
    if (k->val.type != TYPE_PTR)
      return val_clone(&k->val); /* reached non-pointer: return value */
    if (!k->val.data)
      return val_err(3, "reg_get_deref: NULL pointer target");
    strncpy(buf, (char *)k->val.data, REG_PATH_MAX - 1);
    buf[REG_PATH_MAX - 1] = '\0';
    cur = buf;
    hops++;
  }
  return val_err(3, "reg_get_deref: pointer chain too deep (circular?)");
}

/* =========================================================
   LISTING
   ========================================================= */

static void reg_ls_node(RegKey *k, FILE *out, int depth, int recursive) {
  if (!k)
    return;
  for (int i = 0; i < depth; i++)
    nexs_fprintf(out, "  ");

  /* Count children for display */
  int n_children = 0;
  for (RegKey *c = k->children; c; c = c->next)
    n_children++;

  nexs_fprintf(out, "%-28s  %-6s", k->name, val_type_name(k->val.type));

  if (k->val.type != TYPE_NIL) {
    nexs_fprintf(out, "  = ");
    /* Arrays: show element count instead of full dump (too wide) */
    if (k->val.type == TYPE_ARR && k->val.data) {
      DynArray *arr = (DynArray *)k->val.data;
      nexs_fprintf(out, "arr[%zu elements]", arr->size);
    } else {
      val_print(&k->val, out);
    }
    /* Pointer: show resolved value */
    if (k->val.type == TYPE_PTR && k->val.data) {
      Value resolved = reg_get_deref(k->path);
      if (resolved.type != TYPE_ERR) {
        nexs_fprintf(out, "  [→ ");
        val_print(&resolved, out);
        nexs_fprintf(out, "]");
      }
      val_free(&resolved);
    }
  } else if (n_children > 0) {
    /* Container node with children: show count so user knows to recurse */
    nexs_fprintf(out, "  (%d keys)", n_children);
  }

  /* IPC queue indicator */
  if (k->queue) {
    int pending = k->queue->use_pipe ? -1 : k->queue->count;
    if (pending >= 0)
      nexs_fprintf(out, "  [Q:%d/%d]", pending, k->queue->max_count);
    else
      nexs_fprintf(out, "  [Q:pipe]");
  }

  nexs_fprintf(out, "\n");
  if (recursive) {
    for (RegKey *c = k->children; c; c = c->next)
      reg_ls_node(c, out, depth + 1, 1);
  }
}

void reg_ls(const char *path, FILE *out) {
  RegKey *k = reg_lookup(path);
  if (!k) {
    nexs_fprintf(out, "Path '%s' not found\n", path);
    return;
  }

  nexs_fprintf(out, "\n[REGISTRY] %s\n", path);
  nexs_fprintf(out, "%-28s  %-6s  %s\n", "NAME", "TYPE", "VALUE");
  nexs_fprintf(out, "---------------------------------------------------\n");

  /* The actual tree is the only index. Bound traversal against the
   * admitted key count before allocating a transient sorting buffer. */
  size_t count = 0U;
  RegKey *cursor = k->children;
  while (cursor != NULL && count < g_registry.total_keys) {
    ++count;
    cursor = cursor->next;
  }
  if (cursor != NULL || count > SIZE_MAX / sizeof(RegKey *)) {
    nexs_fprintf(out, "[REGISTRY] invalid child traversal\n");
    return;
  }

  if (count > 0U) {
    size_t bytes = count * sizeof(RegKey *);
    RegKey **list = nexs_alloc(bytes);
    if (list == NULL) {
      nexs_fprintf(out, "[REGISTRY] listing unavailable: provider exhausted\n");
      return;
    }
    cursor = k->children;
    for (size_t i = 0U; i < count; ++i) {
      list[i] = cursor;
      cursor = cursor->next;
    }

    /* Stable order without allocating a second index or unsafe recursion. */
    for (size_t j = 0U; j < count - 1U; ++j) {
      for (size_t l = 0U; l < count - j - 1U; ++l) {
        if (strcmp(list[l]->name, list[l + 1U]->name) > 0) {
          RegKey *swap = list[l];
          list[l] = list[l + 1U];
          list[l + 1U] = swap;
        }
      }
    }

    for (size_t j = 0U; j < count; ++j)
      reg_ls_node(list[j], out, 0, 0);
    nexs_free(list, bytes);
  }

  nexs_fprintf(out, "\n");
}

void reg_ls_recursive(const char *path, FILE *out, int depth) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return;
  reg_ls_node(k, out, depth, 1);
}

void reg_key_print(RegKey *k, FILE *out) {
  if (!k)
    return;
  nexs_fprintf(out, "[%s] type=%s rights=%02x", k->path,
               val_type_name(k->val.type), k->rights);
  if (k->val.type != TYPE_NIL) {
    nexs_fprintf(out, " val=");
    val_print(&k->val, out);
  }
  nexs_fprintf(out, "\n");
}

/* =========================================================
   MOVE / MOUNT / UNMOUNT / BIND
   ========================================================= */

int reg_move(const char *src, const char *dst) {
  if (!reg_path_valid(src) || !reg_path_valid(dst) ||
      strcmp(src, "/") == 0 || strcmp(dst, "/") == 0)
    return -1;
  RegKey *source = reg_lookup(src);
  if (source == NULL || source->parent == NULL)
    return -1;
  if (strcmp(src, dst) == 0)
    return 0;

  /* A move must not alias a destination, orphan the old source or
   * make a cycle. All rejections happen before any tree mutation. */
  if (reg_lookup(dst) != NULL)
    return -1;
  size_t src_length = strlen(src);
  if (strncmp(dst, src, src_length) == 0 &&
      (dst[src_length] == '/' || dst[src_length] == '\0'))
    return -1;
  if (!regkey_subtree_relocation_fits(source, dst))
    return -1;

  char parent_path[REG_PATH_MAX];
  nexs_path_dirname(dst, parent_path, sizeof(parent_path));
  RegKey *new_parent = reg_lookup(parent_path);
  if (new_parent == NULL)
    new_parent = reg_mkpath(parent_path, RK_ALL);
  if (new_parent == NULL)
    return -1;
  const char *basename = nexs_path_basename(dst);
  if (basename[0] == '\0')
    return -1;

  char previous_path[REG_PATH_MAX];
  memcpy(previous_path, source->path, strlen(source->path) + 1U);
  if (regkey_state_update(source, new_parent, NULL) != 0)
    return -1;
  if (regkey_detach_child(source->parent, source) != 0)
    return -1;
  memcpy(source->name, basename, strlen(basename) + 1U);
  if (regkey_add_child(new_parent, source) != 0)
    return -1;
  /* The key already existed; linking it must not change the count. */
  --g_registry.total_keys;
  regkey_update_paths(source, previous_path, dst);
  return 0;
}


/* Exact paths must not overlap for a tree COPY. Without a detached
 * snapshot, modifying a source ancestor/descendant during iteration can
 * create an unbounded self-referential copy. Fail before any mutation. */
static int regkey_paths_overlap(const char *left, const char *right) {
  if (strcmp(left, "/") == 0 || strcmp(right, "/") == 0)
    return 1;
  size_t a = strlen(left);
  size_t b = strlen(right);
  int left_ancestor = strncmp(left, right, a) == 0 &&
                      (right[a] == '/' || right[a] == '\0');
  int right_ancestor = strncmp(right, left, b) == 0 &&
                       (left[b] == '/' || left[b] == '\0');
  return left_ancestor || right_ancestor;
}

/* Map a descendant to its mirrored canonical path using the original
 * source's immutable path suffix, never truncating an intermediate buffer. */
static int regkey_mount_path(const RegKey *root, const RegKey *descendant,
                             const char *destination, char output[REG_PATH_MAX]) {
  if (root == NULL || descendant == NULL || destination == NULL)
    return -1;
  size_t prefix = strlen(root->path);
  const char *suffix = descendant->path + prefix;
  int written;
  if (strcmp(destination, "/") == 0)
    written = snprintf(output, REG_PATH_MAX, "%s", suffix);
  else
    written = snprintf(output, REG_PATH_MAX, "%s%s", destination, suffix);
  return written >= 0 && written < REG_PATH_MAX &&
         reg_path_valid(output) ? 0 : -1;
}

/* Reuse one canonical RegKey for every mirrored node. Existing destinations
 * obey before/after preference while traversal always copies descendants.
 * A clone is fully admitted before replacing the old Value. */
static int regkey_mount_one(const RegKey *source, const char *destination,
                             int before) {
  RegKey *target = reg_lookup(destination);
  if (target != NULL && !before)
    return 0;

  Value value = val_clone(&source->val);
  if (value.type == TYPE_ERR && source->val.type != TYPE_ERR) {
    val_free(&value);
    return -1;
  }
  if (target == NULL) {
    char parent_path[REG_PATH_MAX];
    nexs_path_dirname(destination, parent_path, sizeof(parent_path));
    RegKey *parent = reg_lookup(parent_path);
    if (parent == NULL) {
      val_free(&value);
      return -1;
    }
    target = regkey_alloc(nexs_path_basename(destination),
                          destination, source->rights);
    if (target == NULL) {
      val_free(&value);
      return -1;
    }
    target->val = value;
    if ((before && regkey_add_child(parent, target) != 0) ||
        (!before && regkey_add_child_tail(parent, target) != 0)) {
      regkey_release_owned(target);
      return -1;
    }
    return 0;
  }
  if (regkey_state_update(target, NULL, &value) != 0) {
    val_free(&value);
    return -1;
  }
  Value old = target->val;
  target->val = value;
  target->rights = source->rights;
  val_free(&old);
  return 0;
}

int reg_mount(const char *src_path, const char *dst_path, int before) {
  if (!reg_path_valid(src_path) || !reg_path_valid(dst_path))
    return -1;
  RegKey *source = reg_lookup(src_path);
  if (source == NULL)
    return -1;
  if (strcmp(src_path, dst_path) == 0)
    return 0;
  /* Current RegKey cannot provide transactionally consistent concurrent
   * in-place subtree overlays. Reject overlap until NXOLMO MVCC exists. */
  if (regkey_paths_overlap(src_path, dst_path) ||
      !regkey_subtree_relocation_fits(source, dst_path))
    return -1;
  RegKey *destination = reg_lookup(dst_path);
  if (destination == NULL)
    destination = reg_mkpath(dst_path, RK_ALL);
  if (destination == NULL)
    return -1;

  /* No recursion and no temporary stack: source parent/sibling links
   * are stable because the destination is not inside the source tree. */
  RegKey *cursor = regkey_next_preorder(source, source);
  for (size_t visited = 0U;
       cursor != NULL && visited < g_registry.total_keys; ++visited) {
    char mapped[REG_PATH_MAX];
    if (regkey_mount_path(source, cursor, dst_path, mapped) != 0 ||
        regkey_mount_one(cursor, mapped, before) != 0)
      return -1;
    cursor = regkey_next_preorder(source, cursor);
  }
  return cursor == NULL ? 0 : -1;
}

int reg_unmount(const char *src_path, const char *dst_path) {
  if (!reg_path_valid(dst_path) ||
      (src_path != NULL &&
       (!reg_path_valid(src_path) ||
        regkey_paths_overlap(src_path, dst_path))))
    return -1;
  RegKey *dst = reg_lookup(dst_path);
  if (!dst)
    return -1;

  if (!src_path) {
    RegKey *child = dst->children;
    while (child) {
      RegKey *next = child->next;
      regkey_free_recursive(child);
      child = next;
    }
    dst->children = NULL;
    return 0;
  }

  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;

  for (RegKey *sc = src->children; sc; sc = sc->next) {
    RegKey *target = regkey_find_child(dst, sc->name);
    if (target) {
      if (regkey_detach_child(dst, target) != 0)
        return -1;
      regkey_free_recursive(target);
    }
  }
  return 0;
}

int reg_bind(const char *src_path, const char *dst_path, int flag) {
  if (!reg_path_valid(src_path) || !reg_path_valid(dst_path))
    return -1;
  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;
  if (strcmp(src_path, dst_path) == 0)
    return 0;
  if (regkey_paths_overlap(src_path, dst_path) ||
      !regkey_subtree_relocation_fits(src, dst_path))
    return -1;

  if (flag == 0) {
    /* Clone and admit the canonical typed state before retiring any child.
     * Do not publish a failed clone as an actual registry value. */
    Value copy = val_nil();
    if (val_clone_checked(&src->val, &copy) != 0)
      return -1;
    RegKey *dst = reg_lookup(dst_path);
    if (dst == NULL)
      dst = reg_mkpath(dst_path, RK_ALL);
    if (dst == NULL || regkey_state_update(dst, NULL, &copy) != 0) {
      val_free(&copy);
      return -1;
    }

    RegKey *child = dst->children;
    while (child) {
      RegKey *next = child->next;
      regkey_free_recursive(child);
      child = next;
    }
    dst->children = NULL;
    Value old = dst->val;
    dst->val = copy;
    val_free(&old);
    dst->rights = src->rights;
    return reg_mount(src_path, dst_path, 1);
  }

  return reg_mount(src_path, dst_path, flag == 1 ? 1 : 0);
}

/* =========================================================
   SCOPE MANAGEMENT
   ========================================================= */

char *reg_push_scope(const char *fn_name) {
  if (g_scope_counter == INT_MAX)
    return NULL;
  char *buf = nexs_alloc(REG_PATH_MAX);
  if (buf == NULL)
    return NULL;
  int written = snprintf(buf, REG_PATH_MAX, "%s/%s_%d", REG_LOCAL,
                         fn_name ? fn_name : "anon", g_scope_counter);
  if (written < 0 || (size_t)written >= REG_PATH_MAX ||
      reg_mkpath(buf, RK_ALL) == NULL) {
    nexs_free(buf, REG_PATH_MAX);
    return NULL;
  }
  ++g_scope_counter;
  return buf;
}

void reg_pop_scope(const char *scope_path) {
  if (scope_path)
    reg_delete(scope_path);
}

/* =========================================================
   INITIALISATION
   ========================================================= */

int reg_init(void) {
  /* Reinitializing over a live object graph silently loses ownership.
   * The eventual RuntimeInstance will control explicit store lifetime. */
  if (g_registry.root != NULL || g_registry.total_keys != 0U)
    return -1;
  if (regkey_state_storage_init() != 0)
    return -1;
  RegKey *root = regkey_alloc("/", "/", RK_ALL);
  if (root == NULL) {
    regkey_state_storage_release();
    return -1;
  }
  if (regkey_state_create(root, NULL) != 0) {
    regkey_release_owned(root);
    regkey_state_storage_release();
    return -1;
  }
  g_registry.root = root;
  g_registry.total_keys = 1U;
  if (reg_mkpath(REG_LOCAL, RK_ALL) == NULL ||
      reg_mkpath(REG_FN, RK_READ | RK_EXEC) == NULL ||
      reg_mkpath(REG_SYS, RK_READ | RK_EXEC) == NULL ||
      reg_mkpath(REG_MOD, RK_ALL) == NULL ||
      reg_mkpath(REG_ENV, RK_ALL) == NULL ||
      reg_mkpath(REG_TYPE, RK_READ | RK_ADMIN) == NULL) {
    regkey_free_recursive(root);
    regkey_state_storage_release();
    g_registry.root = NULL;
    g_registry.total_keys = 0U;
    return -1;
  }
  return 0;
}
