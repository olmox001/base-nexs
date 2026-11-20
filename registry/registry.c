/*
 * registry/registry.c — Hierarchical Registry Implementation
 * ============================================================
 * The "regedit linker" — hierarchical key-value store.
 *
 * Key changes vs original:
 *   - RegKey includes 'queue' field (initialised to NULL)
 *   - regkey_free_recursive is now iterative (explicit stack, max 1024)
 *   - regkey_update_paths is now iterative
 *   - reg_set_ptr and reg_get_deref added
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "include/nexs_registry.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

Registry g_registry;
static int g_scope_counter = 0;

/* =========================================================
   REGKEY ALLOCATION
   ========================================================= */

static RegKey *regkey_alloc(const char *name, const char *path,
                            uint8_t rights) {
  RegKey *k = xmalloc(sizeof(RegKey));
  strncpy(k->name, name, NAME_LEN - 1);
  k->name[NAME_LEN - 1] = '\0';
  strncpy(k->path, path, REG_PATH_MAX - 1);
  k->path[REG_PATH_MAX - 1] = '\0';
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
  for (RegKey *c = parent->children; c; c = c->next) {
    if (strcmp(c->name, name) == 0)
      return c;
  }
  return NULL;
}

static void regkey_add_child(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  child->parent = parent;
  child->next = parent->children;
  parent->children = child;
  g_registry.total_keys++;
}

static void regkey_add_child_tail(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  child->parent = parent;
  child->next = NULL;
  if (!parent->children) {
    parent->children = child;
  } else {
    RegKey *last = parent->children;
    while (last->next)
      last = last->next;
    last->next = child;
  }
  g_registry.total_keys++;
}

static void regkey_detach_child(RegKey *parent, RegKey *child) {
  if (!parent || !child)
    return;
  RegKey **pp = &parent->children;
  while (*pp && *pp != child)
    pp = &(*pp)->next;
  if (*pp) {
    *pp = child->next;
    child->next = NULL;
    child->parent = NULL;
    g_registry.total_keys--;
  }
}

/* =========================================================
   ITERATIVE FREE (avoids stack overflow on deep trees)
   ========================================================= */

static void regkey_free_recursive(RegKey *root) {
  if (!root)
    return;

#define FREE_STACK_MAX REG_MAX_STACK_DEPTH
  RegKey **stack = xmalloc(sizeof(RegKey *) * FREE_STACK_MAX);
  if (!stack)
    return; /* Should not happen with xmalloc (dies on OOM) */

  int top = 0;
  stack[top++] = root;

  while (top > 0) {
    RegKey *k = stack[--top];

    /* Push all children */
    RegKey *c = k->children;
    while (c) {
      RegKey *nx = c->next;
      if (top < FREE_STACK_MAX)
        stack[top++] = c;
      c = nx;
    }

    /* Free IPC queue if present */
    if (k->queue) {
      MsgNode *m = k->queue->head;
      while (m) {
        MsgNode *mn = m->next;
        val_free(&m->msg);
        xfree(m);
        m = mn;
      }
      /* Close pipe fds if pipe transport was active */
      if (k->queue->use_pipe) {
        if (k->queue->pipe_fd[0] >= 0)
          close(k->queue->pipe_fd[0]);
        if (k->queue->pipe_fd[1] >= 0)
          close(k->queue->pipe_fd[1]);
      }
      xfree(k->queue);
      k->queue = NULL;
    }

    val_free(&k->val);
    xfree(k);
    if (g_registry.total_keys > 0)
      g_registry.total_keys--;
  }
  xfree(stack);
#undef FREE_STACK_MAX
}

/* =========================================================
   ITERATIVE PATH UPDATE (avoids stack overflow)
   ========================================================= */

