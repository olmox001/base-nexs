/*
 * lang/eval.c — NEXS Tree-Walking Evaluator
 * ============================================
 * Evaluates the AST against the hierarchical registry.
 *
 * Key changes vs original:
 *   - AST_FN_DECL: calls fn_register(); sets n->right = NULL (fn_table owns
 * body)
 *   - AST_FN_CALL: dispatches via fn_table (fn_lookup_by_idx / fn_lookup)
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

extern const char *nexs_embedded_lookup(const char *path);

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
#ifdef NEXS_BAREMETAL
  ctx->out = NULL;
  ctx->err = NULL;
#else
  ctx->out = stdout;
  ctx->err = stderr;
#endif
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

  /* --- Import --- */
  case AST_IMPORT: {
    return eval_file(ctx, n->path);
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
    if (existing) {
      /* Update existing key */
      reg_set(existing->path, vr.ret_val, RK_ALL);
    } else {
      /* Create new in current scope */
      snprintf(path, sizeof(path), "%s/%s", ctx->scope, n->name);
      reg_set(path, vr.ret_val, RK_ALL);
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
    if (k && k->val.type == TYPE_ARR && k->val.data) {
      arr = (DynArray *)k->val.data;
    } else {
      arr = arr_get(n->name);
    }

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
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (k && k->val.type == TYPE_ARR && k->val.data) {
      arr = (DynArray *)k->val.data;
    } else {
      arr = arr_get_or_create(n->name);
      /* If we created/found it via name, ensure it's in the current scope too
       */
      char path[REG_PATH_MAX];
      snprintf(path, sizeof(path), "%s/%s", ctx->scope, n->name);
      Value av;
      av.type = TYPE_ARR;
      av.data = arr;
      av.ival = 0;
      av.fval = 0;
      av.err_code = 0;
      av.err_msg = NULL;
      reg_set(path, av, RK_ALL);
    }

    arr_set(arr, (size_t)idx, vr.ret_val);
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
    if (k && k->val.type == TYPE_ARR && k->val.data) {
      arr = (DynArray *)k->val.data;
    } else {
      arr = arr_get(n->name);
    }

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
   * fn_table takes ownership of n->right (the body).
   * We set n->right = NULL so ast_free(prog) skips it.
   */
  case AST_FN_DECL: {
    /* Register in fn_table — transfers ownership of n->right */
    int idx = fn_register(n->name, n->right,
                          (const char (*)[NAME_LEN])n->params, n->n_params);
    /* CRITICAL: NULL out the body so ast_free doesn't double-free */
    n->right = NULL;

    if (idx < 0)
      return err_result("fn_register: table full");

    /* Store the fn_table index in the registry so call resolution works */
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", REG_FN, n->name);
    Value fv = val_fn_idx((int64_t)idx);
    reg_set(path, fv, RK_READ | RK_EXEC);
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

    /* Resolve the function */
    NexsFnDef *def = NULL;

    /* 1. Try fn_table by name (covers both user and builtin) */
    def = fn_lookup(n->name);

    /* 2. If not in fn_table, look in registry (legacy path) */
    if (!def) {
      RegKey *k = reg_resolve(n->name, ctx->scope);
      if (!k) {
        char syspath[REG_PATH_MAX];
        snprintf(syspath, sizeof(syspath), "%s/%s", REG_SYS, n->name);
        k = reg_lookup(syspath);
      }
      if (k && k->val.type == TYPE_FN) {
        /* Registry contains ival=idx for fn_table entries, or ival=1 for legacy
         * builtins */
        if (k->val.ival == 1 && k->val.data) {
          /* Legacy builtin: data is a BuiltinFn pointer */
          BuiltinFn fn = (BuiltinFn)k->val.data;
          Value res = fn(args, n_args);
          for (int i = 0; i < n_args; i++)
            val_free(&args[i]);
          return ok(res);
        }
        /* fn_table entry: ival is the index */
        def = fn_lookup_by_idx((int)k->val.ival);
      }
    }

    if (!def) {
      char msg[128];
      snprintf(msg, sizeof(msg), "function not found: '%s'", n->name);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result(msg);
    }

    /* Dispatch */
    if (def->is_builtin) {
      Value res = def->builtin_fn(args, n_args);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return ok(res);
    }

    /* User function */
    if (ctx->call_depth >= MAX_CALL_DEPTH) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result("stack overflow: too many recursion levels");
    }
    ASTNode *fn_body = def->body;
    if (!fn_body) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result("empty function body");
    }

    char *new_scope = reg_push_scope(n->name);
    ctx->call_depth++;

    /* Bind parameters into scope (initialize missing ones to nil) */
    for (int i = 0; i < def->n_params; i++) {
      char ppath[REG_PATH_MAX];
      snprintf(ppath, sizeof(ppath), "%s/%s", new_scope, def->params[i]);
      if (i < n_args) {
        reg_set(ppath, args[i], RK_ALL);
      } else {
        reg_set(ppath, val_nil(), RK_ALL);
      }
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
    xfree(new_scope);
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
    int rc = reg_set_ptr(n->path, n->name);
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

  char normalized[REG_PATH_MAX];
  const char *p = fpath;
  /* Strip leading slash if present for VFS registry key lookup */
  if (p[0] == '/')
    p++;

  /* 0. Resolve relative paths via VFS CWD if possible */
  if (fpath[0] != '/') {
    Value cwd_val = reg_get("/proc/1/vfs_cwd");
    if (cwd_val.type == TYPE_STR && cwd_val.data &&
        ((char *)cwd_val.data)[0] != '\0') {
      snprintf(normalized, sizeof(normalized), "%s/%s", (char *)cwd_val.data,
               fpath);
    } else {
      strncpy(normalized, fpath, sizeof(normalized) - 1);
      normalized[sizeof(normalized) - 1] = '\0';
    }
    val_free(&cwd_val);
  } else {
    strncpy(normalized, p, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
  }

  /* 1. Check Registry VFS (/sys/vfs/files/) */
  char reg_vfs_path[REG_PATH_MAX];
  snprintf(reg_vfs_path, sizeof(reg_vfs_path), "/sys/vfs/files/%s", normalized);
  Value vfs_val = reg_get(reg_vfs_path);
  if (vfs_val.type == TYPE_STR && vfs_val.data) {
    const char *vfs_content = (char *)vfs_val.data;
    if (strncmp(vfs_content, "BUNDLED", 7) == 0) {
      const char *lookup_path = normalized;
      if (strncmp(vfs_content, "BUNDLED:", 8) == 0) {
        lookup_path = vfs_content + 8;
      }
      const char *emb = nexs_embedded_lookup(lookup_path);
      if (emb) {
        EvalResult r = eval_str(ctx, emb);
        val_free(&vfs_val);
        return r;
      }
    } else {
      /* Direct content from Registry */
      EvalResult r = eval_str(ctx, vfs_content);
      val_free(&vfs_val);
      return r;
    }
  }
  val_free(&vfs_val);

  /* 2. Fallback to embedded table (AOT) using normalized path */
  const char *emb = nexs_embedded_lookup(normalized);
  if (emb) {
    return eval_str(ctx, emb);
  }
  /* Also try original path in embedded table */
  if (p != fpath) {
    emb = nexs_embedded_lookup(p);
    if (emb)
      return eval_str(ctx, emb);
  }

  /* 3. Fallback to hosted filesystem */
  FILE *f = fopen(fpath, "r");
  if (!f) {
    char msg[256];
    snprintf(msg, sizeof(msg), "cannot open '%s': %s", fpath, strerror(errno));
    return err_result(msg);
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return ok(val_nil());
  }
  fseek(f, 0, SEEK_SET);
  char *src = xmalloc((size_t)sz + 1);
  size_t bytes_read = fread(src, 1, (size_t)sz, f);
  src[bytes_read] = '\0';
  fclose(f);

  /* Auto-inject include guard for modules/ files that lack a library statement.
   * services/stdlib.nx uses its own library guard — no injection needed. */
  const char *lib_name = NULL;
  if (strstr(fpath, "modules/")) {
    const char *last_slash = strrchr(fpath, '/');
    lib_name = last_slash ? last_slash + 1 : fpath;
  }

  EvalResult r = eval_str_lib(ctx, src, lib_name);
  xfree(src);
  return r;
}
