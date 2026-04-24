/*
 * eval.c — NEXS Evaluator (Tree-Walking Interpreter)
 * =====================================================
 * Valuta l'albero AST nel contesto del registro gerarchico.
 *
 * BUG FIXES:
 *   - AST_INDEX: cerca prima nel registro, poi nella tabella array globale
 *   - eval_file: controlla il valore di ritorno di fread
 *   - eval_block: gestione corretta della memoria per il valore last
 */

#include "basereg.h"

#include <errno.h>
#include <string.h>

/* =========================================================
   INIZIALIZZAZIONE CONTESTO
   ========================================================= */

void eval_ctx_init(EvalCtx *ctx) {
  if (!ctx)
    return;
  strncpy(ctx->scope, REG_LOCAL, REG_PATH_MAX - 1);
  ctx->scope[REG_PATH_MAX - 1] = '\0';
  ctx->call_depth = 0;
  ctx->debug = 0;
  ctx->out = stdout;
  ctx->err = stderr;
}

/* =========================================================
   RESULT CONSTRUCTORS
   ========================================================= */

static EvalResult ok(Value v) { return (EvalResult){CTRL_NONE, v}; }
static EvalResult ctrl_break(void) { return (EvalResult){CTRL_BREAK, val_nil()}; }
static EvalResult ctrl_cont(void) { return (EvalResult){CTRL_CONT, val_nil()}; }
static EvalResult ctrl_ret(Value v) { return (EvalResult){CTRL_RET, v}; }
static EvalResult err_result(const char *msg) {
  return (EvalResult){CTRL_ERR, val_err(99, msg)};
}

/* =========================================================
   FORWARD DECLARATIONS
   ========================================================= */

static EvalResult eval_node(EvalCtx *ctx, ASTNode *n);

