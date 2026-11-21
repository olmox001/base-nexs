/*
 * nexs_eval.h — NEXS Evaluator API
 * ===================================
 * CtrlSig enum, EvalResult struct, EvalCtx struct,
 * and the eval / eval_str / eval_file / eval_ctx_init declarations.
 */

#ifndef NEXS_EVAL_H
#define NEXS_EVAL_H
#pragma once

#include "nexs_ast.h"
#include "../../runtime/include/nexs_budget.h"
#include "../../core/include/nexs_value.h"
#include "../../core/include/nexs_common.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   CONTROL SIGNAL
   ========================================================= */

typedef enum {
  CTRL_NONE  = 0,
  CTRL_BREAK = 1,
  CTRL_CONT  = 2,
  CTRL_RET   = 3,
  CTRL_ERR   = 4,
} CtrlSig;

/* =========================================================
   EVAL RESULT
   ========================================================= */

typedef struct {
  CtrlSig sig;
  Value   ret_val;
} EvalResult;

/* =========================================================
   EVAL CONTEXT
   ========================================================= */

typedef struct {
  char scope[REG_PATH_MAX]; /* current scope path in /local/ */
  int  call_depth;
  int  debug;               /* 1 = print every evaluation step */
  FILE *out;                /* output stream (default stdout) */
  FILE *err;                /* error stream  (default stderr) */
  nx_operation_budget execution_budget; /* sole NXOLMO work-accounting owner */
  int fuel_limited; /* explicit owner admission before verified VM */
} EvalCtx;

/* =========================================================
   EVALUATOR API
   ========================================================= */

NEXS_API void       eval_ctx_init(EvalCtx *ctx);
/* Explicit bounded execution; unsupported/unadmitted callers still use the
 * old profile until a RuntimeInstance provider supplies mandatory budgets.
 * The budget never authenticates a capability or claims CPU-cycle accuracy. */
NEXS_API int        eval_ctx_set_fuel_budget(EvalCtx *ctx, uint64_t budget);
NEXS_API EvalResult eval(EvalCtx *ctx, ASTNode *node);
NEXS_API EvalResult eval_str(EvalCtx *ctx, const char *src);
NEXS_API EvalResult eval_str_lib(EvalCtx *ctx, const char *src,
                                 const char *lib_name);
NEXS_API EvalResult eval_file(EvalCtx *ctx, const char *path);

/* Global REPL context — set by main() before running repl.nx;
   used by the eval() builtin so .nx code can evaluate strings. */
extern EvalCtx *nexs_g_eval_ctx;

#ifdef __cplusplus
}
#endif

#endif /* NEXS_EVAL_H */
