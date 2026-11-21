/*
 * lang/parser.c — NEXS Recursive-Descent Parser
 * ================================================
 * Converts token stream into AST.
 * Added parsing for: sendmessage, receivemessage, msgpending, ptr, deref.
 *
 * After fn_register() takes ownership of AST_FN_DECL.right (the body),
 * eval sets n->right = NULL so ast_free(prog) skips it automatically.
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_value.h"
#include "include/nexs_ast.h"
#include "include/nexs_lex.h"

#include <string.h>

/* =========================================================
   AST ALLOCATION
   ========================================================= */

ASTNode *ast_alloc(ASTKind kind, Token tok) {
  ASTNode *n = nexs_alloc(sizeof(ASTNode));
  if (n == NULL)
    return NULL;
  /* nexs_alloc zeroes all child/argument and allocation-chain pointers. */
  n->kind = kind;
  n->tok = tok;
  /* Source spans are borrowed exclusively while parsing. Function bodies
   * may outlive the source buffer in the function table; never retain a
   * dangling source pointer in their AST diagnostic token. */
  n->tok.span = NULL;
  n->tok.span_len = 0;
  n->tok.span_escaped = 0;
  n->litval = val_nil();
  n->left = n->right = n->children = n->next = n->alt = NULL;
  n->n_params = n->n_args = 0;
  memset(n->name, 0, NAME_LEN);
  memset(n->path, 0, REG_PATH_MAX);
  memset(n->op, 0, sizeof(n->op));
  return n;
}

/* Every parser-created node is owned by its current parse transaction until
 * the returned AST has been validated. On a failed parse, the same nodes
 * are freed once via an intrusive allocation chain, even if syntax failure
 * stranded some of them outside the partially assembled tree. */
static ASTNode *parser_ast_alloc(Parser *parser, ASTKind kind, Token tok) {
  if (parser == NULL || parser->had_error)
    return NULL;
  ASTNode *node = ast_alloc(kind, tok);
  if (node == NULL) {
    parser->had_error = 1;
    snprintf(parser->error_msg, sizeof(parser->error_msg),
             "Line %d: AST allocation failed", tok.line);
    return NULL;
  }
  node->allocation_next = parser->owned_allocations;
  parser->owned_allocations = node;
  return node;
}

static void parser_discard_allocations(Parser *parser) {
  if (parser == NULL)
    return;
  ASTNode *node = parser->owned_allocations;
  parser->owned_allocations = NULL;
  const size_t limit = POOL_SIZE / MIN_BLOCK;
  for (size_t freed = 0U; node != NULL && freed < limit; ++freed) {
    ASTNode *next = node->allocation_next;
    val_free(&node->litval);
    xfree(node);
    node = next;
  }
}

/* Prepend one owned AST sibling chain without a heap-allocated DFS stack.
 * The original next pointers are preserved until their nodes are consumed.
 * One chain is appended only once in a well-formed AST tree. */
static ASTNode *ast_release_prepend(ASTNode *head, ASTNode *pending,
                                    size_t limit) {
  if (head == NULL)
    return pending;
  ASTNode *last = head;
  for (size_t examined = 0U;
       last->next != NULL && examined < limit; ++examined)
    last = last->next;
  if (last->next != NULL)
    return pending;  /* Malformed cyclic chain: never follow unboundedly. */
  last->next = pending;
  return head;
}

/* Consume the one existing ownership graph in bounded iterative preorder.
 * There are at most POOL_SIZE/MIN_BLOCK live Buddy-backed AST nodes.
 * We borrow the nodes' own next fields as the pending chain; no recursion,
 * malloc, synthetic function registry or fixed-size DFS queue is needed.
 * The right child is skipped ONLY when fn_register already transferred it
 * and set that pointer to NULL. Parse-error bodies must be freed. */
