/*
 * nexs_fn.h — NEXS Function Table
 * ==================================
 * Global flat table of all user-defined and built-in functions.
 * Owns the ASTNode* body of every user function.
 */

#ifndef NEXS_FN_H
#define NEXS_FN_H
#pragma once

#include "nexs_ast.h"
#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BuiltinFn — C function pointer type for built-in functions */
typedef Value (*BuiltinFn)(Value *args, int n_args);

/* =========================================================
   FUNCTION DEFINITION STRUCT
   ========================================================= */

typedef struct {
  char      name[NAME_LEN];
  char      params[MAX_PARAMS][NAME_LEN];
  int       n_params;

  /*
   * body: for user functions, the fn_table OWNS this pointer.
   * After fn_register(), the AST_FN_DECL node's right pointer
   * is set to NULL so ast_free(prog) skips it safely.
   */
  ASTNode  *body;

  int       ref_count;
  int       is_builtin;   /* 1 = builtin_fn is valid, body == NULL */
  BuiltinFn builtin_fn;

  /*
   * signature: human-readable prototype shown when printing this value.
   * Example: "open(path str, mode int) → fd int"
   * Empty for user-defined functions (signature is derived from params[]).
   */
  char signature[128];
} NexsFnDef;

/* =========================================================
   GLOBAL TABLE
   ========================================================= */

extern NexsFnDef g_fn_table[NEXS_MAX_FN_DEFS];
extern int       g_fn_count;

/* =========================================================
   FUNCTION TABLE API
   ========================================================= */

NEXS_API void fn_table_init(void);
NEXS_API void fn_table_free(void);

/*
 * fn_register: register a user function.  Takes ownership of 'body'.
 * If a function with the same name already exists, replaces it.
 * Returns the index into g_fn_table, or -1 on error.
 */
NEXS_API int fn_register(const char *name,
                          ASTNode *body,
                          const char (*params)[NAME_LEN],
                          int n_params);

/*
 * fn_register_builtin: register a C built-in function.
 * Returns the index, or -1 on error.
 */
NEXS_API int fn_register_builtin(const char *name, BuiltinFn fn);

/*
 * fn_register_builtin_sig: register a builtin with a human-readable signature.
 * sig example: "open(path str, mode int) → fd int"
 */
NEXS_API int fn_register_builtin_sig(const char *name, BuiltinFn fn,
                                      const char *sig);

/* fn_lookup: find by name, return pointer into g_fn_table or NULL */
NEXS_API NexsFnDef *fn_lookup(const char *name);

/* fn_lookup_by_idx: find by index, return pointer or NULL if out of range */
NEXS_API NexsFnDef *fn_lookup_by_idx(int idx);

/* Reference counting */
NEXS_API void fn_ref(int idx);
NEXS_API void fn_unref(int idx);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_FN_H */
