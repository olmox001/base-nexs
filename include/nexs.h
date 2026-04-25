/*
 * include/nexs.h — NEXS Public Embedding API
 * =============================================
 * Single-include header for embedding the NEXS runtime in a C application.
 *
 * Usage:
 *   #include "nexs.h"
 *
 *   nexs_runtime_init();
 *   EvalCtx ctx;
 *   eval_ctx_init(&ctx);
 *   EvalResult r = eval_str(&ctx, "x = 42\nout x");
 *   val_free(&r.ret_val);
 *
 * Build flags (add to your compiler invocation):
 *   -Icore/include -Iregistry/include -Ilang/include
 *   -Isys/include  -Iruntime/include  -Icompiler/include
 *   -Ihal/include
 *
 * No external dependencies beyond libc.
 */

#ifndef NEXS_H
#define NEXS_H
#pragma once

/* ---- Core ---- */
#include "../core/include/nexs_common.h"   /* constants, NEXS_API */
#include "../core/include/nexs_alloc.h"    /* buddy + page allocator */
#include "../core/include/nexs_value.h"    /* Value, DynArray, val_* */
#include "../core/include/nexs_utils.h"    /* path helpers, nexs_trim */

/* ---- Registry ---- */
#include "../registry/include/nexs_registry.h" /* reg_*, RegKey, IPC */

/* ---- Language ---- */
#include "../lang/include/nexs_lex.h"      /* Lexer, Token, TokenKind */
#include "../lang/include/nexs_ast.h"      /* ASTNode, ASTKind */
#include "../lang/include/nexs_fn.h"       /* fn_table, NexsFnDef */
#include "../lang/include/nexs_parse.h"    /* Parser, parse_program */
#include "../lang/include/nexs_eval.h"     /* EvalCtx, EvalResult, eval_* */

/* ---- Plan 9 Syscalls ---- */
#include "../sys/include/nexs_sys.h"       /* open/read/write/rfork/… */

/* ---- Runtime ---- */
#include "../runtime/include/nexs_runtime.h" /* nexs_runtime_init, REPL */

/* ---- Compiler (optional — not needed for interpret-only embedding) ---- */
#include "../compiler/include/nexs_compiler.h" /* nexs_compile_file */

/* ---- HAL Bytecode (optional — for bare-metal embedding) ---- */
#include "../hal/include/nexs_hal.h"       /* nexs_hal_print, nexs_hal_putc */
#include "../hal/include/nexs_hal_bc.h"    /* HALB executor + assembler */

/*
 * Quick-start macros — convenience wrappers over the core API.
 */

/* Evaluate a source string; returns 0 on success, -1 on CTRL_ERR. */
static inline int nexs_eval_cstr(EvalCtx *ctx, const char *src) {
  EvalResult r = eval_str(ctx, src);
  int ok = (r.sig != CTRL_ERR);
  val_free(&r.ret_val);
  return ok ? 0 : -1;
}

/* Read a registry integer (returns 0 if key absent or wrong type). */
static inline int64_t nexs_reg_int(const char *path) {
  Value v = reg_get(path);
  int64_t n = (v.type == TYPE_INT) ? v.ival : 0;
  val_free(&v);
  return n;
}

/* Read a registry string (writes up to bufsz bytes; returns buf). */
static inline char *nexs_reg_str(const char *path, char *buf, int bufsz) {
  Value v = reg_get(path);
  if (v.type == TYPE_STR && v.data && bufsz > 0) {
    int i = 0;
    const char *s = (const char *)v.data;
    while (i < bufsz - 1 && s[i]) { buf[i] = s[i]; i++; }
    buf[i] = '\0';
  } else if (bufsz > 0) {
    buf[0] = '\0';
  }
  val_free(&v);
  return buf;
}

#endif /* NEXS_H */
