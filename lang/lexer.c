/*
 * lang/lexer.c — NEXS Lexer (Tokenizer)
 * ========================================
 * Converts source text into a stream of Tokens.
 * Added keywords: ptr, deref, sendmessage, receivemessage, msgpending.
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "include/nexs_lex.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   INIT
   ========================================================= */

void lexer_init(Lexer *lex, const char *src) {
  if (!lex || !src)
    return;
  lex->src = src;
  lex->lib_name = NULL;
  lex->inject_stage = 0;
  lex->pos = 0;
  lex->len = strlen(src);
  lex->line = 1;
  lex->col = 1;
  lex->has_peek = 0;
}

void lexer_init_lib(Lexer *lex, const char *src, const char *lib_name) {
  lexer_init(lex, src);
  lex->lib_name = lib_name;
  lex->inject_stage =
      1; /* 1: TK_KW_LIBRARY, 2: TK_STRING, 3: TK_NEWLINE, 0: Done */
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
  t.col = 0;
  t.ival = 0;
  t.fval = 0.0;
  strncpy(t.text, text ? text : "", NAME_LEN - 1);
  t.text[NAME_LEN - 1] = '\0';
  return t;
}

/* Decimal-only integer parsing with an explicit int64_t limit.
 * It uses unsigned intermediate arithmetic, so even INT64_MIN is safe
 * and no hosted errno, strtoll or freestanding libc extension is needed. */
static int lexer_parse_integer(const char *text, int64_t *value) {
  if (text == NULL || value == NULL)
    return 0;
  size_t pos = text[0] == '-' ? 1U : 0U;
  int negative = pos != 0U;
  if (text[pos] == '\0')
    return 0;
  const uint64_t limit = ((~(uint64_t)0) >> 1U) +
                         (negative ? 1U : 0U);
  uint64_t number = 0U;
  while (text[pos] != '\0') {
    unsigned char ch = (unsigned char)text[pos++];
    if (ch < '0' || ch > '9')
      return 0;
    uint64_t digit = (uint64_t)(ch - '0');
    if (number > (limit - digit) / 10U)
      return 0;
    number = number * 10U + digit;
  }
  if (negative) {
    if (number == limit)
      *value = -(int64_t)(limit - 1U) - 1;
    else
      *value = -(int64_t)number;
  } else {
    *value = (int64_t)number;
  }
  return 1;
}

/* The lexical grammar is decimal digits with at most one decimal point.
 * With NAME_LEN at most 64 there is no exponent and no representable input
 * long enough to overflow binary64 via atof(). This also rejects 1.2.3. */
static int lexer_parse_float(const char *text, double *value) {
  if (text == NULL || value == NULL)
    return 0;
  size_t pos = text[0] == '-' ? 1U : 0U;
  size_t digits = 0U;
  size_t dots = 0U;
  while (text[pos] != '\0') {
    char ch = text[pos++];
    if (ch == '.')
      dots++;
    else if (ch >= '0' && ch <= '9')
      digits++;
    else
      return 0;
  }
  if (digits == 0U || dots != 1U)
    return 0;
  *value = atof(text);
  return 1;
}

/* =========================================================
   KEYWORDS TABLE
   ========================================================= */

static struct {
  const char *word;
  TokenKind kind;
} keywords[] = {{"fn", TK_KW_FN},
                {"ret", TK_KW_RET},
                {"if", TK_KW_IF},
                {"else", TK_KW_ELSE},
                {"loop", TK_KW_LOOP},
                {"break", TK_KW_BREAK},
                {"cont", TK_KW_CONT},
                {"continue", TK_KW_CONT}, /* alias — fix nxed_editor.nx */
                {"del", TK_KW_DEL},
                {"out", TK_KW_OUT},
                {"reg", TK_KW_REG},
                {"ls", TK_KW_LS},
                {"import", TK_KW_IMPORT},
                {"proc", TK_KW_PROC},
                {"and", TK_KW_AND},
                {"or", TK_KW_OR},
                {"not", TK_KW_NOT},
                {"true", TK_KW_TRUE},
                {"false", TK_KW_FALSE},
                {"nil", TK_KW_NIL},
                {"cd", TK_KW_CD},
                {"pwd", TK_KW_PWD},
                {"library", TK_KW_LIBRARY},
                {"module", TK_KW_MODULE},
                /* New keywords */
                {"ptr", TK_KW_PTR},
                {"deref", TK_KW_DEREF},
                {"sendmessage", TK_KW_SEND},
                {"receivemessage", TK_KW_RECV},
                {"msgpending", TK_KW_PENDING},
                {NULL, TK_EOF}};