static void ast_release_owned(ASTNode *root) {
  ASTNode *pending = root;
  const size_t limit = POOL_SIZE / MIN_BLOCK;
  for (size_t released = 0U;
       pending != NULL && released < limit; ++released) {
    ASTNode *current = pending;
    pending = current->next;
    pending = ast_release_prepend(current->left, pending, limit);
    pending = ast_release_prepend(current->right, pending, limit);
    pending = ast_release_prepend(current->children, pending, limit);
    pending = ast_release_prepend(current->alt, pending, limit);
    int count = current->n_args;
    if (count < 0 || count > MAX_PARAMS)
      count = MAX_PARAMS;
    for (int i = 0; i < count; ++i)
      pending = ast_release_prepend(current->args[i], pending, limit);
    val_free(&current->litval);
    xfree(current);
  }
}

void ast_free(ASTNode *node) {
  ast_release_owned(node);
}

void ast_free_safe(ASTNode *node) {
  /* fn_register clears the transferred AST_FN_DECL.right. For parse
   * failures right is still owned here and MUST NOT be thrown away. */
  ast_release_owned(node);
}

void ast_print(ASTNode *node, FILE *out, int depth) {
  if (!node || !out)
    return;
  for (int i = 0; i < depth; i++)
    fprintf(out, "  ");
  fprintf(out, "[%d] ", node->kind);
  if (node->name[0])
    fprintf(out, "name='%s' ", node->name);
  if (node->path[0])
    fprintf(out, "path='%s' ", node->path);
  if (node->op[0])
    fprintf(out, "op='%s' ", node->op);
  fprintf(out, "\n");
  if (node->left)
    ast_print(node->left, out, depth + 1);
  if (node->right)
    ast_print(node->right, out, depth + 1);
  if (node->alt)
    ast_print(node->alt, out, depth + 1);
  for (int i = 0; i < node->n_args; i++)
    ast_print(node->args[i], out, depth + 1);
  if (node->children)
    ast_print(node->children, out, depth + 1);
  if (node->next)
    ast_print(node->next, out, depth);
}

/* =========================================================
   PARSER HELPERS
   ========================================================= */

static Token parser_advance(Parser *p) {
  p->cur = lexer_next(p->lex);
  if (p->cur.kind == TK_ERROR) {
    p->had_error = 1;
    strncpy(p->error_msg, p->cur.text, sizeof(p->error_msg) - 1);
  }
  p->peek = (Token){.kind = TK_EOF};
  return p->cur;
}

static void parser_skip_newlines(Parser *p) {
  while (p->cur.kind == TK_NEWLINE || p->cur.kind == TK_EOF) {
    if (p->cur.kind == TK_EOF)
      break;
    parser_advance(p);
  }
}

static int parser_expect(Parser *p, TokenKind k) {
  if (p->cur.kind != k) {
    if (!p->had_error) {
      snprintf(p->error_msg, sizeof(p->error_msg),
               "Line %d: expected '%s', found '%s' ('%s')", p->cur.line,
               token_kind_name(k), token_kind_name(p->cur.kind), p->cur.text);
      p->had_error = 1;
    }
    return 0;
  }
  parser_advance(p);
  return 1;
}

/* Preserve the complete token, never a NAME_LEN-sized preview.
 * Callers own AST allocations and must release them if this fails. */
