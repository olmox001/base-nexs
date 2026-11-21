/*
 * lang/fn_table.c — registry-owned callable objects.
 *
 * There is no independent function name table or MAX_FN_DEFS ceiling.
 * The ONE existing registry owns each callable at /fn/name or /sys/name.
 * The Value reference count owns the AST/function object lifetime.
 *
 * This is the registry-backed function migration. The eventual NXOLMO
 * StateStore will replace RegKey ownership in place, not add a second table.
 * Callable pointers are process-local only and MUST NOT enter REG wire data.
 */
#include "include/nexs_fn.h"
#include "include/nexs_ast.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_value.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int fn_object_retain(void *object) {
  NexsFnDef *def = (NexsFnDef *)object;
  if (def == NULL || def->ref_count == UINT64_MAX || def->ref_count == 0U)
    return -1;
  ++def->ref_count;
  return 0;
}

void fn_object_release(void *object) {
  NexsFnDef *def = (NexsFnDef *)object;
  if (def == NULL || def->ref_count == 0U)
    return;
  --def->ref_count;
  if (def->ref_count == 0U) {
    ASTNode *body = def->body;
    def->body = NULL;
    if (!def->is_builtin && body != NULL)
      ast_free(body);
    xfree(def);
  }
}

static void fn_value_print_hook(const void *object, FILE *out) {
  const NexsFnDef *def = (const NexsFnDef *)object;
  if (def == NULL) {
    nexs_fprintf(out, "<invalid function>");
    return;
  }
  if (def->is_builtin) {
    if (def->signature[0] != '\0')
      nexs_fprintf(out, "<builtin: %s>", def->signature);
    else
      nexs_fprintf(out, "<builtin: %s(...)>", def->name);
    return;
  }
  nexs_fprintf(out, "<fn %s(", def->name);
  for (int i = 0; i < def->n_params; ++i)
    nexs_fprintf(out, "%s%s", i ? " " : "", def->params[i]);
  nexs_fprintf(out, ")>");
}

void fn_value_hooks_init(void) {
  nexs_val_fn_print = fn_value_print_hook;
  nexs_val_fn_retain = fn_object_retain;
  nexs_val_fn_release = fn_object_release;
}

static size_t fn_bounded_length(const char *value, size_t capacity) {
  if (value == NULL)
    return capacity;
  size_t length = 0U;
  while (length < capacity && value[length] != '\0')
    ++length;
  return length;
}

static NexsFnDef *fn_allocate(const char *name) {
  if (name == NULL || name[0] == '\0' ||
      fn_bounded_length(name, NAME_LEN) >= NAME_LEN)
    return NULL;
  NexsFnDef *def = buddy_alloc(sizeof(*def));
  if (def == NULL)
    return NULL;
  memset(def, 0, sizeof(*def));
  memcpy(def->name, name, strlen(name) + 1U);
  def->ref_count = 1U;
  return def;
}

static int fn_publish(const char *prefix, NexsFnDef *def) {
  if (prefix == NULL || def == NULL)
    return -1;
  char path[REG_PATH_MAX];
  int len = snprintf(path, sizeof(path), "%s/%s", prefix, def->name);
  if (len < 0 || (size_t)len >= sizeof(path))
    return -1;
  /* reg_set clones the callable Value and thus takes one strong ref.
   * The creator's independent ref is dropped by the caller afterwards. */
  return reg_set(path, val_fn_borrow(def), RK_READ | RK_EXEC);
}

/* Success transfers ownership of the AST body; failure leaves it with
 * the parser/evaluator. The registry is the ONLY name->callable index. */
int fn_register(const char *name, ASTNode *body,
                const char (*params)[NAME_LEN], int n_params) {
  if (body == NULL || n_params < 0 || n_params > MAX_PARAMS ||
      (n_params > 0 && params == NULL))
    return -1;
  NexsFnDef *def = fn_allocate(name);
  if (def == NULL)
    return -1;
  for (int i = 0; i < n_params; ++i) {
    if (fn_bounded_length(params[i], NAME_LEN) >= NAME_LEN) {
      fn_object_release(def);
      return -1;
    }
    memcpy(def->params[i], params[i], strlen(params[i]) + 1U);
  }
  def->n_params = n_params;
  def->is_builtin = 0;
  def->body = body;
  int result = fn_publish(REG_FN, def);
  if (result != 0)
    def->body = NULL; /* Parser still owns the AST on failure. */
  fn_object_release(def);
  return result;
}

int fn_register_builtin(const char *name, BuiltinFn fn) {
  return fn_register_builtin_sig(name, fn, NULL);
}

int fn_register_builtin_sig(const char *name, BuiltinFn fn, const char *sig) {
  if (fn == NULL || (sig && fn_bounded_length(sig, sizeof(((NexsFnDef *)0)->signature))
                        >= sizeof(((NexsFnDef *)0)->signature)))
    return -1;
  NexsFnDef *def = fn_allocate(name);
  if (def == NULL)
    return -1;
  def->is_builtin = 1;
  def->builtin_fn = fn;
  if (sig != NULL)
    memcpy(def->signature, sig, strlen(sig) + 1U);
  int result = fn_publish(REG_SYS, def);
  fn_object_release(def);
  return result;
}

NexsFnDef *fn_lookup_in_scope(const char *name, const char *scope) {
  RegKey *key = reg_resolve_callable(name, scope);
  if (key == NULL || key->val.type != TYPE_FN)
    return NULL;
  return (NexsFnDef *)key->val.data;
}

NexsFnDef *fn_lookup(const char *name) {
  return fn_lookup_in_scope(name, REG_ROOT);
}
