/*
 * lang/eval.c — NEXS Tree-Walking Evaluator
 * ============================================
 * Evaluates the AST against the hierarchical registry.
 *
 * Key changes vs original:
 *   - AST_FN_DECL: transfers the body to the canonical registry Value
 *   - AST_FN_CALL: resolves and retains the registry-owned callable
 *   - eval_str: calls ast_free(prog) safely — fn bodies are NULL'd
 *   - New cases: AST_SEND_MSG, AST_RECV_MSG, AST_MSG_PENDING, AST_PTR_SET,
 * AST_PTR_DEREF
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "../registry/include/nexs_registry.h"
#include "include/nexs_ast.h"
#include "include/nexs_eval.h"
#include "include/nexs_fn.h"
#include "include/nexs_lex.h"
#include "../hal/include/nexs_hal.h"
#include "../runtime/include/nexs_source_status.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

extern const char *nexs_embedded_lookup(const char *path, size_t *bytes);

/* =========================================================
   CONTEXT INIT
   ========================================================= */

EvalCtx *nexs_g_eval_ctx = NULL;

void eval_ctx_init(EvalCtx *ctx) {
  if (!ctx)
    return;
  strncpy(ctx->scope, REG_ROOT, REG_PATH_MAX - 1);
  ctx->scope[REG_PATH_MAX - 1] = '\0';
  ctx->call_depth = 0;
  ctx->debug = 0;
  memset(&ctx->execution_budget, 0, sizeof(ctx->execution_budget));
  ctx->fuel_limited = 0;
#ifdef NEXS_BAREMETAL
  ctx->out = NULL;
  ctx->err = NULL;
#else
  ctx->out = stdout;
  ctx->err = stderr;
#endif
}

/* An explicit task/owner budget is charged for every AST dispatch.
 * This does not yet meter native service calls, page traffic or preempt
 * recursive C frames: the complete P2 verified VM remains required. */
int eval_ctx_set_fuel_budget(EvalCtx *ctx, uint64_t fuel) {
  if (ctx == NULL)
    return -1;
  /* Zero is an explicit deny-all budget. The NXOLMO operation initializer
   * admits only positive budgets; the zero case never authorizes dispatch. */
  if (fuel == 0U) {
    memset(&ctx->execution_budget, 0, sizeof(ctx->execution_budget));
  } else if (nx_operation_budget_init(&ctx->execution_budget,
                                      0U, fuel, UINT64_MAX) != NX_STATUS_OK) {
    return -1;
  }
  ctx->fuel_limited = 1;
  return 0;
}

static int eval_charge_fuel(EvalCtx *ctx) {
  if (ctx == NULL)
    return 0;
  if (!ctx->fuel_limited)
    return 1;
  if (ctx->execution_budget.fuel_remaining == 0U)
    return 0;
  return nx_operation_budget_consume(
      &ctx->execution_budget, UINT64_C(1)) == NX_STATUS_OK;
}

/* =========================================================
   RESULT CONSTRUCTORS
   ========================================================= */

static EvalResult ok(Value v) { return (EvalResult){CTRL_NONE, v}; }
static EvalResult ctrl_break(void) {
  return (EvalResult){CTRL_BREAK, val_nil()};
}
static EvalResult ctrl_cont(void) { return (EvalResult){CTRL_CONT, val_nil()}; }
static EvalResult ctrl_ret(Value v) { return (EvalResult){CTRL_RET, v}; }
static EvalResult err_result(const char *msg) {
  return (EvalResult){CTRL_ERR, val_err(99, msg)};
}

/* =========================================================
   FORWARD DECLARATION
   ========================================================= */

static EvalResult eval_node(EvalCtx *ctx, ASTNode *n);

/* =========================================================
   ENTRY POINTS
   ========================================================= */

EvalResult eval(EvalCtx *ctx, ASTNode *node) {
  if (!node)
    return ok(val_nil());
  return eval_node(ctx, node);
}

static EvalResult eval_block(EvalCtx *ctx, ASTNode *block) {
  if (!block)
    return ok(val_nil());
  ASTNode *s = block->children;
  Value last = val_nil();
  while (s) {
    EvalResult r = eval_node(ctx, s);
    if (r.sig != CTRL_NONE) {
      val_free(&last);
      return r;
    }
    val_free(&last);
    last = val_clone(&r.ret_val);
    val_free(&r.ret_val);
    s = s->next;
  }
  return ok(last);
}