static int parser_copy_token(Parser *p, Token token, char *out, size_t cap) {
  if (lexer_token_copy(&token, out, cap))
    return 1;
  if (!p->had_error) {
    p->had_error = 1;
    snprintf(p->error_msg, sizeof(p->error_msg),
             "Line %d: token exceeds destination or contains NUL", token.line);
  }
  return 0;
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
    ASTNode *n = parser_ast_alloc(p, AST_INT_LIT, t);
    if (n == NULL)
      return NULL;
    n->litval = val_int(t.ival);
    parser_advance(p);
    return n;
  }
  case TK_FLOAT: {
    ASTNode *n = parser_ast_alloc(p, AST_FLOAT_LIT, t);
    if (n == NULL)
      return NULL;
    n->litval = val_float(t.fval);
    parser_advance(p);
    return n;
  }
  case TK_STRING: {
    ASTNode *n = parser_ast_alloc(p, AST_STR_LIT, t);
    if (n == NULL)
      return NULL;
    char *decoded = nexs_alloc(MAX_STR_LEN);
    if (decoded == NULL) {
      p->had_error = 1;
      snprintf(p->error_msg, sizeof(p->error_msg),
               "Line %d: string decode allocation failed", t.line);
      return NULL;
    }
    if (!parser_copy_token(p, t, decoded, MAX_STR_LEN)) {
      nexs_free(decoded, MAX_STR_LEN);
      return NULL;
    }
    n->litval = val_str(decoded);
    nexs_free(decoded, MAX_STR_LEN);
    if (n->litval.type != TYPE_STR) {
      p->had_error = 1;
      snprintf(p->error_msg, sizeof(p->error_msg),
               "Line %d: string ownership admission failed", t.line);
      return NULL;
    }
    parser_advance(p);
    return n;
  }
  case TK_KW_TRUE: {
    ASTNode *n = parser_ast_alloc(p, AST_BOOL_LIT, t);
    if (n == NULL)
      return NULL;
    n->litval = val_bool(1);
    parser_advance(p);
    return n;
  }
  case TK_KW_FALSE: {
    ASTNode *n = parser_ast_alloc(p, AST_BOOL_LIT, t);
    if (n == NULL)
      return NULL;
    n->litval = val_bool(0);
    parser_advance(p);
    return n;
  }
  case TK_KW_NIL: {
    ASTNode *n = parser_ast_alloc(p, AST_NIL_LIT, t);
    if (n == NULL)
      return NULL;
    parser_advance(p);
    return n;
  }
  case TK_KW_PWD: {
    ASTNode *n = parser_ast_alloc(p, AST_PWD, t);
    if (n == NULL)
      return NULL;
    parser_advance(p);
    return n;
  }
  case TK_KW_LS: {
    Token kw = t;
    parser_advance(p);
    if (p->cur.kind == TK_LPAREN) {
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, kw);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "ls", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return fn;
    }
    /* Simple ls in expression -> default registry list / */
    ASTNode *n = parser_ast_alloc(p, AST_REG_LS, kw);
    if (n == NULL)
      return NULL;
    strncpy(n->path, "/", REG_PATH_MAX - 1);
    return n;
  }
  case TK_IDENT: {
    parser_advance(p);
    if (p->cur.kind == TK_LBRACKET) {
      parser_advance(p);
      ASTNode *idx = parse_expr(p);
      if (!idx)
        return NULL;
      if (!parser_expect(p, TK_RBRACKET))
        return NULL;
      ASTNode *n = parser_ast_alloc(p, AST_INDEX, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->left = idx;
      return n;
    }
    if (p->cur.kind == TK_LPAREN) {
      parser_advance(p);
      ASTNode *n = parser_ast_alloc(p, AST_FN_CALL, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF &&
             !p->had_error) {
        ASTNode *arg = parse_expr(p);
        if (!arg)
          break;
        if (n->n_args < MAX_PARAMS)
          n->args[n->n_args++] = arg;
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return n;
    }
    ASTNode *n = parser_ast_alloc(p, AST_IDENT, t);
    if (n == NULL)
      return NULL;
    strncpy(n->name, t.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    return n;
  }
  case TK_KW_REG: {
    parser_advance(p);
    if (p->cur.kind != TK_REGPATH)
      return NULL;
    Token pt = p->cur;
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_REG_ACCESS, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }
  case TK_REGPATH: {
    ASTNode *n = parser_ast_alloc(p, AST_REG_ACCESS, t);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, t, n->path, sizeof(n->path))) {
      return NULL;
    }
    parser_advance(p);
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
    ASTNode *n = parser_ast_alloc(p, AST_UNOP, t);
    if (n == NULL)
      return NULL;
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
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, kw);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "msgpending", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return fn;
    }
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_MSG_PENDING, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }
  case TK_KW_RECV: {
    Token kw = t;
    parser_advance(p);
    if (p->cur.kind == TK_LPAREN) {
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, kw);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "receivemessage", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN))
        return NULL;
      return fn;
    }
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_RECV_MSG, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }
  case TK_KW_DEREF: {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_PTR_DEREF, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
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
  case TK_OR:
  case TK_KW_OR:
    return 1;
  case TK_AND:
  case TK_KW_AND:
    return 2;
  case TK_EQEQ:
  case TK_NEQ:
    return 3;
  case TK_LT:
  case TK_GT:
  case TK_LE:
  case TK_GE:
    return 4;
  case TK_PLUS:
  case TK_MINUS:
    return 5;
  case TK_STAR:
  case TK_SLASH:
  case TK_PERCENT:
    return 6;
  default:
    return 0;
  }
}

