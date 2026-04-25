/*
 * lang/parser.c — NEXS Recursive-Descent Parser
 * ================================================
 * Converts token stream into AST.
 * Added parsing for: sendmessage, receivemessage, msgpending, ptr, deref.
 *
 * After fn_register() takes ownership of AST_FN_DECL.right (the body),
 * eval sets n->right = NULL so ast_free(prog) skips it automatically.
 */

#include "include/nexs_ast.h"
#include "include/nexs_lex.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <string.h>

/* =========================================================
   AST ALLOCATION
   ========================================================= */

ASTNode *ast_alloc(ASTKind kind, Token tok) {
  ASTNode *n = xmalloc(sizeof(ASTNode));
  n->kind     = kind;
  n->tok      = tok;
  n->litval   = val_nil();
  n->left     = n->right = n->children = n->next = n->alt = NULL;
  n->n_params = n->n_args = 0;
  memset(n->name,   0, NAME_LEN);
  memset(n->path,   0, REG_PATH_MAX);
  memset(n->op,     0, sizeof(n->op));
  return n;
}

void ast_free(ASTNode *n) {
  if (!n) return;
  ast_free(n->left);
  ast_free(n->right);
  ast_free(n->children);
  ast_free(n->next);
  ast_free(n->alt);
  for (int i = 0; i < n->n_args; i++)
    ast_free(n->args[i]);
  val_free(&n->litval);
  xfree(n);
}

/*
 * ast_free_safe: same as ast_free but explicitly NULLs the right child
 * of any AST_FN_DECL node before freeing it, as an extra safety measure.
 * In practice eval already sets n->right = NULL after fn_register(), so
 * ast_free() is also safe.  This version makes the invariant explicit.
 */
void ast_free_safe(ASTNode *n) {
  if (!n) return;
  if (n->kind == AST_FN_DECL) {
    /* fn_table owns the body — do not free it */
    n->right = NULL;
  }
  ast_free(n->left);
  ast_free(n->right);
  ast_free(n->children);
  ast_free(n->next);
  ast_free(n->alt);
  for (int i = 0; i < n->n_args; i++)
    ast_free(n->args[i]);
  val_free(&n->litval);
  xfree(n);
}

void ast_print(ASTNode *node, FILE *out, int depth) {
  if (!node || !out) return;
  for (int i = 0; i < depth; i++) fprintf(out, "  ");
  fprintf(out, "[%d] ", node->kind);
  if (node->name[0]) fprintf(out, "name='%s' ", node->name);
  if (node->path[0]) fprintf(out, "path='%s' ", node->path);
  if (node->op[0])   fprintf(out, "op='%s' ",   node->op);
  fprintf(out, "\n");
  if (node->left)    ast_print(node->left,    out, depth + 1);
  if (node->right)   ast_print(node->right,   out, depth + 1);
  if (node->alt)     ast_print(node->alt,     out, depth + 1);
  for (int i = 0; i < node->n_args; i++)
    ast_print(node->args[i], out, depth + 1);
  if (node->children) ast_print(node->children, out, depth + 1);
  if (node->next)    ast_print(node->next,    out, depth);
}

/* =========================================================
   PARSER HELPERS
   ========================================================= */

static Token parser_advance(Parser *p) {
  p->cur = lexer_next(p->lex);
  p->peek = (Token){.kind = TK_EOF};
  return p->cur;
}

static void parser_skip_newlines(Parser *p) {
  while (p->cur.kind == TK_NEWLINE || p->cur.kind == TK_EOF) {
    if (p->cur.kind == TK_EOF) break;
    parser_advance(p);
  }
}

static int parser_expect(Parser *p, TokenKind k) {
  if (p->cur.kind != k) {
    if (!p->had_error) {
      snprintf(p->error_msg, sizeof(p->error_msg),
               "Line %d: expected '%s', found '%s' ('%s')",
               p->cur.line, token_kind_name(k),
               token_kind_name(p->cur.kind), p->cur.text);
      p->had_error = 1;
    }
    return 0;
  }
  parser_advance(p);
  return 1;
}

