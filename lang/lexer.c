/*
 * lang/lexer.c — NEXS Lexer (Tokenizer)
 * ========================================
 * Converts source text into a stream of Tokens.
 * Added keywords: ptr, deref, sendmessage, receivemessage, msgpending.
 */

#include "include/nexs_lex.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   INIT
   ========================================================= */

void lexer_init(Lexer *lex, const char *src) {
  if (!lex || !src) return;
  lex->src      = src;
  lex->pos      = 0;
  lex->len      = strlen(src);
  lex->line     = 1;
  lex->col      = 1;
  lex->has_peek = 0;
}

/* =========================================================
   SKIP WHITESPACE & COMMENTS
   ========================================================= */

static void lexer_skip_ws(Lexer *lex) {
  while (lex->pos < lex->len) {
    char c = lex->src[lex->pos];
    if (c == '#') {
      while (lex->pos < lex->len && lex->src[lex->pos] != '\n')
        lex->pos++;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      lex->pos++;
      lex->col++;
    } else {
      break;
    }
  }
}

/* =========================================================
   TOKEN FACTORY
   ========================================================= */

static Token make_tok(TokenKind k, const char *text, int line) {
  Token t;
  memset(&t, 0, sizeof(t));
  t.kind = k;
  t.line = line;
  t.col  = 0;
  t.ival = 0;
  t.fval = 0.0;
  strncpy(t.text, text ? text : "", MAX_STR_LEN - 1);
  t.text[MAX_STR_LEN - 1] = '\0';
  return t;
}

/* =========================================================
   KEYWORDS TABLE
   ========================================================= */

static struct {
  const char *word;
  TokenKind   kind;
} keywords[] = {
  {"fn",             TK_KW_FN},
  {"ret",            TK_KW_RET},
  {"if",             TK_KW_IF},
  {"else",           TK_KW_ELSE},
  {"loop",           TK_KW_LOOP},
  {"break",          TK_KW_BREAK},
  {"cont",           TK_KW_CONT},
  {"del",            TK_KW_DEL},
  {"out",            TK_KW_OUT},
  {"reg",            TK_KW_REG},
  {"ls",             TK_KW_LS},
  {"import",         TK_KW_IMPORT},
  {"proc",           TK_KW_PROC},
  {"and",            TK_KW_AND},
  {"or",             TK_KW_OR},
  {"not",            TK_KW_NOT},
  {"true",           TK_KW_TRUE},
  {"false",          TK_KW_FALSE},
  {"nil",            TK_KW_NIL},
  /* New keywords */
  {"ptr",            TK_KW_PTR},
  {"deref",          TK_KW_DEREF},
  {"sendmessage",    TK_KW_SEND},
  {"receivemessage", TK_KW_RECV},
  {"msgpending",     TK_KW_PENDING},
  {NULL,             TK_EOF}
};

/* =========================================================
   LEXER NEXT TOKEN
   ========================================================= */