static void regkey_update_paths(RegKey *k, const char *new_parent_path) {
  if (!k)
    return;

  /* First, update the root of the move/update operation */
  if (strcmp(new_parent_path, "/") == 0)
    snprintf(k->path, REG_PATH_MAX, "/%s", k->name);
  else
    snprintf(k->path, REG_PATH_MAX, "%s/%s", new_parent_path, k->name);

  /* Iterative DFS using a dynamic stack from heap */
#define UPD_STACK_MAX REG_MAX_STACK_DEPTH
  RegKey **stack = xmalloc(sizeof(RegKey *) * UPD_STACK_MAX);
  if (!stack)
    return;

  int top = 0;

  /* Push children of the root */
  for (RegKey *child = k->children; child; child = child->next) {
    if (top < UPD_STACK_MAX)
      stack[top++] = child;
  }

  while (top > 0) {
    RegKey *cur = stack[--top];

    /* Update current node path using its already-updated parent */
    if (cur->parent) {
      if (strcmp(cur->parent->path, "/") == 0)
        snprintf(cur->path, REG_PATH_MAX, "/%s", cur->name);
      else
        snprintf(cur->path, REG_PATH_MAX, "%s/%s", cur->parent->path,
                 cur->name);
    }

    /* Push children */
    for (RegKey *child = cur->children; child; child = child->next) {
      if (top < UPD_STACK_MAX) {
        stack[top++] = child;
      }
    }
  }
  xfree(stack);
#undef UPD_STACK_MAX
}

/* =========================================================
   CORE OPERATIONS
   ========================================================= */

RegKey *reg_mkpath(const char *path, uint8_t rights) {
  if (!path || path[0] != '/')
    return NULL;

  RegKey *cur = g_registry.root;
  char buf[REG_PATH_MAX];
  strncpy(buf, path, REG_PATH_MAX - 1);
  buf[REG_PATH_MAX - 1] = '\0';

  char *seg = strtok(buf + 1, "/");
  char built[REG_PATH_MAX] = "/";
  built[REG_PATH_MAX - 1] = '\0';

  while (seg) {
    char child_path[REG_PATH_MAX];
    if (strcmp(built, "/") == 0)
      snprintf(child_path, sizeof(child_path), "/%s", seg);
    else
      snprintf(child_path, sizeof(child_path), "%s/%s", built, seg);

    RegKey *child = regkey_find_child(cur, seg);
    if (!child) {
      /* if (g_nexs_debug)
        nexs_fprintf(stderr, "[REG DEBUG] Creating key: %s\n", child_path); */
      child = regkey_alloc(seg, child_path, rights);
      regkey_add_child(cur, child);
    }
    strncpy(built, child_path, REG_PATH_MAX - 1);
    built[REG_PATH_MAX - 1] = '\0';
    cur = child;
    seg = strtok(NULL, "/");
  }
  return cur;
}

RegKey *reg_lookup(const char *path) {
  if (!path || path[0] == '\0')
    return NULL;
  /* if (g_nexs_debug)
    nexs_fprintf(stderr, "[REG DEBUG] Lookup: %s\n", path); */
  if (strcmp(path, "/") == 0)
    return g_registry.root;
  if (path[0] != '/')
    return NULL;

  if (!g_registry.root)
    return NULL;

  char buf[REG_PATH_MAX];
  strncpy(buf, path, REG_PATH_MAX - 1);
  buf[REG_PATH_MAX - 1] = '\0';

  RegKey *cur = g_registry.root;
  char *p = buf + 1;
  while (*p && cur) {
    char *next_slash = strchr(p, '/');
    if (next_slash)
      *next_slash = '\0';

    if (*p != '\0') {
      cur = regkey_find_child(cur, p);
    }

    if (next_slash) {
      p = next_slash + 1;
    } else {
      break;
    }
  }
  return cur;
}