static const char *binop_name(TokenKind k) {
  switch (k) {
  case TK_PLUS:
    return "+";
  case TK_MINUS:
    return "-";
  case TK_STAR:
    return "*";
  case TK_SLASH:
    return "/";
  case TK_PERCENT:
    return "%";
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
  case TK_KW_AND:
    return "&&";
  case TK_OR:
  case TK_KW_OR:
    return "||";
  default:
    return "?";
  }
}

static ASTNode *parse_binop(Parser *p, int min_prec) {
  ASTNode *left = parse_primary(p);
  if (!left)
    return NULL;
  while (1) {
    int prec = binop_prec(p->cur.kind);
    if (p->cur.kind == TK_REGPATH && strcmp(p->cur.text, "/") == 0)
      prec = 6;
    if (prec < min_prec)
      break;
    Token op = p->cur;
    if (op.kind == TK_REGPATH && strcmp(op.text, "/") == 0)
      op.kind = TK_SLASH;
    parser_advance(p);
    ASTNode *right = parse_binop(p, prec + 1);
    if (!right)
      return left;
    ASTNode *n = parser_ast_alloc(p, AST_BINOP, op);
    if (n == NULL)
      return NULL;
    strncpy(n->op, binop_name(op.kind), 3);
    n->op[3] = '\0';
    n->left = left;
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
  if (!parser_expect(p, TK_LBRACE))
    return NULL;
  parser_skip_newlines(p);
  ASTNode *block = parser_ast_alloc(p, AST_BLOCK, t);
  if (block == NULL)
    return NULL;
  ASTNode *last = NULL;
  while (p->cur.kind != TK_RBRACE && p->cur.kind != TK_EOF && !p->had_error) {
    ASTNode *s = parse_stmt(p);
    if (s) {
      if (!block->children)
        block->children = s;
      else
        last->next = s;
      last = s;
    }
    parser_skip_newlines(p);
  }
  if (!parser_expect(p, TK_RBRACE))
    return block;
  return block;
}

ASTNode *parse_stmt(Parser *p) {
  parser_skip_newlines(p);
  Token t = p->cur;

  /* library "name" */
  if (t.kind == TK_KW_LIBRARY || t.kind == TK_KW_MODULE) {
    parser_advance(p);
    Token lib_tok = p->cur;
    if (!parser_expect(p, TK_STRING))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_LIBRARY, t);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, lib_tok, n->name, sizeof(n->name))) {
      return NULL;
    }
    return n;
  }

  /* import "path" */
  if (t.kind == TK_KW_IMPORT) {
    parser_advance(p);
    Token path_tok = p->cur;
    if (!parser_expect(p, TK_STRING))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_IMPORT, t);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, path_tok, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }

  /* fn name(params) { body } */
  if (t.kind == TK_KW_FN) {
    parser_advance(p);
    Token name_tok = p->cur;
    if (name_tok.kind != TK_IDENT && name_tok.kind != TK_KW_LS) {
      if (!p->had_error) {
        snprintf(p->error_msg, sizeof(p->error_msg),
                 "Line %d: expected identifier after 'fn', found '%s'",
                 name_tok.line, name_tok.text);
        p->had_error = 1;
      }
      return NULL;
    }
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_FN_DECL, name_tok);
    if (n == NULL)
      return NULL;
    strncpy(n->name, name_tok.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    if (!parser_expect(p, TK_LPAREN)) {
      return NULL;
    }
    while (p->cur.kind == TK_IDENT && n->n_params < MAX_PARAMS) {
      strncpy(n->params[n->n_params], p->cur.text, NAME_LEN - 1);
      n->params[n->n_params][NAME_LEN - 1] = '\0';
      n->n_params++;
      parser_advance(p);
      if (p->cur.kind == TK_COMMA)
        parser_advance(p);
    }
    if (!parser_expect(p, TK_RPAREN)) {
      return NULL;
    }
    n->right = parse_block(p);
    return n;
  }

  /* if cond { then } [else { els }] */
  if (t.kind == TK_KW_IF) {
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_IF, t);
    if (n == NULL)
      return NULL;
    n->left = parse_expr(p);
    n->right = parse_block(p);
    parser_skip_newlines(p);
    if (p->cur.kind == TK_KW_ELSE) {
      parser_advance(p);
      if (p->cur.kind == TK_KW_IF) {
        n->alt = parse_stmt(p);
      } else {
        n->alt = parse_block(p);
      }
    }
    return n;
  }

  /* loop { body } */
  if (t.kind == TK_KW_LOOP) {
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_LOOP, t);
    if (n == NULL)
      return NULL;
    n->right = parse_block(p);
    return n;
  }

  /* break / cont / ret */
  if (t.kind == TK_KW_BREAK) {
    parser_advance(p);
    return parser_ast_alloc(p, AST_BREAK, t);
  }
  if (t.kind == TK_KW_CONT) {
    parser_advance(p);
    return parser_ast_alloc(p, AST_CONT, t);
  }
  if (t.kind == TK_KW_RET) {
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_RET, t);
    if (n == NULL)
      return NULL;
    if (p->cur.kind != TK_NEWLINE && p->cur.kind != TK_RBRACE &&
        p->cur.kind != TK_EOF)
      n->left = parse_expr(p);
    return n;
  }

  /* out expr */
  if (t.kind == TK_KW_OUT) {
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_OUT, t);
    if (n == NULL)
      return NULL;
    n->left = parse_expr(p);
    return n;
  }

  /* del name[idx] */
  if (t.kind == TK_KW_DEL) {
    parser_advance(p);
    Token nt = p->cur;
    if (!parser_expect(p, TK_IDENT))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_DEL, nt);
    if (n == NULL)
      return NULL;
    strncpy(n->name, nt.text, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    if (!parser_expect(p, TK_LBRACKET)) {
      return NULL;
    }
    n->left = parse_expr(p);
    if (!parser_expect(p, TK_RBRACKET)) {
      return NULL;
    }
    return n;
  }

  /* ls /path OR ls(path) */
  if (t.kind == TK_KW_LS) {
    parser_advance(p);
    if (p->cur.kind == TK_LPAREN) {
      /* Parse as function call */
      ASTNode *n = parser_ast_alloc(p, AST_FN_CALL, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, "ls", NAME_LEN - 1);
      parser_advance(p);
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF &&
             !p->had_error) {
        ASTNode *arg = parse_expr(p);
        if (!arg)
          break;
        if (n->n_args < MAX_PARAMS)
          n->args[n->n_args++] = arg;
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return n;
    }
    /* Fallback to built-in registry listing */
    ASTNode *n = parser_ast_alloc(p, AST_REG_LS, t);
    if (n == NULL)
      return NULL;
    if (p->cur.kind == TK_REGPATH || p->cur.kind == TK_STRING || p->cur.kind == TK_IDENT ||
        (p->cur.kind >= TK_KW_FN && p->cur.kind <= TK_KW_PENDING)) {
      if (!parser_copy_token(p, p->cur, n->path, sizeof(n->path))) {
        return NULL;
      }
      parser_advance(p);
    } else {
      n->path[0] = '\0';
    }
    return n;
  }
  
  /* cd /path OR cd("path") OR cd path */
  if (t.kind == TK_KW_CD) {
    parser_advance(p);
    ASTNode *n = parser_ast_alloc(p, AST_CD, t);
    if (n == NULL)
      return NULL;
    if (p->cur.kind == TK_REGPATH || p->cur.kind == TK_STRING || p->cur.kind == TK_IDENT || 
        (p->cur.kind >= TK_KW_FN && p->cur.kind <= TK_KW_PENDING)) {
      if (!parser_copy_token(p, p->cur, n->path, sizeof(n->path))) {
        return NULL;
      }
      parser_advance(p);
    } else {
      n->left = parse_expr(p);
    }
    return n;
  }

  /* pwd */
  if (t.kind == TK_KW_PWD) {
    parser_advance(p);
    return parser_ast_alloc(p, AST_PWD, t);
  }

  /* reg /path [= expr] */
  if (t.kind == TK_KW_REG) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    if (p->cur.kind == TK_EQ) {
      parser_advance(p);
      ASTNode *n = parser_ast_alloc(p, AST_REG_SET, pt);
      if (n == NULL)
        return NULL;
      if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
        return NULL;
      }
      n->left = parse_expr(p);
      return n;
    }
    ASTNode *n = parser_ast_alloc(p, AST_REG_ACCESS, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }

  /* sendmessage /path expr           — keyword form with literal path
   * sendmessage(path_expr, val_expr) — function-call form with any expression
   */
  if (t.kind == TK_KW_SEND) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      /* Function-call form: delegate to fn_call parsing */
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, t);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "sendmessage", NAME_LEN - 1);
      parser_advance(p); /* consume '(' */
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_SEND_MSG, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    n->left = parse_expr(p);
    return n;
  }

  /* receivemessage /path  or  receivemessage(path_expr) */
  if (t.kind == TK_KW_RECV) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, t);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "receivemessage", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN))
        return NULL;
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_RECV_MSG, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }

  /* msgpending /path  or  msgpending(path_expr) */
  if (t.kind == TK_KW_PENDING) {
    parser_advance(p);
    Token peek = p->cur;
    if (peek.kind == TK_LPAREN) {
      ASTNode *fn = parser_ast_alloc(p, AST_FN_CALL, t);
      if (fn == NULL)
        return NULL;
      strncpy(fn->name, "msgpending", NAME_LEN - 1);
      parser_advance(p);
      fn->n_args = 0;
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF) {
        if (fn->n_args < MAX_PARAMS)
          fn->args[fn->n_args++] = parse_expr(p);
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN))
        return NULL;
      return fn;
    }
    Token pt = peek;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_MSG_PENDING, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }

  /* ptr /path = /target */
  if (t.kind == TK_KW_PTR) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    if (!parser_expect(p, TK_EQ))
      return NULL;
    Token target_tok = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_PTR_SET, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    /* Pointer targets belong to the Value slot, not an ABI name buffer. */
    char target[REG_PATH_MAX];
    if (!parser_copy_token(p, target_tok, target, sizeof(target))) {
      return NULL;
    }
    n->litval = val_str(target);
    if (n->litval.type != TYPE_STR) {
      p->had_error = 1;
      snprintf(p->error_msg, sizeof(p->error_msg),
               "Line %d: pointer literal admission failed", t.line);
      return NULL;
    }
    return n;
  }

  /* deref /path */
  if (t.kind == TK_KW_DEREF) {
    parser_advance(p);
    Token pt = p->cur;
    if (!parser_expect(p, TK_REGPATH))
      return NULL;
    ASTNode *n = parser_ast_alloc(p, AST_PTR_DEREF, pt);
    if (n == NULL)
      return NULL;
    if (!parser_copy_token(p, pt, n->path, sizeof(n->path))) {
      return NULL;
    }
    return n;
  }

  /* name = expr  |  name[idx] = expr  |  expr */
  if (t.kind == TK_IDENT) {
    parser_advance(p);
    if (p->cur.kind == TK_LBRACKET) {
      parser_advance(p);
      ASTNode *idx = parse_expr(p);
      if (!parser_expect(p, TK_RBRACKET)) {
        return NULL;
      }
      if (p->cur.kind == TK_EQ) {
        parser_advance(p);
        ASTNode *n = parser_ast_alloc(p, AST_INDEX_ASSIGN, t);
        if (n == NULL)
          return NULL;
        strncpy(n->name, t.text, NAME_LEN - 1);
        n->name[NAME_LEN - 1] = '\0';
        n->left = idx;
        n->right = parse_expr(p);
        return n;
      }
      ASTNode *n = parser_ast_alloc(p, AST_INDEX, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->left = idx;
      return n;
    }
    if (p->cur.kind == TK_EQ) {
      parser_advance(p);
      ASTNode *n = parser_ast_alloc(p, AST_ASSIGN, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      n->right = parse_expr(p);
      return n;
    }
    if (p->cur.kind == TK_LPAREN) {
      parser_advance(p);
      ASTNode *n = parser_ast_alloc(p, AST_FN_CALL, t);
      if (n == NULL)
        return NULL;
      strncpy(n->name, t.text, NAME_LEN - 1);
      n->name[NAME_LEN - 1] = '\0';
      while (p->cur.kind != TK_RPAREN && p->cur.kind != TK_EOF &&
             !p->had_error) {
        ASTNode *arg = parse_expr(p);
        if (!arg)
          break;
        if (n->n_args < MAX_PARAMS)
          n->args[n->n_args++] = arg;
        if (p->cur.kind == TK_COMMA)
          parser_advance(p);
      }
      if (!parser_expect(p, TK_RPAREN)) {
        return NULL;
      }
      return n;
    }
    ASTNode *n = parser_ast_alloc(p, AST_IDENT, t);
    if (n == NULL)
      return NULL;
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
  if (!p || !lex)
    return;
  p->lex = lex;
  p->had_error = 0;
  p->error_msg[0] = '\0';
  p->owned_allocations = NULL;
  p->peek.kind = TK_EOF;
  parser_advance(p);
}

ASTNode *parse_program(Parser *p) {
  if (p == NULL)
    return NULL;
  ASTNode *prog = parser_ast_alloc(p, AST_PROGRAM, p->cur);
  if (prog == NULL) {
    parser_discard_allocations(p);
    return NULL;
  }
  ASTNode *last = NULL;
  parser_skip_newlines(p);
  while (p->cur.kind != TK_EOF && !p->had_error) {
    ASTNode *s = parse_stmt(p);
    if (s) {
      if (!prog->children)
        prog->children = s;
      else
        last->next = s;
      last = s;

      if (!p->had_error && p->cur.kind != TK_NEWLINE && p->cur.kind != TK_EOF &&
          p->cur.kind != TK_RBRACE) {
        snprintf(p->error_msg, sizeof(p->error_msg),
                 "Line %d: expected end of line after statement, found '%s'",
                 p->cur.line, p->cur.text);
        p->had_error = 1;
        break;
      }
    }
    parser_skip_newlines(p);
  }
  if (p->had_error) {
    parser_discard_allocations(p);
    return NULL;
  }
  /* Ownership has moved to the linked AST; success is never a leak. */
  p->owned_allocations = NULL;
  return prog;
}