/* =========================================================
   CORE EVALUATOR
   ========================================================= */

#define DEBUG_PRINT(ctx, fmt, ...)                                             \
  if (g_nexs_debug) {                                                          \
    nexs_fprintf((ctx)->err ? (ctx)->err : stderr, "[DEBUG] " fmt "\n",        \
                 ##__VA_ARGS__);                                               \
  }

static EvalResult eval_node(EvalCtx *ctx, ASTNode *n) {
  if (!n)
    return ok(val_nil());
  if (!ctx)
    return err_result("NULL EvalCtx in eval_node");
  if (!eval_charge_fuel(ctx))
    return err_result("execution fuel exhausted");

  /* DEBUG_PRINT(ctx, "eval_node kind=%d", n->kind); */

  switch (n->kind) {

  /* --- Library / Module (Include Guard) --- */
  case AST_LIBRARY: {
    /* Check if this library is already loaded */
    char guard_path[REG_PATH_MAX];
    snprintf(guard_path, sizeof(guard_path), "/sys/loader/libs/%s", n->name);
    if (reg_lookup(guard_path)) {
      /* Already loaded: skip evaluation of the rest of the file */
      return ctrl_ret(val_nil());
    }
    /* Register it as loaded */
    reg_set(guard_path, val_int(1), RK_READ);
    return ok(val_nil());
  }

  /* --- Import ---
   * An imported file has its own top-level control boundary. A library
   * guard's CTRL_RET ends only that imported source, not the importer
   * containing subsequent service definitions. Errors still propagate. */
  case AST_IMPORT: {
    EvalResult imported = eval_file(ctx, n->path);
    if (imported.sig == CTRL_RET) {
      val_free(&imported.ret_val);
      return ok(val_nil());
    }
    return imported;
  }

  /* --- Literals --- */
  case AST_INT_LIT:
  case AST_FLOAT_LIT:
  case AST_STR_LIT:
  case AST_BOOL_LIT:
  case AST_NIL_LIT:
    return ok(val_clone(&n->litval));

  /* --- Binary operation --- */
  case AST_BINOP: {
    EvalResult lr = eval_node(ctx, n->left);
    if (lr.sig != CTRL_NONE)
      return lr;
    EvalResult rr = eval_node(ctx, n->right);
    if (rr.sig != CTRL_NONE) {
      val_free(&lr.ret_val);
      return rr;
    }
    Value res;
    const char *op = n->op;
    if (!strcmp(op, "+"))
      res = val_add(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "-"))
      res = val_sub(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "*"))
      res = val_mul(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "/"))
      res = val_div(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "%"))
      res = val_mod(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "=="))
      res = val_eq(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "!="))
      res = val_ne(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "<"))
      res = val_lt(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, ">"))
      res = val_gt(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "<="))
      res = val_le(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, ">="))
      res = val_ge(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "&&"))
      res = val_and(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "||"))
      res = val_or(&lr.ret_val, &rr.ret_val);
    else
      res = val_err(5, "unknown operator");
    val_free(&lr.ret_val);
    val_free(&rr.ret_val);
    return ok(res);
  }

  /* --- Unary --- */
  case AST_UNOP: {
    EvalResult lr = eval_node(ctx, n->left);
    if (lr.sig != CTRL_NONE)
      return lr;
    Value res = val_not(&lr.ret_val);
    val_free(&lr.ret_val);
    return ok(res);
  }

  /* --- Identifier lookup --- */
  case AST_IDENT: {
    if (n->name[0] == '\0')
      return ok(val_nil());
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (!k) {
      char msg[128];
      snprintf(msg, sizeof(msg), "name not found: '%s'", n->name);
      return err_result(msg);
    }
    return ok(val_clone(&k->val));
  }

  /* --- Variable assignment --- */
  case AST_ASSIGN: {
    EvalResult vr = eval_node(ctx, n->right);
    if (vr.sig != CTRL_NONE)
      return vr;

    char path[REG_PATH_MAX];
    RegKey *existing = reg_resolve(n->name, ctx->scope);
    /* The original language has distinct variable and callable bindings.
     * An assignment to a variable named 'type', 'mod' or a user function
     * creates/updates a lexical value without rewriting the callable
     * object under /fn or /sys. Explicit reg_set("/fn/name", ...)
     * remains the only path-based authoritative mutation mechanism. */
    if (existing != NULL && existing->val.type == TYPE_FN)
      existing = NULL;
    int status = -1;
    if (existing != NULL) {
      status = reg_set(existing->path, vr.ret_val, RK_ALL);
    } else {
      int written = snprintf(path, sizeof(path), "%s/%s",
                             strcmp(ctx->scope, REG_ROOT) == 0 ? "" : ctx->scope,
                             n->name);
      if (written >= 0 && (size_t)written < sizeof(path))
        status = reg_set(path, vr.ret_val, RK_ALL);
    }
    if (status != 0) {
      val_free(&vr.ret_val);
      return err_result("variable registry admission failed");
    }
    Value ret = val_clone(&vr.ret_val);
    val_free(&vr.ret_val);
    return ok(ret);
  }

  /* --- Array index: arr[idx] --- */
  case AST_INDEX: {
    EvalResult ir = eval_node(ctx, n->left);
    if (ir.sig != CTRL_NONE)
      return ir;
    int64_t idx = val_to_int(&ir.ret_val);
    val_free(&ir.ret_val);
    DynArray *arr = NULL;
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (k && k->val.type == TYPE_ARR && k->val.data)
      arr = (DynArray *)k->val.data;

    if (!arr)
      return err_result("array not found");
    if (idx < 0)
      return err_result("negative index");
    return ok(arr_get_at(arr, (size_t)idx));
  }

  /* --- Array index assignment: arr[idx] = val --- */
  case AST_INDEX_ASSIGN: {
    EvalResult ir = eval_node(ctx, n->left);
    if (ir.sig != CTRL_NONE)
      return ir;
    EvalResult vr = eval_node(ctx, n->right);
    if (vr.sig != CTRL_NONE) {
      val_free(&ir.ret_val);
      return vr;
    }
    int64_t idx = val_to_int(&ir.ret_val);
    val_free(&ir.ret_val);
    if (idx < 0) {
      val_free(&vr.ret_val);
      return err_result("negative index");
    }

    DynArray *arr = NULL;
    int initialized_new = 0;
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (k != NULL) {
      if (k->val.type != TYPE_ARR || k->val.data == NULL) {
        val_free(&vr.ret_val);
        return err_result("array variable is not an array");
      }
      arr = (DynArray *)k->val.data;
    } else {
      /* Registry takes a reference to the new array, not a second copy
       * indexed by a global name. Check the actual scope/path first. */
      char path[REG_PATH_MAX];
      int written = snprintf(path, sizeof(path), "%s/%s",
                             strcmp(ctx->scope, REG_ROOT) == 0 ? "" : ctx->scope,
                             n->name);
      if (written < 0 || (size_t)written >= sizeof(path)) {
        val_free(&vr.ret_val);
        return err_result("array namespace path exceeds REG_PATH_MAX");
      }
      arr = arr_create_anon();
      if (arr == NULL) {
        val_free(&vr.ret_val);
        return err_result("array object allocation failed");
      }
      /* Initialize first element BEFORE publishing a registry key.
       * An invalid index or exhausted provider must leave no ghost key. */
      if (arr_set(arr, (size_t)idx, vr.ret_val) != 0) {
        arr_unref(arr);
        val_free(&vr.ret_val);
        return err_result("array capacity admission failed");
      }
      initialized_new = 1;
      Value av = val_nil();
      av.type = TYPE_ARR;
      av.data = arr;
      int registered = reg_set(path, av, RK_ALL);
      arr_unref(arr);
      if (registered != 0) {
        val_free(&vr.ret_val);
        return err_result("array registry insertion failed");
      }
      k = reg_lookup(path);
      if (k == NULL || k->val.type != TYPE_ARR || k->val.data == NULL) {
        val_free(&vr.ret_val);
        return err_result("array registry ownership missing");
      }
    }
    if (!initialized_new && arr_set(arr, (size_t)idx, vr.ret_val) != 0) {
      val_free(&vr.ret_val);
      return err_result("array capacity admission failed");
    }
    Value ret = val_clone(&vr.ret_val);
    val_free(&vr.ret_val);
    return ok(ret);
  }

  /* --- Delete array element --- */
  case AST_DEL: {
    EvalResult ir = eval_node(ctx, n->left);
    if (ir.sig != CTRL_NONE)
      return ir;
    int64_t idx = val_to_int(&ir.ret_val);
    val_free(&ir.ret_val);

    DynArray *arr = NULL;
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (k && k->val.type == TYPE_ARR && k->val.data)
      arr = (DynArray *)k->val.data;

    if (arr && idx >= 0)
      arr_delete(arr, (size_t)idx);
    return ok(val_nil());
  }

  /* --- Output --- */
  /* --- Output --- */
  case AST_OUT: {
    EvalResult vr = eval_node(ctx, n->left);
    if (vr.sig != CTRL_NONE)
      return vr;
    FILE *out = (ctx && ctx->out) ? ctx->out : stdout;
    val_print(&vr.ret_val, out);
    nexs_fprintf(out, "\n");
    val_free(&vr.ret_val);
    return ok(val_nil());
  }

  /* --- Registry read --- */
  case AST_REG_ACCESS:
    return ok(reg_get(n->path));

  /* --- Registry write --- */
  case AST_REG_SET: {
    EvalResult vr = eval_node(ctx, n->left);
    if (vr.sig != CTRL_NONE)
      return vr;
    reg_set(n->path, vr.ret_val, RK_ALL);
    Value ret = val_clone(&vr.ret_val);
    val_free(&vr.ret_val);
    return ok(ret);
  }

  /* --- Registry list --- */
  case AST_REG_LS: {
    char target[REG_PATH_MAX];
    if (n->path[0] != '/') {
      REG_PATH(target, ctx->scope, n->path);
    } else {
      strncpy(target, n->path, REG_PATH_MAX - 1);
      target[REG_PATH_MAX - 1] = '\0';
    }
    DEBUG_PRINT(ctx, "LS: %s", target);
    reg_ls(target, ctx->out);
    return ok(val_nil());
  }

  /* --- Navigation --- */
  case AST_CD: {
    char target[REG_PATH_MAX];
    if (n->path[0] != '\0') {
      if (n->path[0] != '/') {
        REG_PATH(target, ctx->scope, n->path);
      } else {
        strncpy(target, n->path, REG_PATH_MAX - 1);
        target[REG_PATH_MAX - 1] = '\0';
      }
      if (reg_lookup(target)) {
        strncpy(ctx->scope, target, REG_PATH_MAX - 1);
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Path not found: '%s'", target);
        return err_result(msg);
      }
    } else if (n->left) {
      EvalResult vr = eval_node(ctx, n->left);
      if (vr.sig != CTRL_NONE)
        return vr;
      const char *p = (vr.ret_val.type == TYPE_STR && vr.ret_val.data)
                          ? (char *)vr.ret_val.data
                          : "";
      if (p[0] != '/') {
        REG_PATH(target, ctx->scope, p);
      } else {
        strncpy(target, p, REG_PATH_MAX - 1);
        target[REG_PATH_MAX - 1] = '\0';
      }
      if (reg_lookup(target)) {
        strncpy(ctx->scope, target, REG_PATH_MAX - 1);
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Path not found: '%s'", target);
        val_free(&vr.ret_val);
        return err_result(msg);
      }
      val_free(&vr.ret_val);
    }
    ctx->scope[REG_PATH_MAX - 1] = '\0';
    DEBUG_PRINT(ctx, "CD: Scope is now %s", ctx->scope);
    return ok(val_nil());
  }

  case AST_PWD:
    return ok(val_str(ctx->scope));

  /* --- Function declaration ---
   * The one registry owns the callable and its AST body.
   * The parser releases the transferred pointer by nulling n->right.
   */
  case AST_FN_DECL: {
    /* Exactly one registry-owned callable object receives the AST.
     * On error, the parser retains ownership of n->right. */
    if (fn_register(n->name, n->right,
                    (const char (*)[NAME_LEN])n->params,
                    n->n_params) != 0)
      return err_result("callable registry admission failed");
    n->right = NULL;
    return ok(val_nil());
  }

  /* --- Function call --- */
  case AST_FN_CALL: {
    /* if (g_nexs_debug) {
      nexs_fprintf(NULL, "[TRACE] Call: %s\n", n->name);
    } */

    /* Evaluate arguments */
    Value args[MAX_PARAMS];
    int n_args = n->n_args;
    for (int i = 0; i < n_args; i++) {
      EvalResult ar = eval_node(ctx, n->args[i]);
      if (ar.sig != CTRL_NONE) {
        for (int j = 0; j < i; j++)
          val_free(&args[j]);
        return ar;
      }
      args[i] = ar.ret_val;
    }

    /* The current registry is the sole source of named callables.
     * Its typed resolver honors the distinct language callable namespace:
     * local scalars cannot hide functions, but deleting/replacing an
     * actual /fn or /sys callable remains authoritative. */
    NexsFnDef *def = fn_lookup_in_scope(n->name, ctx->scope);
    if (!def) {
      char msg[128];
      snprintf(msg, sizeof(msg), "function not found: '%s'", n->name);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result(msg);
    }

    /* Hold the object through the whole call, even if its name is
     * redefined/deleted in the body. No dangling AST body on replacement. */
    if (fn_object_retain(def) != 0) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result("callable lifetime admission failed");
    }
    if (def->is_builtin) {
      Value res = def->builtin_fn(args, n_args);
      fn_object_release(def);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return ok(res);
    }

    /* User function (legacy recursive C evaluator, VM still pending) */
    if (ctx->call_depth >= MAX_CALL_DEPTH) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      fn_object_release(def);
      return err_result("stack overflow: too many recursion levels");
    }
    ASTNode *fn_body = def->body;
    if (!fn_body) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      fn_object_release(def);
      return err_result("empty function body");
    }

    char *new_scope = reg_push_scope(n->name);
    if (new_scope == NULL) {
      fn_object_release(def);
      for (int i = 0; i < n_args; ++i)
        val_free(&args[i]);
      return err_result("function scope admission failed");
    }
    ctx->call_depth++;

    /* Bind parameters atomically with respect to function execution:
     * on any failed binding, destroy this new scope and do NOT call body. */
    int scope_bound = 1;
    for (int i = 0; i < def->n_params && scope_bound; i++) {
      char ppath[REG_PATH_MAX];
      int path_size = snprintf(ppath, sizeof(ppath), "%s/%s",
                               new_scope, def->params[i]);
      if (path_size < 0 || (size_t)path_size >= sizeof(ppath)) {
        scope_bound = 0;
      } else if (reg_set(ppath, i < n_args ? args[i] : val_nil(),
                         RK_ALL) != 0) {
        scope_bound = 0;
      }
    }
    if (!scope_bound) {
      --ctx->call_depth;
      reg_pop_scope(new_scope);
      nexs_free(new_scope, REG_PATH_MAX);
      fn_object_release(def);
      for (int i = 0; i < n_args; ++i)
        val_free(&args[i]);
      return err_result("function parameter binding failed");
    }

    char saved_scope[REG_PATH_MAX];
    strncpy(saved_scope, ctx->scope, REG_PATH_MAX - 1);
    saved_scope[REG_PATH_MAX - 1] = '\0';
    strncpy(ctx->scope, new_scope, REG_PATH_MAX - 1);
    ctx->scope[REG_PATH_MAX - 1] = '\0';

    EvalResult res = eval_block(ctx, fn_body);

    strncpy(ctx->scope, saved_scope, REG_PATH_MAX - 1);
    ctx->scope[REG_PATH_MAX - 1] = '\0';
    ctx->call_depth--;
    reg_pop_scope(new_scope);
    nexs_free(new_scope, REG_PATH_MAX);
    fn_object_release(def);
    for (int i = 0; i < n_args; i++)
      val_free(&args[i]);

    if (res.sig == CTRL_RET)
      return ok(res.ret_val);
    if (res.sig == CTRL_ERR)
      return res;
    val_free(&res.ret_val);
    return ok(val_nil());
  }

  /* --- If/Else --- */
  case AST_IF: {
    EvalResult cr = eval_node(ctx, n->left);
    if (cr.sig != CTRL_NONE)
      return cr;
    int truthy = val_is_truthy(&cr.ret_val);
    val_free(&cr.ret_val);
    if (truthy)
      return eval_block(ctx, n->right);
    if (n->alt)
      return eval_node(ctx, n->alt);
    return ok(val_nil());
  }

  /* --- Loop --- */
  case AST_LOOP: {
    while (1) {
      /* A zero-statement loop still consumes work and must terminate
       * under a finite admitted budget; body nodes charge separately. */
      if (!eval_charge_fuel(ctx))
        return err_result("execution fuel exhausted");
      EvalResult r = eval_block(ctx, n->right);
      if (r.sig == CTRL_BREAK) {
        val_free(&r.ret_val);
        return ok(val_nil());
      }
      if (r.sig == CTRL_RET || r.sig == CTRL_ERR)
        return r;
      val_free(&r.ret_val);
    }
  }

  case AST_BREAK:
    return ctrl_break();
  case AST_CONT:
    return ctrl_cont();

  case AST_RET: {
    if (n->left) {
      EvalResult vr = eval_node(ctx, n->left);
      if (vr.sig != CTRL_NONE)
        return vr;
      return ctrl_ret(vr.ret_val);
    }
    return ctrl_ret(val_nil());
  }

  case AST_PROGRAM:
  case AST_BLOCK:
    return eval_block(ctx, n);

  /* =========================================================
     NEW: IPC and Pointer nodes
     ========================================================= */

  /* sendmessage /path expr */
  case AST_SEND_MSG: {
    EvalResult vr = eval_node(ctx, n->left);
    if (vr.sig != CTRL_NONE)
      return vr;
    int rc = reg_ipc_send(n->path, vr.ret_val);
    val_free(&vr.ret_val);
    return ok(val_int(rc));
  }

  /* receivemessage /path */
  case AST_RECV_MSG: {
    Value msg = val_nil();
    int rc = reg_ipc_recv(n->path, &msg);
    if (rc < 0) {
      val_free(&msg);
      return ok(val_nil());
    }
    return ok(msg); /* caller owns msg */
  }

  /* msgpending /path */
  case AST_MSG_PENDING: {
    int count = reg_ipc_pending(n->path);
    return ok(val_int(count));
  }

  /* ptr /path = /target */
  case AST_PTR_SET: {
    int rc = reg_set_ptr(n->path, (const char *)n->litval.data);
    if (rc < 0)
      return err_result("ptr: failed to set pointer");
    return ok(val_nil());
  }

  /* deref /path */
  case AST_PTR_DEREF: {
    Value v = reg_get_deref(n->path);
    return ok(v);
  }

  default:
    return err_result("unknown AST node");
  }
}