RegKey *reg_resolve(const char *name, const char *scope_path) {
  if (!name || name[0] == '\0')
    return NULL;
  char path[REG_PATH_MAX];

  /* 1. Current scope and ancestors */
  char sp[REG_PATH_MAX];
  strncpy(sp, scope_path ? scope_path : REG_LOCAL, REG_PATH_MAX - 1);
  sp[REG_PATH_MAX - 1] = '\0';
  while (1) {
    snprintf(path, sizeof(path), "%s/%s", sp, name);
    RegKey *k = reg_lookup(path);
    if (k)
      return k;
    if (strcmp(sp, "/") == 0)
      break;
    char parent[REG_PATH_MAX];
    nexs_path_dirname(sp, parent, sizeof(parent));
    strncpy(sp, parent, REG_PATH_MAX - 1);
    sp[REG_PATH_MAX - 1] = '\0';
  }

  /* 2. /fn/ */
  snprintf(path, sizeof(path), "%s/%s", REG_FN, name);
  RegKey *k = reg_lookup(path);
  if (k)
    return k;

  /* 3. /sys/ */
  snprintf(path, sizeof(path), "%s/%s", REG_SYS, name);
  k = reg_lookup(path);
  if (k)
    return k;

  /* 4. /mod/[all] */
  RegKey *mod_root = reg_lookup(REG_MOD);
  if (mod_root) {
    for (RegKey *mod = mod_root->children; mod; mod = mod->next) {
      snprintf(path, sizeof(path), "%s/%s/%s", REG_MOD, mod->name, name);
      k = reg_lookup(path);
      if (k)
        return k;
    }
  }

  return NULL;
}

int reg_set(const char *path, Value val, uint8_t rights) {
  if (!path)
    return -1;
  RegKey *k = reg_lookup(path);
  if (!k)
    k = reg_mkpath(path, rights);
  if (!k)
    return -1;

  /* Save old value for safe replacement (self-assignment protection) */
  Value old = k->val;
  k->val = val_clone(&val);
  val_free(&old);

  k->rights = rights;
  return 0;
}

Value reg_get(const char *path) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return val_err(3, "registry key not found");
  return val_clone(&k->val);
}

int reg_delete(const char *path) {
  RegKey *k = reg_lookup(path);
  if (!k)
    return -1;
  if (k->parent) {
    RegKey **pp = &k->parent->children;
    while (*pp && *pp != k)
      pp = &(*pp)->next;
    if (*pp)
      *pp = k->next;
  }
  regkey_free_recursive(k);
  return 0;
}

/* =========================================================
   POINTER EXTENSIONS
   ========================================================= */

int reg_set_ptr(const char *path, const char *target) {
  if (!path || !target)
    return -1;
  return reg_set(path, val_ptr(target), RK_ALL);
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

  /* Collect children into a temporary array for sorting */
  int count = 0;
  for (RegKey *c = k->children; c; c = c->next)
    count++;

  if (count > 0) {
    RegKey **list = xmalloc(sizeof(RegKey *) * count);
    int i = 0;
    for (RegKey *c = k->children; c; c = c->next)
      list[i++] = c;

    /* Simple bubble sort (sufficient for shell display) */
    for (int j = 0; j < count - 1; j++) {
      for (int l = 0; l < count - j - 1; l++) {
        if (strcmp(list[l]->name, list[l + 1]->name) > 0) {
          RegKey *tmp = list[l];
          list[l] = list[l + 1];
          list[l + 1] = tmp;
        }
      }
    }

    for (int j = 0; j < count; j++) {
      reg_ls_node(list[j], out, 0, 0);
    }
    xfree(list);
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
  if (!src || !dst || src[0] != '/' || dst[0] != '/')
    return -1;
  if (strcmp(src, "/") == 0)
    return -1;
  if (strcmp(src, dst) == 0)
    return 0;

  RegKey *src_key = reg_lookup(src);
  if (!src_key)
    return -1;

  size_t src_len = strlen(src);
  if (strncmp(dst, src, src_len) == 0 &&
      (dst[src_len] == '/' || dst[src_len] == '\0'))
    return -1;

  if (src_key->parent)
    regkey_detach_child(src_key->parent, src_key);

  char dst_parent[REG_PATH_MAX];
  nexs_path_dirname(dst, dst_parent, sizeof(dst_parent));
  RegKey *dst_parent_key = reg_lookup(dst_parent);
  if (!dst_parent_key)
    dst_parent_key = reg_mkpath(dst_parent, RK_ALL);
  if (!dst_parent_key)
    return -1;

  const char *new_name = nexs_path_basename(dst);
  strncpy(src_key->name, new_name, NAME_LEN - 1);
  src_key->name[NAME_LEN - 1] = '\0';

  regkey_add_child(dst_parent_key, src_key);
  regkey_update_paths(src_key, dst_parent);
  return 0;
}

int reg_mount(const char *src_path, const char *dst_path, int before) {
  if (!src_path || !dst_path)
    return -1;
  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;
  RegKey *dst = reg_lookup(dst_path);
  if (!dst)
    dst = reg_mkpath(dst_path, RK_ALL);
  if (!dst)
    return -1;

  for (RegKey *child = src->children; child; child = child->next) {
    RegKey *existing = regkey_find_child(dst, child->name);
    if (existing) {
      if (before) {
        Value old = existing->val;
        existing->val = val_clone(&child->val);
        val_free(&old);
        existing->rights = child->rights;
      }
    } else {
      char new_path[REG_PATH_MAX];
      if (strcmp(dst_path, "/") == 0)
        snprintf(new_path, sizeof(new_path), "/%s", child->name);
      else
        snprintf(new_path, sizeof(new_path), "%s/%s", dst_path, child->name);

      RegKey *mirror = regkey_alloc(child->name, new_path, child->rights);
      mirror->val = val_clone(&child->val);

      if (before)
        regkey_add_child(dst, mirror);
      else
        regkey_add_child_tail(dst, mirror);

      if (child->children) {
        char child_src[REG_PATH_MAX];
        snprintf(child_src, sizeof(child_src), "%s/%s", src_path, child->name);
        reg_mount(child_src, new_path, before);
      }
    }
  }
  return 0;
}

int reg_unmount(const char *src_path, const char *dst_path) {
  if (!dst_path)
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
      regkey_detach_child(dst, target);
      regkey_free_recursive(target);
    }
  }
  return 0;
}