/* =========================================================
   LEXER NEXT TOKEN
   ========================================================= */

Token lexer_next(Lexer *lex) {
  if (lex->has_peek) {
    lex->has_peek = 0;
    return lex->peeked;
  }

  /* Virtual Token Injection for libraries */
  if (lex->lib_name && lex->inject_stage > 0) {
    if (lex->inject_stage == 1) {
      lex->inject_stage = 2;
      return make_tok(TK_KW_LIBRARY, "library", 0);
    }
    if (lex->inject_stage == 2) {
      lex->inject_stage = 3;
      Token t = make_tok(TK_STRING, lex->lib_name, 0);
      t.span = lex->lib_name;
      t.span_len = strlen(lex->lib_name);
      t.span_escaped = 0;
      return t;
    }
    if (lex->inject_stage == 3) {
      lex->inject_stage = 0;
      return make_tok(TK_NEWLINE, "\n", 0);
    }
  }

  lexer_skip_ws(lex);
  if (lex->pos >= lex->len)
    return make_tok(TK_EOF, "", lex->line);

  int line = lex->line;
  char c = lex->src[lex->pos];
  char buf[MAX_STR_LEN];

  /* Newline */
  if (c == '\n') {
    lex->pos++;
    lex->line++;
    lex->col = 1;
    return make_tok(TK_NEWLINE, "\\n", line);
  }

  /* String literal: retain its complete source slice. No fixed-size
   * string copy, no embedded NUL accepted, no silent length truncation. */
  if (c == '"') {
    lex->pos++;
    size_t start = lex->pos;
    size_t decoded = 0;
    int closed = 0;
    int oversized = 0;
    while (lex->pos < lex->len && !closed) {
      char ch = lex->src[lex->pos++];
      if (ch == '"') {
        closed = 1;
      } else {
        if (ch == '\\' && lex->pos < lex->len) {
          ch = lex->src[lex->pos++];
          /* A C-style octal escape encodes one byte: \\033 is ESC.
           * Count the whole escape as one decoded character. */
          if (ch >= '0' && ch <= '7') {
            for (int digit = 0; digit < 2 && lex->pos < lex->len &&
                 lex->src[lex->pos] >= '0' &&
                 lex->src[lex->pos] <= '7'; ++digit)
              lex->pos++;
          }
        }
        if (ch == '\n') {
          lex->line++;
          lex->col = 1;
        }
        if (decoded >= MAX_STR_LEN - 1U)
          oversized = 1;
        decoded++;
      }
    }
    if (!closed)
      return make_tok(TK_ERROR, "unterminated string literal", line);
    if (oversized)
      return make_tok(TK_ERROR, "string literal exceeds MAX_STR_LEN", line);
    Token t = make_tok(TK_STRING, "", line);
    t.span = lex->src + start;
    t.span_len = lex->pos - start - 1U;
    t.span_escaped = 1;
    return t;
  }

  /* Number: consume the complete lexeme; never split an oversized
   * number into two valid tokens or pass a truncated value to strtoll. */
  if (isdigit((unsigned char)c) ||
      (c == '-' && lex->pos + 1U < lex->len &&
       isdigit((unsigned char)lex->src[lex->pos + 1U]))) {
    size_t i = 0;
    int is_float = 0;
    if (c == '-') {
      buf[i++] = lex->src[lex->pos++];
    }
    while (lex->pos < lex->len &&
           (isdigit((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '.')) {
      char next = lex->src[lex->pos++];
      if (next == '.')
        is_float = 1;
      if (i < NAME_LEN - 1U)
        buf[i] = next;
      i++;
    }
    if (i >= NAME_LEN)
      return make_tok(TK_ERROR, "number exceeds NAME_LEN", line);
    buf[i] = '\0';
    Token t = make_tok(is_float ? TK_FLOAT : TK_INT, buf, line);
    int valid = is_float ? lexer_parse_float(buf, &t.fval)
                         : lexer_parse_integer(buf, &t.ival);
    if (!valid)
      return make_tok(TK_ERROR, "invalid or out-of-range number", line);
    return t;
  }

  /* Registry paths use a borrowed slice. The parser copies the
   * entire bounded path (up to REG_PATH_MAX - 1), not text[NAME_LEN]. */
  if (c == '/') {
    size_t start = lex->pos++;
    while (lex->pos < lex->len &&
           (isalnum((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '_' || lex->src[lex->pos] == '/')) {
      lex->pos++;
    }
    size_t n = lex->pos - start;
    if (n == 1U || (n >= 2U &&
        (isalpha((unsigned char)lex->src[start + 1U]) ||
         lex->src[start + 1U] == '_'))) {
      if (n >= REG_PATH_MAX)
        return make_tok(TK_ERROR, "registry path exceeds REG_PATH_MAX", line);
      /* Registry currently stores each path component in name[NAME_LEN].
       * Reject unsupported components instead of creating unreachable,
       * truncated keys. Complete dynamic names belong to P3/NXREG. */
      size_t component = 0;
      for (size_t j = start + 1U; j < lex->pos; ++j) {
        if (lex->src[j] == '/') {
          component = 0;
        } else {
          component++;
          if (component >= NAME_LEN)
            return make_tok(TK_ERROR, "registry component exceeds NAME_LEN", line);
        }
      }
      Token t = make_tok(TK_REGPATH, "/", line);
      size_t preview = n < NAME_LEN ? n : NAME_LEN - 1U;
      memcpy(t.text, lex->src + start, preview);
      t.text[preview] = '\0';
      t.span = lex->src + start;
      t.span_len = n;
      return t;
    }
    lex->pos = start + 1U;
    return make_tok(TK_SLASH, "/", line);
  }

  /* Identifiers have an explicitly bounded ABI name field.
   * Oversized names are syntax errors; aliases are never silently created. */
  if (isalpha((unsigned char)c) || c == '_') {
    size_t i = 0;
    while (lex->pos < lex->len &&
           (isalnum((unsigned char)lex->src[lex->pos]) ||
            lex->src[lex->pos] == '_')) {
      char next = lex->src[lex->pos++];
      if (i < NAME_LEN - 1U)
        buf[i] = next;
      i++;
    }
    if (i >= NAME_LEN)
      return make_tok(TK_ERROR, "identifier exceeds NAME_LEN", line);
    buf[i] = '\0';
    for (int j = 0; keywords[j].word; j++)
      if (strcmp(buf, keywords[j].word) == 0)
        return make_tok(keywords[j].kind, buf, line);
    return make_tok(TK_IDENT, buf, line);
  }

  /* Operators and symbols */
  lex->pos++;
  switch (c) {
  case '+':
    return make_tok(TK_PLUS, "+", line);
  case '-':
    return make_tok(TK_MINUS, "-", line);
  case '*':
    return make_tok(TK_STAR, "*", line);
  case '%':
    return make_tok(TK_PERCENT, "%", line);
  case '.':
    return make_tok(TK_DOT, ".", line);
  case '[':
    return make_tok(TK_LBRACKET, "[", line);
  case ']':
    return make_tok(TK_RBRACKET, "]", line);
  case '{':
    return make_tok(TK_LBRACE, "{", line);
  case '}':
    return make_tok(TK_RBRACE, "}", line);
  case '(':
    return make_tok(TK_LPAREN, "(", line);
  case ')':
    return make_tok(TK_RPAREN, ")", line);
  case ',':
    return make_tok(TK_COMMA, ",", line);
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
  return make_tok(TK_ERROR, buf, line);
}

/* Copy a complete source slice into a caller-owned, bounded destination.
 * On failure, discard output; embedded '\0' must not forge a shorter string. */
int lexer_token_copy(const Token *token, char *out, size_t capacity) {
  if (token == NULL || out == NULL || capacity == 0)
    return 0;
  out[0] = '\0';
  const char *source = token->text;
  size_t bytes = strlen(token->text);
  if ((token->kind == TK_STRING || token->kind == TK_REGPATH) &&
      token->span != NULL) {
    source = token->span;
    bytes = token->span_len;
  }
  size_t used = 0;
  for (size_t i = 0; i < bytes; i++) {
    unsigned char ch = (unsigned char)source[i];
    if (token->kind == TK_STRING && token->span_escaped &&
        ch == '\\' && i + 1U < bytes) {
      ch = (unsigned char)source[++i];
      if (ch == 'n') ch = '\n';
      else if (ch == 'r') ch = '\r';
      else if (ch == 'b') ch = '\b';
      else if (ch == 'e') ch = '\x1b';
      else if (ch == 't') ch = '\t';
      else if (ch >= '0' && ch <= '7') {
        unsigned int octal = (unsigned int)(ch - '0');
        for (unsigned int digit = 0U; digit < 2U && i + 1U < bytes &&
             source[i + 1U] >= '0' && source[i + 1U] <= '7'; ++digit) {
          ++i;
          octal = octal * 8U + (unsigned int)(source[i] - '0');
        }
        if (octal > 255U) {
          out[0] = '\0';
          return 0;
        }
        ch = (unsigned char)octal;
      }
    }
    if (ch == 0 || used >= capacity - 1U) {
      out[0] = '\0';
      return 0;
    }
    out[used++] = (char)ch;
  }
  out[used] = '\0';
  return 1;
}

/* =========================================================
   PEEK
   ========================================================= */

Token lexer_peek(Lexer *lex) {
  if (!lex->has_peek) {
    lex->peeked = lexer_next(lex);
    lex->has_peek = 1;
  }
  return lex->peeked;
}

/* =========================================================
   TOKEN KIND NAMES (DEBUG)
   ========================================================= */

const char *token_kind_name(TokenKind k) {
  switch (k) {
  case TK_ERROR:
    return "ERROR";
  case TK_EOF:
    return "EOF";
  case TK_INT:
    return "INT";
  case TK_FLOAT:
    return "FLOAT";
  case TK_STRING:
    return "STR";
  case TK_IDENT:
    return "IDENT";
  case TK_REGPATH:
    return "PATH";
  case TK_PLUS:
    return "+";
  case TK_MINUS:
    return "-";
  case TK_STAR:
    return "*";
  case TK_SLASH:
    return "/";
  case TK_EQ:
    return "=";
  case TK_EQEQ:
    return "==";
  case TK_NEQ:
    return "!=";
  case TK_LT:
    return "<";
  case TK_GT:
    return ">";
  case TK_LE:
    return "<=";
  case TK_GE:
    return ">=";
  case TK_AND:
    return "&&";
  case TK_OR:
    return "||";
  case TK_NOT:
    return "!";
  case TK_DOT:
    return ".";
  case TK_LBRACKET:
    return "[";
  case TK_RBRACKET:
    return "]";
  case TK_LBRACE:
    return "{";
  case TK_RBRACE:
    return "}";
  case TK_LPAREN:
    return "(";
  case TK_RPAREN:
    return ")";
  case TK_NEWLINE:
    return "NL";
  case TK_COMMA:
    return ",";
  case TK_PERCENT:
    return "%";
  case TK_KW_FN:
    return "fn";
  case TK_KW_RET:
    return "ret";
  case TK_KW_IF:
    return "if";
  case TK_KW_ELSE:
    return "else";
  case TK_KW_LOOP:
    return "loop";
  case TK_KW_BREAK:
    return "break";
  case TK_KW_CONT:
    return "cont";
  case TK_KW_DEL:
    return "del";
  case TK_KW_OUT:
    return "out";
  case TK_KW_REG:
    return "reg";
  case TK_KW_LS:
    return "ls";
  case TK_KW_TRUE:
    return "true";
  case TK_KW_FALSE:
    return "false";
  case TK_KW_NIL:
    return "nil";
  case TK_KW_IMPORT:
    return "import";
  case TK_KW_PROC:
    return "proc";
  case TK_KW_AND:
    return "and";
  case TK_KW_OR:
    return "or";
  case TK_KW_NOT:
    return "not";
  case TK_KW_PTR:
    return "ptr";
  case TK_KW_DEREF:
    return "deref";
  case TK_KW_SEND:
    return "sendmessage";
  case TK_KW_RECV:
    return "receivemessage";
  case TK_KW_PENDING:
    return "msgpending";
  case TK_KW_LIBRARY:
    return "library";
  case TK_KW_MODULE:
    return "module";
  default:
    return "?";
  }
}