/* =========================================================
   EVAL ENTRY POINTS
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
    if (r.sig != CTRL_NONE)
      return r;
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

static EvalResult eval_node(EvalCtx *ctx, ASTNode *n) {
  if (!n)
    return ok(val_nil());

  switch (n->kind) {

  /* --- Letterali --- */
  case AST_INT_LIT:
  case AST_FLOAT_LIT:
  case AST_STR_LIT:
  case AST_BOOL_LIT:
  case AST_NIL_LIT:
    return ok(val_clone(&n->litval));

  /* --- Operazione binaria --- */
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
    if (!strcmp(op, "+"))       res = val_add(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "-"))  res = val_sub(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "*"))  res = val_mul(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "/"))  res = val_div(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "%"))  res = val_mod(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "==")) res = val_eq(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "!=")) res = val_ne(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "<"))  res = val_lt(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, ">"))  res = val_gt(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "<=")) res = val_le(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, ">=")) res = val_ge(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "&&")) res = val_and(&lr.ret_val, &rr.ret_val);
    else if (!strcmp(op, "||")) res = val_or(&lr.ret_val, &rr.ret_val);
    else                       res = val_err(5, "operatore sconosciuto");
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

  /* --- Identificatore (lookup nel registry) --- */
  case AST_IDENT: {
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (!k) {
      char msg[128];
      snprintf(msg, sizeof(msg), "nome non trovato: '%s'", n->name);
      return err_result(msg);
    }
    return ok(val_clone(&k->val));
  }

  /* --- Assegnazione variabile --- */
  case AST_ASSIGN: {
    EvalResult vr = eval_node(ctx, n->right);
    if (vr.sig != CTRL_NONE)
      return vr;
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", ctx->scope, n->name);
    reg_set(path, vr.ret_val, RK_ALL);
    return ok(val_clone(&vr.ret_val));
  }

  /* --- Indice array: arr[idx] --- */
  case AST_INDEX: {
    EvalResult ir = eval_node(ctx, n->left);
    if (ir.sig != CTRL_NONE)
      return ir;
    int64_t idx = val_to_int(&ir.ret_val);
    val_free(&ir.ret_val);

    /*
     * FIX: cerca prima nel registro (scope corrente),
     * poi nella tabella array globale. Il vecchio codice
     * cercava solo nella tabella globale.
     */
    DynArray *arr = NULL;

    /* 1. Cerca nel registro scope corrente */
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", ctx->scope, n->name);
    RegKey *k = reg_lookup(path);
    if (k && k->val.type == TYPE_ARR && k->val.data) {
      arr = (DynArray *)k->val.data;
    }

    /* 2. Cerca con reg_resolve (scope chain) */
    if (!arr) {
      k = reg_resolve(n->name, ctx->scope);
      if (k && k->val.type == TYPE_ARR && k->val.data)
        arr = (DynArray *)k->val.data;
    }

    /* 3. Fallback: tabella array globale */
    if (!arr)
      arr = arr_get(n->name);

    if (!arr)
      return err_result("array non trovato");
    if (idx < 0)
      return err_result("indice negativo");
    return ok(arr_get_at(arr, (size_t)idx));
  }

  /* --- Assegnazione indice: arr[idx] = val --- */
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
      return err_result("indice negativo");
    }
    DynArray *arr = arr_get_or_create(n->name);
    arr_set(arr, (size_t)idx, vr.ret_val);
    /* Registra l'array nel registry */
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
    return ok(val_clone(&vr.ret_val));
  }

  /* --- Cancella elemento array --- */
  case AST_DEL: {
    EvalResult ir = eval_node(ctx, n->left);
    if (ir.sig != CTRL_NONE)
      return ir;
    int64_t idx = val_to_int(&ir.ret_val);
    val_free(&ir.ret_val);
    DynArray *arr = arr_get(n->name);
    if (arr && idx >= 0)
      arr_delete(arr, (size_t)idx);
    return ok(val_nil());
  }

  /* --- Output --- */
  case AST_OUT: {
    EvalResult vr = eval_node(ctx, n->left);
    if (vr.sig != CTRL_NONE)
      return vr;
    val_print(&vr.ret_val, ctx->out);
    fprintf(ctx->out, "\n");
    val_free(&vr.ret_val);
    return ok(val_nil());
  }

  /* --- Accesso diretto al registro --- */
  case AST_REG_ACCESS: {
    Value v = reg_get(n->path);
    return ok(v);
  }

  /* --- Scrittura diretta nel registro --- */
  case AST_REG_SET: {
    EvalResult vr = eval_node(ctx, n->left);
    if (vr.sig != CTRL_NONE)
      return vr;
    reg_set(n->path, vr.ret_val, RK_ALL);
    return ok(val_clone(&vr.ret_val));
  }

  /* --- ls del registro --- */
  case AST_REG_LS: {
    reg_ls(n->path, ctx->out);
    return ok(val_nil());
  }

  /* --- Dichiarazione funzione --- */
  case AST_FN_DECL: {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", REG_FN, n->name);
    Value fv;
    fv.type = TYPE_FN;
    fv.data = n; /* ASTNode* è la definizione della funzione */
    fv.ival = 0;
    fv.fval = 0;
    fv.err_code = 0;
    fv.err_msg = NULL;
    reg_set(path, fv, RK_READ | RK_EXEC);
    return ok(val_nil());
  }

  /* --- Chiamata funzione --- */
  case AST_FN_CALL: {
    /* Valuta gli argomenti */
    Value args[MAX_PARAMS];
    int n_args = n->n_args;
    for (int i = 0; i < n_args; i++) {
      EvalResult ar = eval_node(ctx, n->args[i]);
      if (ar.sig != CTRL_NONE) {
        /* Cleanup degli argomenti già valutati */
        for (int j = 0; j < i; j++)
          val_free(&args[j]);
        return ar;
      }
      args[i] = ar.ret_val;
    }

    /* Risolvi la funzione nel registro */
    RegKey *k = reg_resolve(n->name, ctx->scope);
    if (!k || k->val.type != TYPE_FN) {
      char syspath[REG_PATH_MAX];
      snprintf(syspath, sizeof(syspath), "%s/%s", REG_SYS, n->name);
      k = reg_lookup(syspath);
    }
    if (!k || k->val.type != TYPE_FN) {
      char msg[128];
      snprintf(msg, sizeof(msg), "funzione non trovata: '%s'", n->name);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result(msg);
    }

    /* Built-in (puntatore a funzione C) */
    if (k->val.ival == 1) {
      BuiltinFn fn = (BuiltinFn)k->val.data;
      Value res = fn(args, n_args);
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return ok(res);
    }

    /* Funzione utente */
    if (ctx->call_depth >= MAX_CALL_DEPTH) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result("stack overflow: troppi livelli di ricorsione");
    }
    ASTNode *fn_node = (ASTNode *)k->val.data;
    if (!fn_node) {
      for (int i = 0; i < n_args; i++)
        val_free(&args[i]);
      return err_result("funzione vuota");
    }
    char *new_scope = reg_push_scope(n->name);
    ctx->call_depth++;

    /* Lega i parametri nello scope */
    for (int i = 0; i < fn_node->n_params && i < n_args; i++) {
      char ppath[REG_PATH_MAX];
      snprintf(ppath, sizeof(ppath), "%s/%s", new_scope, fn_node->params[i]);
      reg_set(ppath, args[i], RK_ALL);
    }

    char saved_scope[REG_PATH_MAX];
    strncpy(saved_scope, ctx->scope, REG_PATH_MAX - 1);
    saved_scope[REG_PATH_MAX - 1] = '\0';
    strncpy(ctx->scope, new_scope, REG_PATH_MAX - 1);
    ctx->scope[REG_PATH_MAX - 1] = '\0';

    EvalResult res = eval_block(ctx, fn_node->right);

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
      return eval_block(ctx, n->alt);
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
      /* CTRL_CONT o CTRL_NONE: continua */
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

  /* --- Programma / Blocco --- */
  case AST_PROGRAM:
    return eval_block(ctx, n);
  case AST_BLOCK:
    return eval_block(ctx, n);

  default:
    return err_result("nodo AST sconosciuto");
  }
}

/* =========================================================
   PIPELINE SHORTHAND
   ========================================================= */

EvalResult eval_str(EvalCtx *ctx, const char *src) {
  if (!ctx || !src)
    return err_result("ctx o src NULL");
  Lexer lex;
  Parser par;
  lexer_init(&lex, src);
  parser_init(&par, &lex);
  ASTNode *prog = parse_program(&par);
  if (par.had_error) {
    fprintf(ctx->err, "\033[1;31m[PARSE ERR]\033[0m %s\n", par.error_msg);
    ast_free(prog);
    return err_result(par.error_msg);
  }
  EvalResult r = eval(ctx, prog);
  /* Non liberiamo prog se contiene definizioni di funzione ancora usate nel
   * registro. In una versione futura: reference counting o GC */
  return r;
}

EvalResult eval_file(EvalCtx *ctx, const char *fpath) {
  if (!ctx || !fpath)
    return err_result("ctx o fpath NULL");
  FILE *f = fopen(fpath, "r");
  if (!f) {
    char msg[256];
    snprintf(msg, sizeof(msg), "impossibile aprire '%s': %s", fpath,
             strerror(errno));
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
  /* FIX: controlla il valore di ritorno di fread */
  size_t bytes_read = fread(src, 1, (size_t)sz, f);
  src[bytes_read] = '\0'; /* null-terminate at actual bytes read */
  fclose(f);
  EvalResult r = eval_str(ctx, src);
  xfree(src);
  return r;
}
