/* Actual parser AST allocation and release under the one Buddy provider.
 * Tests wide/deep owned trees, parse-error function bodies and transfers.
 * No mocks, production asserts, extra AST index or resource magic.
 */
#include "nexs_alloc.h"
#include "nexs_ast.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void verify(int okay, const char *description) {
  ++checks;
  if (!okay) {
    ++failures;
    fprintf(stderr, "AST_RELEASE_FAIL %s\n", description);
  }
}

static void verify_full_reclaim(const char *name) {
  void *whole = buddy_alloc(POOL_SIZE);
  verify(whole != NULL, name);
  if (whole != NULL)
    buddy_free(whole);
}

static void test_deep_without_c_stack(void) {
  Token token = {0};
  ASTNode *head = ast_alloc(AST_PROGRAM, token);
  size_t depth = 0U;
  while (head != NULL && depth < 2048U) {
    ASTNode *node = ast_alloc(AST_UNOP, token);
    if (node == NULL)
      break;
    node->left = head;
    head = node;
    ++depth;
  }
  verify(depth == 2048U, "actual Buddy admits 2048-nested AST ownership");
  ast_free(head);
  verify_full_reclaim("iterative deep AST release returns full 16MiB arena");
}

static void test_wide_siblings(void) {
  Token token = {0};
  ASTNode *head = ast_alloc(AST_PROGRAM, token);
  ASTNode *tail = NULL;
  size_t width = 0U;
  for (size_t i = 0U; i < 1800U && head != NULL; ++i) {
    ASTNode *node = ast_alloc(AST_NIL_LIT, token);
    if (node == NULL)
      break;
    if (tail == NULL)
      head->children = node;
    else
      tail->next = node;
    tail = node;
    ++width;
  }
  verify(width == 1800U, "actual Buddy admits 1800 sibling AST nodes");
  ast_free(head);
  verify_full_reclaim("iterative wide AST release returns full 16MiB arena");
}

static void test_parse_failure_owned_body(void) {
  Token token = {0};
  ASTNode *root = ast_alloc(AST_PROGRAM, token);
  ASTNode *decl = ast_alloc(AST_FN_DECL, token);
  ASTNode *body = ast_alloc(AST_BLOCK, token);
  verify(root != NULL && decl != NULL && body != NULL,
         "function and original untransferred body admitted");
  if (root != NULL && decl != NULL && body != NULL) {
    root->children = decl;
    decl->right = body;
    body->left = ast_alloc(AST_INT_LIT, token);
    verify(body->left != NULL, "untransferred body nested child admitted");
    ast_free_safe(root);
    verify_full_reclaim("failed parse retains body for exact AST cleanup");
  } else {
    ast_free(root);
    ast_free(decl);
    ast_free(body);
  }
}

static void test_transferred_body_survives(void) {
  Token token = {0};
  ASTNode *root = ast_alloc(AST_FN_DECL, token);
  ASTNode *body = ast_alloc(AST_BLOCK, token);
  verify(root != NULL && body != NULL,
         "body and registry-bound AST declaration admitted");
  if (root != NULL && body != NULL) {
    root->right = body;
    root->right = NULL; /* Exact fn_register successful-transfer contract. */
    ast_free_safe(root);
    verify(body->kind == AST_BLOCK,
           "registry-transferred body survives parser-owner cleanup");
    ast_free(body);
    verify_full_reclaim("transferred body later freed by its actual owner");
  } else {
    ast_free(root);
    ast_free(body);
  }
}

static void test_all_child_edge_kinds(void) {
  Token token = {0};
  ASTNode *root = ast_alloc(AST_FN_CALL, token);
  verify(root != NULL, "all-edge owner admitted");
  if (root == NULL)
    return;
  root->left = ast_alloc(AST_INT_LIT, token);
  root->right = ast_alloc(AST_INT_LIT, token);
  root->children = ast_alloc(AST_INT_LIT, token);
  root->alt = ast_alloc(AST_INT_LIT, token);
  root->n_args = 2;
  root->args[0] = ast_alloc(AST_INT_LIT, token);
  root->args[1] = ast_alloc(AST_INT_LIT, token);
  verify(root->left != NULL && root->right != NULL &&
         root->children != NULL && root->alt != NULL &&
         root->args[0] != NULL && root->args[1] != NULL,
         "all AST child edge kinds admitted");
  ast_free_safe(root);
  verify_full_reclaim("all child-edge kinds release without duplicate frees");
}

static void test_parse_partial_oom_transaction(void) {
  /* Reserve almost all real Buddy blocks, leaving enough capacity to
   * build a partial AST before an actual allocator exhaustion. */
  void *held[8192] = {0};
  size_t count = 0U;
  while (count < sizeof(held) / sizeof(held[0])) {
    void *next = buddy_alloc(sizeof(ASTNode));
    if (next == NULL)
      break;
    held[count] = next;
    ++count;
  }
  verify(count > 20U && buddy_alloc(sizeof(ASTNode)) == NULL,
         "real Buddy AST admission exhausts a finite provider");
  for (size_t i = 0U; i < 12U && i < count; ++i) {
    buddy_free(held[count - 1U - i]);
    held[count - 1U - i] = NULL;
  }

  char script[8192] = {0};
  size_t used = 0U;
  for (size_t i = 0U; i < 1000U && used + 6U < sizeof(script); ++i) {
    memcpy(script + used, "out 1\n", 6U);
    used += 6U;
  }
  script[used] = '\0';
  Lexer lexer;
  Parser parser;
  lexer_init(&lexer, script);
  parser_init(&parser, &lexer);
  ASTNode *program = parse_program(&parser);
  verify(program == NULL && parser.had_error,
         "valid source fails closed when AST provider actually runs out");
  verify(strstr(parser.error_msg, "allocation failed") != NULL,
         "real OOM is surfaced as a parser diagnostic");
  verify(parser.owned_allocations == NULL,
         "failed parse retires every allocation-ledger entry");
  for (size_t i = 0U; i < count; ++i)
    if (held[i] != NULL)
      buddy_free(held[i]);
  verify_full_reclaim(
      "failed parse rolled back all disconnected and linked AST nodes");
}

static void test_actual_malformed_function_rollback(void) {
  const char script[] =
      "fn tested(a) {\n"
      "out a\n"
      "}\n"
      "fn incomplete(\n";
  Lexer lexer;
  Parser parser;
  lexer_init(&lexer, script);
  parser_init(&parser, &lexer);
  ASTNode *program = parse_program(&parser);
  verify(program == NULL && parser.had_error,
         "real parser rejects unfinished second function definition");
  verify(parser.owned_allocations == NULL,
         "syntax error releases original and untransferred function bodies");
  verify_full_reclaim(
      "malformed function declaration leaves no stranded AST memory");
}

int main(void) {
  verify(POOL_SIZE == 16U * 1024U * 1024U,
         "16MiB actual Buddy resource profile selected");
  test_deep_without_c_stack();
  test_wide_siblings();
  test_parse_failure_owned_body();
  test_transferred_body_survives();
  test_all_child_edge_kinds();
  test_parse_partial_oom_transaction();
  test_actual_malformed_function_rollback();
  printf("AST_ITERATIVE_OWNER %s checks=%u failures=%u\n",
         failures == 0U ? "PASS" : "FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