/* =========================================================
   PIPELINE SHORTCUTS
   ========================================================= */

EvalResult eval_str(EvalCtx *ctx, const char *src) {
  return eval_str_lib(ctx, src, NULL);
}

EvalResult eval_str_lib(EvalCtx *ctx, const char *src, const char *lib_name) {
  if (!ctx || !src)
    return err_result("NULL ctx or src");
  nexs_g_eval_ctx = ctx;

  Lexer lex;
  Parser par;
  if (lib_name)
    lexer_init_lib(&lex, src, lib_name);
  else
    lexer_init(&lex, src);

  parser_init(&par, &lex);
  ASTNode *prog = parse_program(&par);
  if (par.had_error) {
    /* Use stderr directly as a safe fallback — ctx->err might be corrupt
     * in reentrant eval scenarios (shell → eval → eval_str). */
#ifndef NEXS_BAREMETAL
    FILE *safe_err = ctx->err ? ctx->err : stderr;
    nexs_fprintf(safe_err, "\033[1;31m[PARSE ERR]\033[0m %s\n", par.error_msg);
#else
    nexs_fprintf(ctx->err, "\033[1;31m[PARSE ERR]\033[0m %s\n", par.error_msg);
#endif
    ast_free_safe(prog);
    return err_result(par.error_msg);
  }
  EvalResult r = eval(ctx, prog);

  /*
   * IMPORTANT: If the result is a string, it might point into the AST we are
   * about to free. We must clone it to ensure it remains valid after ast_free.
   */
  Value final_val = val_clone(&r.ret_val);
  val_free(&r.ret_val);
  r.ret_val = final_val;

  /*
   * Safe to free prog: fn bodies have been NULL'd by eval (AST_FN_DECL case),
   * so ast_free() will not touch fn_table-owned AST nodes.
   */
  ast_free_safe(prog);
  return r;
}

