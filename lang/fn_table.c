/*
 * lang/fn_table.c — Global Function Table Implementation
 * ========================================================
 * Flat table of all user-defined and built-in functions.
 * Owns the ASTNode* body of every user function.
 */

#include "include/nexs_fn.h"
#include "include/nexs_ast.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"

#include <string.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

NexsFnDef g_fn_table[NEXS_MAX_FN_DEFS];
int       g_fn_count = 0;

/* =========================================================
   INIT / FREE
   ========================================================= */

void fn_table_init(void) {
  memset(g_fn_table, 0, sizeof(g_fn_table));
  g_fn_count = 0;
}

void fn_table_free(void) {
  for (int i = 0; i < g_fn_count; i++) {
    if (!g_fn_table[i].is_builtin && g_fn_table[i].body) {
      ast_free(g_fn_table[i].body);
      g_fn_table[i].body = NULL;
    }
  }
  g_fn_count = 0;
}

/* =========================================================
   REGISTER USER FUNCTION
   Takes ownership of 'body' — caller must NOT free it.
   ========================================================= */

int fn_register(const char *name,
                ASTNode *body,
                const char (*params)[NAME_LEN],
                int n_params) {
  if (!name) return -1;

  /* Check if a function with the same name already exists — replace it */
  for (int i = 0; i < g_fn_count; i++) {
    if (!g_fn_table[i].is_builtin &&
        strcmp(g_fn_table[i].name, name) == 0) {
      /* Free old body before replacing */
      if (g_fn_table[i].body) {
        ast_free(g_fn_table[i].body);
        g_fn_table[i].body = NULL;
      }
      g_fn_table[i].body = body; /* takes ownership */
      for (int j = 0; j < n_params && j < MAX_PARAMS; j++) {
        strncpy(g_fn_table[i].params[j], params[j], NAME_LEN - 1);
        g_fn_table[i].params[j][NAME_LEN - 1] = '\0';
      }
      g_fn_table[i].n_params = n_params;
      return i;
    }
  }

  if (g_fn_count >= NEXS_MAX_FN_DEFS) return -1;

  int idx = g_fn_count++;
  strncpy(g_fn_table[idx].name, name, NAME_LEN - 1);
  g_fn_table[idx].name[NAME_LEN - 1] = '\0';
  g_fn_table[idx].body = body; /* takes ownership */
  for (int j = 0; j < n_params && j < MAX_PARAMS; j++) {
    strncpy(g_fn_table[idx].params[j], params[j], NAME_LEN - 1);
    g_fn_table[idx].params[j][NAME_LEN - 1] = '\0';
  }
  g_fn_table[idx].n_params   = n_params;
  g_fn_table[idx].ref_count  = 1;
  g_fn_table[idx].is_builtin = 0;
  g_fn_table[idx].builtin_fn = NULL;
  return idx;
}

/* =========================================================
   REGISTER BUILT-IN FUNCTION
   ========================================================= */

int fn_register_builtin(const char *name, BuiltinFn fn) {
  if (!name || !fn) return -1;
  if (g_fn_count >= NEXS_MAX_FN_DEFS) return -1;

  int idx = g_fn_count++;
  strncpy(g_fn_table[idx].name, name, NAME_LEN - 1);
  g_fn_table[idx].name[NAME_LEN - 1] = '\0';
  g_fn_table[idx].body       = NULL;
  g_fn_table[idx].is_builtin = 1;
  g_fn_table[idx].builtin_fn = fn;
  g_fn_table[idx].ref_count  = 1;
  g_fn_table[idx].n_params   = 0;
  return idx;
}

/* =========================================================
   LOOKUP
   ========================================================= */

NexsFnDef *fn_lookup(const char *name) {
  if (!name) return NULL;
  for (int i = 0; i < g_fn_count; i++)
    if (strcmp(g_fn_table[i].name, name) == 0)
      return &g_fn_table[i];
  return NULL;
}

NexsFnDef *fn_lookup_by_idx(int idx) {
  if (idx < 0 || idx >= g_fn_count) return NULL;
  return &g_fn_table[idx];
}

/* =========================================================
   REFERENCE COUNTING
   ========================================================= */

void fn_ref(int idx) {
  if (idx >= 0 && idx < g_fn_count)
    g_fn_table[idx].ref_count++;
}

void fn_unref(int idx) {
  if (idx < 0 || idx >= g_fn_count) return;
  if (--g_fn_table[idx].ref_count <= 0) {
    if (!g_fn_table[idx].is_builtin && g_fn_table[idx].body) {
      ast_free(g_fn_table[idx].body);
      g_fn_table[idx].body = NULL;
    }
    /* Leave slot in place — simple flat table, no compaction */
  }
}