Token lexer_next(Lexer *lex) {
  if (lex->has_peek) {
    lex->has_peek = 0;
    return lex->peeked;
  }

  lexer_skip_ws(lex);
  if (lex->pos >= lex->len)
    return make_tok(TK_EOF, "", lex->line);

  int  line = lex->line;
  char c    = lex->src[lex->pos];
  char buf[MAX_STR_LEN];

  /* Newline */
  if (c == '\n') {
    lex->pos++;
    lex->line++;
    lex->col = 1;
    return make_tok(TK_NEWLINE, "\\n", line);
  }

  /* String literal */
  if (c == '"') {
    size_t i = 0;
    lex->pos++;
    while (lex->pos < lex->len && lex->src[lex->pos] != '"' && i < MAX_STR_LEN - 1) {
      char ch = lex->src[lex->pos++];
      if (ch == '\\' && lex->pos < lex->len) {
        ch = lex->src[lex->pos++];
        if      (ch == 'n')  ch = '\n';
        else if (ch == 't')  ch = '\t';
        else if (ch == '\\') ch = '\\';
        else if (ch == '"')  ch = '"';
        else if (ch == '0')  ch = '\0';
      }
      buf[i++] = ch;
    }
    buf[i] = '\0';
    if (lex->pos < lex->len) lex->pos++; /* closing " */
    return make_tok(TK_STRING, buf, line);
  }

  /* Number */
  if (isdigit((unsigned char)c) ||
      (c == '-' && lex->pos + 1 < lex->len &&
       isdigit((unsigned char)lex->src[lex->pos + 1]))) {
    size_t i = 0;
    int is_float = 0;
    if (c == '-') buf[i++] = lex->src[lex->pos++];
    while (lex->pos < lex->len &&
           (isdigit((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '.') &&
           i < MAX_STR_LEN - 1) {
      if (lex->src[lex->pos] == '.') is_float = 1;
      buf[i++] = lex->src[lex->pos++];
    }
    buf[i] = '\0';
    Token t = make_tok(is_float ? TK_FLOAT : TK_INT, buf, line);
    t.ival = atoll(buf);
    t.fval = atof(buf);
    return t;
  }

  /* Registry path: starts with '/' */
  if (c == '/') {
    size_t i = 0;
    size_t saved_pos = lex->pos;
    buf[i++] = lex->src[lex->pos++];

    while (lex->pos < lex->len &&
           (isalnum((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '_' || lex->src[lex->pos] == '/' ||
            lex->src[lex->pos] == '.' || lex->src[lex->pos] == ':' ||
            lex->src[lex->pos] == '-') &&
           i < MAX_STR_LEN - 1) {
      buf[i++] = lex->src[lex->pos++];
    }
    buf[i] = '\0';

    if (i >= 2 && (isalpha((unsigned char)buf[1]) || buf[1] == '_'))
      return make_tok(TK_REGPATH, buf, line);
    if (i == 1)
      return make_tok(TK_REGPATH, buf, line);
    lex->pos = saved_pos + 1;
    return make_tok(TK_SLASH, "/", line);
  }

  /* Identifier and keywords */
  if (isalpha((unsigned char)c) || c == '_') {
    size_t i = 0;
    while (lex->pos < lex->len &&
           (isalnum((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '_') &&
           i < MAX_STR_LEN - 1)
      buf[i++] = lex->src[lex->pos++];
    buf[i] = '\0';
    for (int j = 0; keywords[j].word; j++)
      if (strcmp(buf, keywords[j].word) == 0)
        return make_tok(keywords[j].kind, buf, line);
    return make_tok(TK_IDENT, buf, line);
  }

  /* Operators and symbols */
  lex->pos++;
  switch (c) {
  case '+': return make_tok(TK_PLUS,     "+",  line);
  case '-': return make_tok(TK_MINUS,    "-",  line);
  case '*': return make_tok(TK_STAR,     "*",  line);
  case '%': return make_tok(TK_PERCENT,  "%",  line);
  case '.': return make_tok(TK_DOT,      ".",  line);
  case '[': return make_tok(TK_LBRACKET, "[",  line);
  case ']': return make_tok(TK_RBRACKET, "]",  line);
  case '{': return make_tok(TK_LBRACE,   "{",  line);
  case '}': return make_tok(TK_RBRACE,   "}",  line);
  case '(': return make_tok(TK_LPAREN,   "(",  line);
  case ')': return make_tok(TK_RPAREN,   ")",  line);
  case ',': return make_tok(TK_COMMA,    ",",  line);
  case '=':
    if (lex->pos < lex->len && lex->src[lex->pos] == '=') {
      lex->pos++;
      return make_tok(TK_EQEQ, "==", line);
    }
    return make_tok(TK_EQ, "=", line);
  case '!':
    if (lex->pos < lex->len && lex->src[lex->pos] == '=') {
      lex->pos++;
      return make_tok(TK_NEQ, "!=", line);
    }
    return make_tok(TK_NOT, "!", line);
  case '<':
    if (lex->pos < lex->len && lex->src[lex->pos] == '=') {
      lex->pos++;
      return make_tok(TK_LE, "<=", line);
    }
    return make_tok(TK_LT, "<", line);
  case '>':
    if (lex->pos < lex->len && lex->src[lex->pos] == '=') {
      lex->pos++;
      return make_tok(TK_GE, ">=", line);
    }
    return make_tok(TK_GT, ">", line);
  case '&':
    if (lex->pos < lex->len && lex->src[lex->pos] == '&') {
      lex->pos++;
      return make_tok(TK_AND, "&&", line);
    }
    break;
  case '|':
    if (lex->pos < lex->len && lex->src[lex->pos] == '|') {
      lex->pos++;
      return make_tok(TK_OR, "||", line);
    }
    break;
  }
  snprintf(buf, sizeof(buf), "unknown character '%c'", c);
  return make_tok(TK_EOF, buf, line);
}

/* =========================================================
   PEEK
   ========================================================= */

Token lexer_peek(Lexer *lex) {
  if (!lex->has_peek) {
    lex->peeked   = lexer_next(lex);
    lex->has_peek = 1;
  }
  return lex->peeked;
}

/* =========================================================
   TOKEN KIND NAMES (DEBUG)
   ========================================================= */

const char *token_kind_name(TokenKind k) {
  switch (k) {
  case TK_EOF:        return "EOF";
  case TK_INT:        return "INT";
  case TK_FLOAT:      return "FLOAT";
  case TK_STRING:     return "STR";
  case TK_IDENT:      return "IDENT";
  case TK_REGPATH:    return "PATH";
  case TK_PLUS:       return "+";
  case TK_MINUS:      return "-";
  case TK_STAR:       return "*";
  case TK_SLASH:      return "/";
  case TK_EQ:         return "=";
  case TK_EQEQ:       return "==";
  case TK_NEQ:        return "!=";
  case TK_LT:         return "<";
  case TK_GT:         return ">";
  case TK_LE:         return "<=";
  case TK_GE:         return ">=";
  case TK_AND:        return "&&";
  case TK_OR:         return "||";
  case TK_NOT:        return "!";
  case TK_DOT:        return ".";
  case TK_LBRACKET:   return "[";
  case TK_RBRACKET:   return "]";
  case TK_LBRACE:     return "{";
  case TK_RBRACE:     return "}";
  case TK_LPAREN:     return "(";
  case TK_RPAREN:     return ")";
  case TK_NEWLINE:    return "NL";
  case TK_COMMA:      return ",";
  case TK_PERCENT:    return "%";
  case TK_KW_FN:      return "fn";
  case TK_KW_RET:     return "ret";
  case TK_KW_IF:      return "if";
  case TK_KW_ELSE:    return "else";
  case TK_KW_LOOP:    return "loop";
  case TK_KW_BREAK:   return "break";
  case TK_KW_CONT:    return "cont";
  case TK_KW_DEL:     return "del";
  case TK_KW_OUT:     return "out";
  case TK_KW_REG:     return "reg";
  case TK_KW_LS:      return "ls";
  case TK_KW_TRUE:    return "true";
  case TK_KW_FALSE:   return "false";
  case TK_KW_NIL:     return "nil";
  case TK_KW_IMPORT:  return "import";
  case TK_KW_PROC:    return "proc";
  case TK_KW_AND:     return "and";
  case TK_KW_OR:      return "or";
  case TK_KW_NOT:     return "not";
  case TK_KW_PTR:     return "ptr";
  case TK_KW_DEREF:   return "deref";
  case TK_KW_SEND:    return "sendmessage";
  case TK_KW_RECV:    return "receivemessage";
  case TK_KW_PENDING: return "msgpending";
  default:            return "?";
  }
}