EvalResult eval_file(EvalCtx *ctx, const char *fpath) {
  if (!ctx || !fpath)
    return err_result("NULL ctx or fpath");

  /* Exactly ONE RegKey/StateStore namespace. A packaged logical resource
   * always lives at its canonical root name; other relative resources
   * resolve through the same EvalCtx scope as cd/ls/reg.
   * No /sys/vfs/files alias, sentinel data node or separate VFS index. */
  char normalized[REG_PATH_MAX];
  const char *logical = fpath[0] == '/' ? fpath + 1 : fpath;
  int packaged = strncmp(logical, "nxlib/", 6U) == 0 ||
                 strncmp(logical, "example/", 8U) == 0;
  if (fpath[0] == '/' || packaged) {
    int written = snprintf(normalized, sizeof(normalized), "/%s", logical);
    if (written < 0 || (size_t)written >= sizeof(normalized))
      return err_result("source path too long");
  } else {
    nexs_path_join(normalized, sizeof(normalized), ctx->scope,
                   fpath, NULL);
  }
  if (normalized[0] != '/' || strlen(normalized) >= REG_PATH_MAX)
    return err_result("invalid source registry name");

  RegKey *file_node = reg_lookup(normalized);
  if (file_node != NULL) {
    if (file_node->val.type != TYPE_STR || file_node->val.data == NULL)
      return err_result("registry resource is not executable text");
    Value source = reg_get(normalized);
    if (source.type != TYPE_STR || source.data == NULL) {
      val_free(&source);
      return err_result("registry source read failed");
    }
    EvalResult r = eval_str(ctx, (const char *)source.data);
    val_free(&source);
    return r;
  }

  /* 2. Filesystem first; packaged source is an ENOENT fallback. The
   * ordinary SDK FILE capability performs actual access and ACL checks. */
  char platform_path[REG_PATH_MAX];
  const char *file_path = nexs_platform_source_path(
      fpath, platform_path, sizeof(platform_path));
  if (file_path == NULL)
    return err_result("invalid or oversized source path");
  FILE *f = fopen(file_path, "r");
  if (f == NULL) {
    /* An installed source has priority over embedded AOT fallback. Do not
     * treat a provider I/O error as permission to execute embedded code:
     * only a genuine absent resource may select the packaged source. */
    int source_error = errno;
    nx_status source_status = nexs_source_status_from_errno(source_error);
    if (nexs_source_embedded_admissible(source_status)) {
      /* The compiler table and hosted source admission use the SAME
       * logical key without a leading slash. A slash is only the NXREG
       * namespace representation, not part of the compiled resource name. */
      const char *emb = nexs_embedded_lookup(logical, NULL);
      if (emb != NULL)
        return eval_str(ctx, emb);
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "cannot open '%s': %s",
             fpath, strerror(source_error));
    return err_result(msg);
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return err_result("cannot seek source file");
  }
  long sz = ftell(f);
  if (sz < 0 || (uintmax_t)sz >= SIZE_MAX) {
    fclose(f);
    return err_result("source file size unavailable or too large");
  }
  if (sz == 0) {
    fclose(f);
    return ok(val_nil());
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return err_result("cannot rewind source file");
  }
  size_t source_bytes = (size_t)sz;
  char *src = nexs_alloc(source_bytes + 1U);
  if (src == NULL) {
    fclose(f);
    return err_result("source file allocation failed");
  }
  size_t bytes_read = fread(src, 1, source_bytes, f);
  /* fread reports a short count for EOF or I/O failure on every selected
   * provider, including the freestanding SDK which has no ferror(). */
  int read_failed = bytes_read != source_bytes;
  fclose(f);
  if (read_failed) {
    nexs_free(src, source_bytes + 1U);
    return err_result("source file read failed");
  }
  src[bytes_read] = '\0';

  /* The nxlib sources declare explicit language module IDs when needed.
   * Never synthesize another "library" token: it would execute the first
   * guard and cause the second one to return early, silently hiding
   * service exports such as vfs_import(). */
  EvalResult r = eval_str(ctx, src);
  nexs_free(src, source_bytes + 1U);
  return r;
}
