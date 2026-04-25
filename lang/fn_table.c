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
#include "../core/include/nexs_value.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

NexsFnDef g_fn_table[NEXS_MAX_FN_DEFS];
int       g_fn_count = 0;

/* =========================================================
   INIT / FREE
   ========================================================= */

/* =========================================================
   PRINT HOOK — installed into nexs_val_fn_print at init
   ========================================================= */

static void fn_print_hook(int64_t idx, FILE *out) {
  NexsFnDef *def = fn_lookup_by_idx((int)idx);
  if (!def) { fprintf(out, "<fn #%lld>", (long long)idx); return; }

  if (def->is_builtin) {
    if (def->signature[0])
      fprintf(out, "<builtin: %s>", def->signature);
    else
      fprintf(out, "<builtin: %s(...)>", def->name);
  } else {
    fprintf(out, "<fn %s(", def->name);
    for (int i = 0; i < def->n_params; i++)
      fprintf(out, "%s%s", i ? " " : "", def->params[i]);
    fprintf(out, ")>");
  }
}

void fn_table_init(void) {
  memset(g_fn_table, 0, sizeof(g_fn_table));
  g_fn_count = 0;
  nexs_val_fn_print = fn_print_hook;
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
  return fn_register_builtin_sig(name, fn, NULL);
}

int fn_register_builtin_sig(const char *name, BuiltinFn fn, const char *sig) {
  if (!name || !fn) return -1;

  /* Replace if already registered (e.g. sysio re-registers after builtins) */
  for (int i = 0; i < g_fn_count; i++) {
    if (g_fn_table[i].is_builtin && strcmp(g_fn_table[i].name, name) == 0) {
      g_fn_table[i].builtin_fn = fn;
      if (sig) {
        strncpy(g_fn_table[i].signature, sig, sizeof(g_fn_table[i].signature) - 1);
        g_fn_table[i].signature[sizeof(g_fn_table[i].signature) - 1] = '\0';
      }
      return i;
    }
  }

  if (g_fn_count >= NEXS_MAX_FN_DEFS) return -1;

  int idx = g_fn_count++;
  strncpy(g_fn_table[idx].name, name, NAME_LEN - 1);
  g_fn_table[idx].name[NAME_LEN - 1] = '\0';
  g_fn_table[idx].body       = NULL;
  g_fn_table[idx].is_builtin = 1;
  g_fn_table[idx].builtin_fn = fn;
  g_fn_table[idx].ref_count  = 1;
  g_fn_table[idx].n_params   = 0;
  if (sig) {
    strncpy(g_fn_table[idx].signature, sig, sizeof(g_fn_table[idx].signature) - 1);
    g_fn_table[idx].signature[sizeof(g_fn_table[idx].signature) - 1] = '\0';
  }
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