/* =========================================================
   EXPRESSION PARSING
   ========================================================= */

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_block(Parser *p);

static ASTNode *parse_primary(Parser *p) {
  Token t = p->cur;
  switch (t.kind) {
  case TK_INT: {
    ASTNode *n = ast_alloc(AST_INT_LIT, t);
    n->litval = val_int(t.ival);
    parser_advance(p);
    return n;
  }
  case TK_FLOAT: {
    ASTNode *n = ast_alloc(AST_FLOAT_LIT, t);
    n->litval = val_float(t.fval);
    parser_advance(p);
    return n;
  }
  case TK_STRING: {
    ASTNode *n = ast_alloc(AST_STR_LIT, t);
    n->litval = val_str(t.text);
    parser_advance(p);
    return n;
  }
  case TK_KW_TRUE: {
    ASTNode *n = ast_alloc(AST_BOOL_LIT, t);
    n->litval = val_bool(1);
    parser_advance(p);
    return n;
  }
  case TK_KW_FALSE: {
    ASTNode *n = ast_alloc(AST_BOOL_LIT, t);
    n->litval = val_bool(0);
    parser_advance(p);
    return n;
  }
  case TK_KW_NIL: {
    ASTNode *n = ast_alloc(AST_NIL_LIT, t);
    parser_advance(p);
    return n;
  }
  case TK_IDENT: {
    parser_advance(p);
    if (p->cur.kind == TK_LBRACKET) {
      parser_advance(p);
      ASTNode *idx = parse_expr(p);
      if (!idx) return NULL;
      if (!parser_expect(p, TK_RBRACKET)) return NULL;
      ASTNode *n = ast_alloc(AST_INDEX, t);
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->left = idx;
      return n;
    }
    if (p->cur.kind == TK_LPAREN) {
      parser_advance(p);
      ASTNode *n = ast_alloc(AST_FN_CALL, t);
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF && !p->had_error) {
        ASTNode *arg = parse_expr(p);
        if (!arg) break;
        if (n->n_args < MAX_PARAMS) n->args[n->n_args++] = arg;
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return n;
    }
    ASTNode *n = ast_alloc(AST_IDENT, t);
    strncpy(n->name, t.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    return n;
  }
  case TK_KW_REG: {
    parser_advance(p);
    if (p->cur.kind != TK_REGPATH) return NULL;
    Token pt = p->cur;
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_REG_ACCESS, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }
  case TK_REGPATH: {
    ASTNode *n;
    parser_advance(p);
    if (p->cur.kind == TK_EQ) {
      parser_advance(p);
      n = ast_alloc(AST_REG_SET, t);
      strncpy(n->path, t.text, REG_PATH_MAX - 1);
      n->path[REG_PATH_MAX - 1] = '\0';
      n->left = parse_expr(p);
    } else {
      n = ast_alloc(AST_REG_ACCESS, t);
      strncpy(n->path, t.text, REG_PATH_MAX - 1);
      n->path[REG_PATH_MAX - 1] = '\0';
    }
    return n;
  }
  case TK_LPAREN: {
    parser_advance(p);
    ASTNode *n = parse_expr(p);
    parser_expect(p, TK_RPAREN);
    return n;
  }
  case TK_NOT:
  case TK_KW_NOT: {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_UNOP, t);
    strncpy(n->op, "!", 3);
    n->left = parse_primary(p);
    return n;
  }
  /* IPC / PTR keywords usable as expressions (e.g. out msgpending /path)
   * Also support function-call form: out msgpending("/q")                */
  case TK_KW_PENDING: {
    Token kw = t;
    parser_advance(p);
    if (p->cur.kind == TK_LPAREN) {
      ASTNode *fn = ast_alloc(AST_FN_CALL, kw);
      strncpy(fn->name, "msgpending", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS) fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return fn;
    }
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_MSG_PENDING, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }
  case TK_KW_RECV: {
    Token kw = t;
    parser_advance(p);
    if (p->cur.kind == TK_LPAREN) {
      ASTNode *fn = ast_alloc(AST_FN_CALL, kw);
      strncpy(fn->name, "receivemessage", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS) fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return fn;
    }
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_RECV_MSG, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }
  case TK_KW_DEREF: {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_PTR_DEREF, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }
  default:
    snprintf(p->error_msg, sizeof(p->error_msg),
             "Line %d: unexpected token '%s'", t.line, t.text);
    p->had_error = 1;
    return NULL;
  }
}

