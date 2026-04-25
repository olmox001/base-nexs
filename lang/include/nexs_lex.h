/*
 * nexs_lex.h — NEXS Lexer API
 * ==============================
 * TokenKind enum (with new IPC/ptr keywords), Token struct, Lexer struct,
 * and lexer_init / lexer_next / token_kind_name.
 */

#ifndef NEXS_LEX_H
#define NEXS_LEX_H
#pragma once

#include "../../core/include/nexs_common.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   TOKEN KIND ENUM
   ========================================================= */

typedef enum {
  TK_EOF = 0,

  /* Literals */
  TK_INT,
  TK_FLOAT,
  TK_STRING,
  TK_IDENT,
  TK_REGPATH,

  /* Operators */
  TK_PLUS,
  TK_MINUS,
  TK_STAR,
  TK_SLASH,
  TK_PERCENT,
  TK_EQ,
  TK_EQEQ,
  TK_NEQ,
  TK_LT,
  TK_GT,
  TK_LE,
  TK_GE,
  TK_AND,
  TK_OR,
  TK_NOT,
  TK_DOT,

  /* Delimiters */
  TK_LBRACKET,
  TK_RBRACKET,
  TK_LBRACE,
  TK_RBRACE,
  TK_LPAREN,
  TK_RPAREN,
  TK_COMMA,
  TK_NEWLINE,

  /* Keywords */
  TK_KW_FN,
  TK_KW_RET,
  TK_KW_IF,
  TK_KW_ELSE,
  TK_KW_LOOP,
  TK_KW_BREAK,
  TK_KW_CONT,
  TK_KW_DEL,
  TK_KW_OUT,
  TK_KW_REG,
  TK_KW_LS,
  TK_KW_IMPORT,
  TK_KW_PROC,
  TK_KW_AND,
  TK_KW_OR,
  TK_KW_NOT,
  TK_KW_TRUE,
  TK_KW_FALSE,
  TK_KW_NIL,

  /* New keywords — pointer and IPC */
  TK_KW_PTR,      /* ptr    — create a pointer to a registry path */
  TK_KW_DEREF,    /* deref  — dereference a pointer */
  TK_KW_SEND,     /* sendmessage    — enqueue a value on a registry IPC queue */
  TK_KW_RECV,     /* receivemessage — dequeue a value from a registry IPC queue */
  TK_KW_PENDING,  /* msgpending     — query pending message count */
} TokenKind;

/* =========================================================
   TOKEN STRUCT
   ========================================================= */

typedef struct {
  TokenKind kind;
  char      text[MAX_STR_LEN];
  int64_t   ival;
  double    fval;
  int       line;
  int       col;
} Token;

/* =========================================================
   LEXER STRUCT
   ========================================================= */

typedef struct {
  const char *src;
  size_t      pos;
  size_t      len;
  int         line;
  int         col;
  Token       peeked;
  int         has_peek;
} Lexer;

/* =========================================================
   LEXER API
   ========================================================= */

NEXS_API void        lexer_init(Lexer *lex, const char *src);
NEXS_API Token       lexer_next(Lexer *lex);
NEXS_API Token       lexer_peek(Lexer *lex);
NEXS_API const char *token_kind_name(TokenKind k);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_LEX_H */