int reg_bind(const char *src_path, const char *dst_path, int flag) {
  if (!src_path || !dst_path)
    return -1;
  RegKey *src = reg_lookup(src_path);
  if (!src)
    return -1;

  if (flag == 0) {
    RegKey *dst = reg_lookup(dst_path);
    if (!dst)
      dst = reg_mkpath(dst_path, RK_ALL);
    if (!dst)
      return -1;

    RegKey *child = dst->children;
    while (child) {
      RegKey *next = child->next;
      regkey_free_recursive(child);
      child = next;
    }
    dst->children = NULL;
    Value old = dst->val;
    dst->val = val_clone(&src->val);
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
  char *buf = xmalloc(REG_PATH_MAX);
  snprintf(buf, REG_PATH_MAX, "%s/%s_%d", REG_LOCAL, fn_name ? fn_name : "anon",
           g_scope_counter++);
  reg_mkpath(buf, RK_ALL);
  return buf;
}

void reg_pop_scope(const char *scope_path) {
  if (scope_path)
    reg_delete(scope_path);
}

/* =========================================================
   INITIALISATION
   ========================================================= */

void reg_init(void) {
  g_registry.root = regkey_alloc("/", "/", RK_ALL);
  g_registry.total_keys = 1;

  reg_mkpath(REG_LOCAL, RK_ALL);
  reg_mkpath(REG_FN, RK_READ | RK_EXEC);
  reg_mkpath(REG_SYS, RK_READ | RK_EXEC);
  reg_mkpath(REG_MOD, RK_ALL);
  reg_mkpath(REG_ENV, RK_ALL);
  reg_mkpath(REG_TYPE, RK_READ | RK_ADMIN);
}

/* =========================================================
   BUILT-IN REGISTRATION HELPER
   ========================================================= */

void reg_register_builtin(const char *name, void *fn) {
  if (!name || !fn)
    return;
  char path[REG_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", REG_SYS, name);
  Value v;
  v.type = TYPE_FN;
  v.data = fn;
  v.ival = 1; /* ival=1 → builtin: data is a BuiltinFn pointer */
  v.fval = 0.0;
  v.err_code = 0;
  v.err_msg = NULL;
  reg_set(path, v, RK_READ | RK_EXEC);
}