static int binop_prec(TokenKind k) {
  switch (k) {
  case TK_OR:  case TK_KW_OR:  return 1;
  case TK_AND: case TK_KW_AND: return 2;
  case TK_EQEQ: case TK_NEQ:  return 3;
  case TK_LT: case TK_GT:
  case TK_LE: case TK_GE:     return 4;
  case TK_PLUS: case TK_MINUS: return 5;
  case TK_STAR: case TK_SLASH:
  case TK_PERCENT:             return 6;
  default:                     return 0;
  }
}

static const char *binop_name(TokenKind k) {
  switch (k) {
  case TK_PLUS:    return "+";
  case TK_MINUS:   return "-";
  case TK_STAR:    return "*";
  case TK_SLASH:   return "/";
  case TK_PERCENT: return "%";
  case TK_EQEQ:   return "==";
  case TK_NEQ:    return "!=";
  case TK_LT:     return "<";
  case TK_GT:     return ">";
  case TK_LE:     return "<=";
  case TK_GE:     return ">=";
  case TK_AND: case TK_KW_AND: return "&&";
  case TK_OR:  case TK_KW_OR:  return "||";
  default:        return "?";
  }
}

static ASTNode *parse_binop(Parser *p, int min_prec) {
  ASTNode *left = parse_primary(p);
  if (!left) return NULL;
  while (1) {
    int prec = binop_prec(p->cur.kind);
    if (p->cur.kind == TK_REGPATH && strcmp(p->cur.text, "/") == 0)
      prec = 6;
    if (prec < min_prec) break;
    Token op = p->cur;
    if (op.kind == TK_REGPATH && strcmp(op.text, "/") == 0)
      op.kind = TK_SLASH;
    parser_advance(p);
    ASTNode *right = parse_binop(p, prec + 1);
    if (!right) return left;
    ASTNode *n = ast_alloc(AST_BINOP, op);
    strncpy(n->op, binop_name(op.kind), 3);
    n->op[3] = '\0';
    n->left  = left;
    n->right = right;
    left = n;
  }
  return left;
}

static ASTNode *parse_expr(Parser *p) { return parse_binop(p, 1); }

/* =========================================================
   STATEMENT PARSING
   ========================================================= */

static ASTNode *parse_block(Parser *p) {
  Token t = p->cur;
  if (!parser_expect(p, TK_LBRACE)) return NULL;
  parser_skip_newlines(p);
  ASTNode *block = ast_alloc(AST_BLOCK, t);
  ASTNode *last  = NULL;
  while (p->cur.kind != TK_RBRACE && p->cur.kind != TK_EOF && !p->had_error) {
    ASTNode *s = parse_stmt(p);
    if (s) {
      if (!block->children) block->children = s;
      else last->next = s;
      last = s;
    }
    parser_skip_newlines(p);
  }
  if (!parser_expect(p, TK_RBRACE)) return block;
  return block;
}

