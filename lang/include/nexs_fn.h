/* Single-registry callable implementation and Value ownership contract.
 * No global fn_table, index-shaped function pointer or duplicate catalog.
 * Capability-bound NXOLMO object references will replace local pointers
 * when StateStore and REG transport are migrated. */
#ifndef NEXS_FN_H
#define NEXS_FN_H

#include "nexs_ast.h"
#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"
#include <stdint.h>

typedef Value (*BuiltinFn)(Value *args, int n_args);

typedef struct NexsFnDef {
  char name[NAME_LEN];
  char params[MAX_PARAMS][NAME_LEN];
  int n_params;
  ASTNode *body;
  uint64_t ref_count;
  int is_builtin;
  BuiltinFn builtin_fn;
  char signature[128];
} NexsFnDef;

/* Hooks keep core Value independent from language object implementation. */
NEXS_API void fn_value_hooks_init(void);
NEXS_API int fn_object_retain(void *object);
NEXS_API void fn_object_release(void *object);

/* Success transfers AST body ownership into registry Value, failure retains
 * the AST with the caller; both functions return 0 on success. */
NEXS_API int fn_register(const char *name, ASTNode *body,
                         const char (*params)[NAME_LEN], int n_params);
NEXS_API int fn_register_builtin(const char *name, BuiltinFn fn);
NEXS_API int fn_register_builtin_sig(const char *name, BuiltinFn fn,
                                     const char *signature);
NEXS_API NexsFnDef *fn_lookup(const char *name);
NEXS_API NexsFnDef *fn_lookup_in_scope(const char *name, const char *scope);

#endif
