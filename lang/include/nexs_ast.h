/*
 * nexs_ast.h — NEXS AST Node Definitions
 * ==========================================
 * ASTKind enum (with new ptr/IPC node kinds), ASTNode struct,
 * Parser struct, and ast_alloc / ast_free / ast_free_safe / ast_print.
 */

#ifndef NEXS_AST_H
#define NEXS_AST_H
#pragma once

#include "nexs_lex.h"
#include "../../core/include/nexs_value.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   AST KIND ENUM
   ========================================================= */

typedef enum {
  AST_PROGRAM,
  AST_ASSIGN,
  AST_INDEX_ASSIGN,
  AST_IDENT,
  AST_INDEX,
  AST_REG_ACCESS,
  AST_INT_LIT,
  AST_FLOAT_LIT,
  AST_STR_LIT,
  AST_BOOL_LIT,
  AST_NIL_LIT,
  AST_BINOP,
  AST_UNOP,
  AST_FN_DECL,
  AST_FN_CALL,
  AST_IF,
  AST_LOOP,
  AST_BREAK,
  AST_CONT,
  AST_RET,
  AST_DEL,
  AST_OUT,
  AST_REG_SET,
  AST_REG_LS,
  AST_BLOCK,

  /* New: pointer and IPC nodes */
  AST_PTR_SET,     /* ptr /path = /target  — create TYPE_PTR at /path -> /target */
  AST_PTR_DEREF,   /* deref /path          — follow ptr chain, return value */
  AST_SEND_MSG,    /* sendmessage /path expr  — enqueue expr onto queue at /path */
  AST_RECV_MSG,    /* receivemessage /path    — dequeue from queue at /path */
  AST_MSG_PENDING, /* msgpending /path        — count pending messages at /path */
} ASTKind;

/* =========================================================
   AST NODE STRUCT
   ========================================================= */

typedef struct ASTNode ASTNode;
struct ASTNode {
  ASTKind kind;
  Token   tok;
  Value   litval;
  char    name[NAME_LEN];
  char    path[REG_PATH_MAX];
  char    op[4];
  char    params[MAX_PARAMS][NAME_LEN];
  int     n_params;
  ASTNode *left;
  ASTNode *right;
  ASTNode *args[MAX_PARAMS];
  int      n_args;
  ASTNode *children;
  ASTNode *next;
  ASTNode *alt;
};

/* =========================================================
   PARSER STRUCT
   ========================================================= */

typedef struct {
  Lexer *lex;
  Token  cur;
  Token  peek;
  int    had_error;
  char   error_msg[256];
} Parser;

/* =========================================================
   AST API
   ========================================================= */

NEXS_API ASTNode *ast_alloc(ASTKind kind, Token tok);
NEXS_API void     ast_print(ASTNode *node, FILE *out, int depth);

/*
 * ast_free: recursively free all nodes.
 * For AST_FN_DECL, if n->right has been transferred to the fn_table
 * (set to NULL by eval after fn_register), it is skipped automatically.
 */
NEXS_API void ast_free(ASTNode *n);

/*
 * ast_free_safe: same as ast_free but also NULLs out any AST_FN_DECL
 * right-child before freeing, as a safety measure.  Prefer using this
 * in eval_str after fn_register has already NULLed the body.
 */
NEXS_API void ast_free_safe(ASTNode *n);

/* =========================================================
   PARSER API (declarations only; implementation in nexs_parse.h)
   ========================================================= */

NEXS_API void     parser_init(Parser *p, Lexer *lex);
NEXS_API ASTNode *parse_program(Parser *p);
NEXS_API ASTNode *parse_stmt(Parser *p);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_AST_H */