ASTNode *parse_stmt(Parser *p) {
  parser_skip_newlines(p);
  Token t = p->cur;

  /* fn name(params) { body } */
  if (t.kind == TK_KW_FN) {
    parser_advance(p);
    Token name_tok = p->cur;
    if (!parser_expect(p, TK_IDENT)) return NULL;
    ASTNode *n = ast_alloc(AST_FN_DECL, name_tok);
    strncpy(n->name, name_tok.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    if (!parser_expect(p, TK_LPAREN)) return NULL;
    while (p->cur.kind == TK_IDENT && n->n_params < MAX_PARAMS) {
      strncpy(n->params[n->n_params], p->cur.text, NAME_LEN - 1);
      n->params[n->n_params][NAME_LEN - 1] = '\0';
      n->n_params++;
      parser_advance(p);
      if (p->cur.kind == TK_COMMA) parser_advance(p);
    }
    if (!parser_expect(p, TK_RPAREN)) return NULL;
    n->right = parse_block(p);
    return n;
  }

  /* if cond { then } [else { els }] */
  if (t.kind == TK_KW_IF) {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_IF, t);
    n->left  = parse_expr(p);
    n->right = parse_block(p);
    if (p->cur.kind == TK_KW_ELSE) {
      parser_advance(p);
      n->alt = parse_block(p);
    }
    return n;
  }

  /* loop { body } */
  if (t.kind == TK_KW_LOOP) {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_LOOP, t);
    n->right = parse_block(p);
    return n;
  }

  /* break / cont / ret */
  if (t.kind == TK_KW_BREAK) { parser_advance(p); return ast_alloc(AST_BREAK, t); }
  if (t.kind == TK_KW_CONT)  { parser_advance(p); return ast_alloc(AST_CONT,  t); }
  if (t.kind == TK_KW_RET) {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_RET, t);
    if (p->cur.kind != TK_NEWLINE && p->cur.kind != TK_RBRACE && p->cur.kind != TK_EOF)
      n->left = parse_expr(p);
    return n;
  }

  /* out expr */
  if (t.kind == TK_KW_OUT) {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_OUT, t);
    n->left = parse_expr(p);
    return n;
  }

  /* del name[idx] */
  if (t.kind == TK_KW_DEL) {
    parser_advance(p);
    Token nt = p->cur;
    if (!parser_expect(p, TK_IDENT)) return NULL;
    ASTNode *n = ast_alloc(AST_DEL, nt);
    strncpy(n->name, nt.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    if (!parser_expect(p, TK_LBRACKET)) return NULL;
    n->left = parse_expr(p);
    if (!parser_expect(p, TK_RBRACKET)) return NULL;
    return n;
  }

  /* ls /path */
  if (t.kind == TK_KW_LS) {
    parser_advance(p);
    ASTNode *n = ast_alloc(AST_REG_LS, t);
    if (p->cur.kind == TK_REGPATH) {
      strncpy(n->path, p->cur.text, REG_PATH_MAX - 1);
      n->path[REG_PATH_MAX - 1] = '\0';
      parser_advance(p);
    } else {
      strncpy(n->path, "/", REG_PATH_MAX - 1);
    }
    return n;
  }

  /* reg /path [= expr] */
  if (t.kind == TK_KW_REG) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    if (p->cur.kind == TK_EQ) {
      parser_advance(p);
      ASTNode *n = ast_alloc(AST_REG_SET, pt);
      strncpy(n->path, pt.text, REG_PATH_MAX - 1);
      n->path[REG_PATH_MAX - 1] = '\0';
      n->left = parse_expr(p);
      return n;
    }
    ASTNode *n = ast_alloc(AST_REG_ACCESS, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }

  /* sendmessage /path expr           — keyword form with literal path
   * sendmessage(path_expr, val_expr) — function-call form with any expression */
  if (t.kind == TK_KW_SEND) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      /* Function-call form: delegate to fn_call parsing */
      ASTNode *fn = ast_alloc(AST_FN_CALL, t);
      strncpy(fn->name, "sendmessage", NAME_LEN - 1);
      parser_advance(p); /* consume '(' */
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_SEND_MSG, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    n->left = parse_expr(p);
    return n;
  }

  /* receivemessage /path  or  receivemessage(path_expr) */
  if (t.kind == TK_KW_RECV) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      ASTNode *fn = ast_alloc(AST_FN_CALL, t);
      strncpy(fn->name, "receivemessage", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_RECV_MSG, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }

  /* msgpending /path  or  msgpending(path_expr) */
  if (t.kind == TK_KW_PENDING) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      ASTNode *fn = ast_alloc(AST_FN_CALL, t);
      strncpy(fn->name, "msgpending", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_MSG_PENDING, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }

  /* ptr /path = /target */
  if (t.kind == TK_KW_PTR) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    if (!parser_expect(p, TK_EQ)) return NULL;
    Token target_tok = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_PTR_SET, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    /* Store the target path in n->name (repurposed) */
    strncpy(n->name, target_tok.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    return n;
  }

  /* deref /path */
  if (t.kind == TK_KW_DEREF) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH)) return NULL;
    ASTNode *n = ast_alloc(AST_PTR_DEREF, pt);
    strncpy(n->path, pt.text, REG_PATH_MAX - 1);
    n->path[REG_PATH_MAX - 1] = '\0';
    return n;
  }

  /* name = expr  |  name[idx] = expr  |  expr */
  if (t.kind == TK_IDENT) {
    parser_advance(p);
    if (p->cur.kind == TK_LBRACKET) {
      parser_advance(p);
      ASTNode *idx = parse_expr(p);
      if (!parser_expect(p, TK_RBRACKET)) return NULL;
      if (p->cur.kind == TK_EQ) {
        parser_advance(p);
        ASTNode *n = ast_alloc(AST_INDEX_ASSIGN, t);
        strncpy(n->name, t.text, NAME_LEN - 1);
        n->name[NAME_LEN - 1] = '\0';
        n->left  = idx;
        n->right = parse_expr(p);
        return n;
      }
      ASTNode *n = ast_alloc(AST_INDEX, t);
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->left = idx;
      return n;
    }
    if (p->cur.kind == TK_EQ) {
      parser_advance(p);
      ASTNode *n = ast_alloc(AST_ASSIGN, t);
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->right = parse_expr(p);
      return n;
    }
    if (p->cur.kind == TK_LPAREN) {
      parser_advance(p);
      ASTNode *n = ast_alloc(AST_FN_CALL, t);
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF && !p->had_error) {
        ASTNode *arg = parse_expr(p);
        if (!arg) break;
        if (n->n_args < MAX_PARAMS) n->args[n->n_args++] = arg;
        if (p->cur.kind == TK_COMMA) parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) return NULL;
      return n;
    }
    ASTNode *n = ast_alloc(AST_IDENT, t);
    strncpy(n->name, t.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    return n;
  }

  return parse_expr(p);
}

/* =========================================================
   PARSER INIT & PARSE PROGRAM
   ========================================================= */

void parser_init(Parser *p, Lexer *lex) {
  if (!p || !lex) return;
  p->lex       = lex;
  p->had_error = 0;
  p->error_msg[0] = '\0';
  p->peek.kind = TK_EOF;
  parser_advance(p);
}

ASTNode *parse_program(Parser *p) {
  ASTNode *prog = ast_alloc(AST_PROGRAM, p->cur);
  ASTNode *last = NULL;
  parser_skip_newlines(p);
  while (p->cur.kind != TK_EOF && !p->had_error) {
    ASTNode *s = parse_stmt(p);
    if (s) {
      if (!prog->children) prog->children = s;
      else last->next = s;
      last = s;

      if (!p->had_error && p->cur.kind != TK_NEWLINE &&
          p->cur.kind != TK_EOF && p->cur.kind != TK_RBRACE) {
        snprintf(p->error_msg, sizeof(p->error_msg),
                 "Line %d: expected end of line after statement, found '%s'",
                 p->cur.line, p->cur.text);
        p->had_error = 1;
        break;
      }
    }
    parser_skip_newlines(p);
  }
  return prog;
}
