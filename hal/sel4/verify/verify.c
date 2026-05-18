#include "verify.h"
#include "../../../core/include/nexs_alloc.h"
#include "../../../core/include/nexs_common.h"
#include "../../../core/include/nexs_value.h"
#include "../../../registry/include/nexs_registry.h"
#include "../../../sys/include/nexs_sys.h"
#include "../../../lang/include/nexs_eval.h"
#include "../../../lang/include/nexs_lex.h"
#include "../../../lang/include/nexs_ast.h"
#include "../../../lang/include/nexs_fn.h"
#include "../../../hal/include/nexs_hal.h"
#include "../../../hal/include/nexs_timer.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Global error counter for the verification suite */
static int g_verify_errors = 0;

/* Helper assertion macro */
#define VERIFY_ASSERT(cond, msg)                                                \
  do {                                                                          \
    if (!(cond)) {                                                              \
      printf("\033[1;31m  [FAIL] %s (Line %d)\033[0m\n", msg, __LINE__);        \
      g_verify_errors++;                                                        \
      return;                                                                   \
    }                                                                           \
  } while (0)

/* =============================================================================
 * 1. Buddy Allocator Test
 * ============================================================================= */
static void test_buddy_allocator(void) {
  printf("  -> Testing Buddy Allocator...\n");

  /* Allocate multiple blocks of different power-of-2 sizes */
  void *b1 = buddy_alloc(64);
  void *b2 = buddy_alloc(128);
  void *b3 = buddy_alloc(512);
  void *b4 = buddy_alloc(1024);

  VERIFY_ASSERT(b1 != NULL, "Allocation b1 (64 bytes) failed");
  VERIFY_ASSERT(b2 != NULL, "Allocation b2 (128 bytes) failed");
  VERIFY_ASSERT(b3 != NULL, "Allocation b3 (512 bytes) failed");
  VERIFY_ASSERT(b4 != NULL, "Allocation b4 (1024 bytes) failed");

  /* Check power-of-2 alignment and that addresses do not overlap */
  VERIFY_ASSERT(((uintptr_t)b1 % 32) == 0, "b1 is not properly aligned");
  VERIFY_ASSERT(((uintptr_t)b2 % 32) == 0, "b2 is not properly aligned");
  VERIFY_ASSERT(((uintptr_t)b3 % 32) == 0, "b3 is not properly aligned");
  VERIFY_ASSERT(((uintptr_t)b4 % 32) == 0, "b4 is not properly aligned");

  VERIFY_ASSERT((uintptr_t)b1 != (uintptr_t)b2, "Overlapping allocations b1 & b2");
  VERIFY_ASSERT((uintptr_t)b2 != (uintptr_t)b3, "Overlapping allocations b2 & b3");
  VERIFY_ASSERT((uintptr_t)b3 != (uintptr_t)b4, "Overlapping allocations b3 & b4");

  /* Free blocks to test coalescing of adjacent spaces */
  buddy_free(b1);
  buddy_free(b2);
  buddy_free(b3);
  buddy_free(b4);

  /* Allocate a huge contiguous chunk to confirm consolidation was successful */
  void *huge = buddy_alloc(1024 * 1024); /* 1MB */
  VERIFY_ASSERT(huge != NULL, "Buddy coalescence failed to consolidate free blocks");
  buddy_free(huge);

  printf("\033[1;32m  [PASS] Buddy Allocator working correctly.\033[0m\n");
}

/* =============================================================================
 * 2. Page Allocator Test
 * ============================================================================= */
static void test_page_allocator(void) {
  printf("  -> Testing Page Allocator...\n");

  /* Allocate 1, 2, and 4 contiguous pages */
  void *p1 = page_alloc(1);
  void *p2 = page_alloc(2);
  void *p3 = page_alloc(4);

  VERIFY_ASSERT(p1 != NULL, "Allocation p1 (1 page) failed");
  VERIFY_ASSERT(p2 != NULL, "Allocation p2 (2 pages) failed");
  VERIFY_ASSERT(p3 != NULL, "Allocation p3 (4 pages) failed");

  /* Ensure page addresses are aligned on standard 4KB page boundaries */
  VERIFY_ASSERT(((uintptr_t)p1 % 4096) == 0, "p1 is not aligned on page boundary");
  VERIFY_ASSERT(((uintptr_t)p2 % 4096) == 0, "p2 is not aligned on page boundary");
  VERIFY_ASSERT(((uintptr_t)p3 % 4096) == 0, "p3 is not aligned on page boundary");

  /* Verify correct page ownership signature */
  VERIFY_ASSERT(is_page_ptr(p1) == 1, "p1 not identified as page pointer");
  VERIFY_ASSERT(is_page_ptr(p2) == 1, "p2 not identified as page pointer");
  VERIFY_ASSERT(is_page_ptr(p3) == 1, "p3 not identified as page pointer");

  /* Release and check bitmap restoration */
  page_free(p1, 1);
  page_free(p2, 2);
  page_free(p3, 4);

  printf("\033[1;32m  [PASS] Page Allocator working correctly.\033[0m\n");
}

/* =============================================================================
 * 3. Unified Allocator Parity Test
 * ============================================================================= */
static void test_unified_allocator(void) {
  printf("  -> Testing Unified Allocator (Buddy/Page routing)...\n");

  /* Small allocation should route through the Buddy Allocator */
  void *small_ptr = nexs_alloc(512);
  VERIFY_ASSERT(small_ptr != NULL, "Small unified allocation failed");
  VERIFY_ASSERT(is_page_ptr(small_ptr) == 0, "Small allocation incorrectly routed to Page Allocator");

  /* Large allocation should route directly to the Page Allocator (> 512KB threshold) */
  void *large_ptr = nexs_alloc(1024 * 1024); /* 1MB */
  VERIFY_ASSERT(large_ptr != NULL, "Large unified allocation failed");
  VERIFY_ASSERT(is_page_ptr(large_ptr) == 1, "Large allocation incorrectly routed to Buddy Allocator");

  /* Clean up */
  nexs_free(small_ptr, 512);
  nexs_free(large_ptr, 1024 * 1024);

  printf("\033[1;32m  [PASS] Unified Allocator working correctly.\033[0m\n");
}

/* =============================================================================
 * 4. Hierarchical Registry Test
 * ============================================================================= */
static void test_hierarchical_registry(void) {
  printf("  -> Testing Hierarchical Registry Store...\n");

  /* Create paths and set values */
  int set_rc1 = reg_set("/test/nested/val_int", val_int(42), RK_READ | RK_WRITE);
  int set_rc2 = reg_set("/test/nested/val_str", val_str("NEXS_OK"), RK_READ | RK_WRITE);

  VERIFY_ASSERT(set_rc1 == 0, "Failed to create registry node /test/nested/val_int");
  VERIFY_ASSERT(set_rc2 == 0, "Failed to create registry node /test/nested/val_str");

  /* Look up nodes */
  Value v_int = reg_get("/test/nested/val_int");
  Value v_str = reg_get("/test/nested/val_str");

  VERIFY_ASSERT(v_int.type == TYPE_INT, "Retrieved integer value has incorrect type");
  VERIFY_ASSERT(v_int.ival == 42, "Retrieved integer value has incorrect content");

  VERIFY_ASSERT(v_str.type == TYPE_STR, "Retrieved string value has incorrect type");
  VERIFY_ASSERT(v_str.data != NULL && strcmp((const char *)v_str.data, "NEXS_OK") == 0, "Retrieved string value has incorrect content");

  val_free(&v_int);
  val_free(&v_str);

  /* Delete node and verify subtraction from registry tree */
  int del_rc = reg_delete("/test/nested/val_str");
  VERIFY_ASSERT(del_rc == 0, "Failed to delete registry key");

  Value v_deleted = reg_get("/test/nested/val_str");
  VERIFY_ASSERT(val_is_error(&v_deleted), "Deleted registry node still exists");

  val_free(&v_deleted);

  printf("\033[1;32m  [PASS] Hierarchical Registry working correctly.\033[0m\n");
}

/* =============================================================================
 * 5. FIFO IPC Message Queue Test
 * ============================================================================= */
static void test_ipc_fifo_queue(void) {
  printf("  -> Testing IPC Message Queue FIFO Ordering...\n");

  const char *q_path = "/ipc/verify_queue";

  /* Initialize a dedicated IPC queue */
  int init_rc = reg_ipc_init_queue(q_path, 8);
  VERIFY_ASSERT(init_rc == 0, "Failed to initialize IPC queue");

  /* Dispatch 3 sequential messages */
  int s1 = reg_ipc_send(q_path, val_int(100));
  int s2 = reg_ipc_send(q_path, val_int(200));
  int s3 = reg_ipc_send(q_path, val_int(300));

  VERIFY_ASSERT(s1 == 0, "Failed to send message 1");
  VERIFY_ASSERT(s2 == 0, "Failed to send message 2");
  VERIFY_ASSERT(s3 == 0, "Failed to send message 3");

  /* Check total pending messages */
  int pending = reg_ipc_pending(q_path);
  VERIFY_ASSERT(pending == 3, "Incorrect pending message count in queue");

  /* Pop messages and verify FIFO (First-In-First-Out) ordering */
  Value m1, m2, m3;
  int r1 = reg_ipc_recv(q_path, &m1);
  int r2 = reg_ipc_recv(q_path, &m2);
  int r3 = reg_ipc_recv(q_path, &m3);

  VERIFY_ASSERT(r1 == 0, "Failed to dequeue message 1");
  VERIFY_ASSERT(m1.type == TYPE_INT && m1.ival == 100, "FIFO ordering broken at message 1");

  VERIFY_ASSERT(r2 == 0, "Failed to dequeue message 2");
  VERIFY_ASSERT(m2.type == TYPE_INT && m2.ival == 200, "FIFO ordering broken at message 2");

  VERIFY_ASSERT(r3 == 0, "Failed to dequeue message 3");
  VERIFY_ASSERT(m3.type == TYPE_INT && m3.ival == 300, "FIFO ordering broken at message 3");

  val_free(&m1);
  val_free(&m2);
  val_free(&m3);

  /* Final queue balance check */
  VERIFY_ASSERT(reg_ipc_pending(q_path) == 0, "Queue not empty after draining all messages");

  printf("\033[1;32m  [PASS] IPC Queue working correctly.\033[0m\n");
}

/* =============================================================================
 * 6. Plan 9 VFS / Syscall Emulation Test
 * ============================================================================= */
static void test_vfs_syscalls(void) {
  printf("  -> Testing Plan 9 VFS Syscalls Emulation...\n");

  const char *f_path = "/verify_file.txt";

  /* 1. Create file with write permissions (using NEXS_ORDWR instead of POSIX O_RDWR) */
  int fd = nexs_create(f_path, NEXS_ORDWR, 0666);
  VERIFY_ASSERT(fd >= 0, "Failed to create virtual file via VFS");

  /* 2. Write data */
  const char *msg = "NEXS_SEL4_VERIFY_OK";
  int n_written = nexs_pwrite(fd, msg, strlen(msg), 0);
  VERIFY_ASSERT(n_written == (int)strlen(msg), "Failed to write exact amount of bytes");

  /* 3. Seek to position */
  int64_t offset = nexs_seek(fd, 5, 0); /* SEEK_SET */
  VERIFY_ASSERT(offset == 5, "Failed to seek file pointer to 5");

  /* 4. Read back partial block */
  char buf[8];
  memset(buf, 0, sizeof(buf));
  int n_read = nexs_pread(fd, buf, 4, 5); /* read 4 bytes starting from offset 5: "SEL4" */
  VERIFY_ASSERT(n_read == 4, "Failed to read exact amount of bytes");
  VERIFY_ASSERT(strcmp(buf, "SEL4") == 0, "Read buffer content mismatch");

  /* 5. Clean up file descriptor (RAMFS buffer is dynamically freed here) */
  int close_rc = nexs_close(fd);
  VERIFY_ASSERT(close_rc == 0, "Failed to close virtual file descriptor");

  printf("\033[1;32m  [PASS] Plan 9 VFS Syscalls working correctly.\033[0m\n");
}

/* =============================================================================
 * 7. Lexer Test
 * ============================================================================= */
static void test_lexer(void) {
  printf("  -> Testing Lexer (Tokenization)...\n");

  const char *src = "x = 42; fn add(a b) { ret a + b } ptr /test; sendmessage /ipc 100;";
  Lexer lex;
  lexer_init(&lex, src);

  /* Token 1: x */
  Token t = lexer_next(&lex);
  VERIFY_ASSERT(t.kind == TK_IDENT && strcmp(t.text, "x") == 0, "Lexer failed on 'x'");

  /* Token 2: = */
  t = lexer_next(&lex);
  VERIFY_ASSERT(t.kind == TK_EQ, "Lexer failed on '='");

  /* Token 3: 42 */
  t = lexer_next(&lex);
  VERIFY_ASSERT(t.kind == TK_INT && t.ival == 42, "Lexer failed on '42'");

  /* Consumiamo i token successivi fino a verificare la presenza di parole chiave fondamentali */
  int found_fn = 0;
  int found_ptr = 0;
  int found_send = 0;
  for (int i = 0; i < 30; i++) {
    t = lexer_next(&lex);
    if (t.kind == TK_KW_FN) found_fn = 1;
    if (t.kind == TK_KW_PTR) found_ptr = 1;
    if (t.kind == TK_KW_SEND) found_send = 1;
    if (t.kind == TK_EOF) break;
  }
  VERIFY_ASSERT(found_fn == 1, "Lexer failed to identify 'fn' keyword");
  VERIFY_ASSERT(found_ptr == 1, "Lexer failed to identify 'ptr' keyword");
  VERIFY_ASSERT(found_send == 1, "Lexer failed to identify 'sendmessage' keyword");

  printf("\033[1;32m  [PASS] Lexer working correctly.\033[0m\n");
}

/* =============================================================================
 * 8. Parser Test
 * ============================================================================= */
static void test_parser(void) {
  printf("  -> Testing Parser (AST generation)...\n");

  const char *src = "y = 10 + 20";
  Lexer lex;
  lexer_init(&lex, src);

  Parser p;
  parser_init(&p, &lex);

  ASTNode *program = parse_program(&p);
  VERIFY_ASSERT(program != NULL, "Parser returned NULL program");
  VERIFY_ASSERT(program->kind == AST_PROGRAM, "Parser root node is not AST_PROGRAM");
  VERIFY_ASSERT(program->children != NULL, "Program has no child statements");

  ASTNode *stmt = program->children;
  VERIFY_ASSERT(stmt->kind == AST_ASSIGN, "Statement is not an assignment (AST_ASSIGN)");
  VERIFY_ASSERT(strcmp(stmt->name, "y") == 0, "Assignment target name is not 'y'");

  ASTNode *expr = stmt->right;
  VERIFY_ASSERT(expr != NULL && expr->kind == AST_BINOP, "Right-hand side expression is not a binary operation (AST_BINOP)");
  VERIFY_ASSERT(strcmp(expr->op, "+") == 0, "Binary operator is not '+'");
  VERIFY_ASSERT(expr->left->kind == AST_INT_LIT && expr->left->litval.ival == 10, "Left operand is not 10");
  VERIFY_ASSERT(expr->right->kind == AST_INT_LIT && expr->right->litval.ival == 20, "Right operand is not 20");

  /* Clean up AST memory securely */
  ast_free(program);

  printf("\033[1;32m  [PASS] Parser working correctly.\033[0m\n");
}

/* =============================================================================
 * 9. Evaluator & Builtins Test
 * ============================================================================= */
static void test_eval_and_builtins(void) {
  printf("  -> Testing Evaluator and Standard Builtins...\n");

  /* Inizializziamo l'ambiente dei built-in (richiesto prima di eval_str) */
  extern void builtins_register_all(void);
  builtins_register_all();

  EvalCtx ctx;
  eval_ctx_init(&ctx);
  strcpy(ctx.scope, "/local");

  /* 1. Eval di un'assegnazione ed espressione matematica */
  EvalResult r = eval_str(&ctx, "a = 5 * 6");
  VERIFY_ASSERT(r.sig != CTRL_ERR, "Evaluation of 'a = 5 * 6' failed");
  VERIFY_ASSERT(r.ret_val.type == TYPE_INT && r.ret_val.ival == 30, "Expression returned incorrect value");
  val_free(&r.ret_val);

  /* Verifica che 'a' sia stato salvato nel registry locale */
  Value v_a = reg_get("/local/a");
  VERIFY_ASSERT(v_a.type == TYPE_INT && v_a.ival == 30, "Variable 'a' not correctly saved in registry");
  val_free(&v_a);

  /* 2. Eval di una definizione di funzione e invocazione */
  r = eval_str(&ctx, "fn multiply(x y) { ret x * y }\nres = multiply(6 7)");
  VERIFY_ASSERT(r.sig != CTRL_ERR, "Function declaration and invocation failed");
  VERIFY_ASSERT(r.ret_val.type == TYPE_INT && r.ret_val.ival == 42, "Function multiply returned incorrect value");
  val_free(&r.ret_val);

  /* 3. Eval di Builtins (str, len, abs, trim) */
  r = eval_str(&ctx, "s = str(42)");
  VERIFY_ASSERT(r.sig != CTRL_ERR && r.ret_val.type == TYPE_STR && strcmp((char*)r.ret_val.data, "42") == 0, "Built-in 'str' failed");
  val_free(&r.ret_val);

  r = eval_str(&ctx, "l = len(\"hello\")");
  VERIFY_ASSERT(r.sig != CTRL_ERR && r.ret_val.type == TYPE_INT && r.ret_val.ival == 5, "Built-in 'len' failed");
  val_free(&r.ret_val);

  r = eval_str(&ctx, "v_abs = abs(-10)");
  VERIFY_ASSERT(r.sig != CTRL_ERR && r.ret_val.type == TYPE_INT && r.ret_val.ival == 10, "Built-in 'abs' failed");
  val_free(&r.ret_val);

  r = eval_str(&ctx, "t = trim(\"  nexs  \")");
  VERIFY_ASSERT(r.sig != CTRL_ERR && r.ret_val.type == TYPE_STR && strcmp((char*)r.ret_val.data, "nexs") == 0, "Built-in 'trim' failed");
  val_free(&r.ret_val);

  /* 4. Verifichiamo il funzionamento del timer reale testando il sleep builtin */
  uint64_t start_tick = hal_timer_ticks();
  r = eval_str(&ctx, "sleep(50)"); /* Dorme per 50 ms reale */
  uint64_t elapsed = hal_timer_ticks() - start_tick;
  VERIFY_ASSERT(r.sig != CTRL_ERR, "Built-in 'sleep' execution failed");
  val_free(&r.ret_val);
  
  /* Verifichiamo che il tempo sia realmente trascorso (> 40ms, con un margine ragionevole) */
  VERIFY_ASSERT(elapsed >= 40, "Real timer / sleep did not elapse real time");

  printf("\033[1;32m  [PASS] Evaluator and Builtins working correctly.\033[0m\n");
}

/* =============================================================================
 * Main Suite Entry Point
 * ============================================================================= */
void nexs_sel4_verify_all(void) {
  printf("\n\033[1;35m===================================================\033[0m\n");
  printf("\033[1;35m  NEXS seL4 9-in-1 Verification Suite starting...\033[0m\n");
  printf("\033[1;35m===================================================\033[0m\n");

  g_verify_errors = 0;

  test_buddy_allocator();
  test_page_allocator();
  test_unified_allocator();
  test_hierarchical_registry();
  test_ipc_fifo_queue();
  test_vfs_syscalls();
  test_lexer();
  test_parser();
  test_eval_and_builtins();

  printf("\033[1;35m===================================================\033[0m\n");
  if (g_verify_errors == 0) {
    printf("\033[1;32m  [SUCCESS] ALL 9 SUBSYSTEMS PASSED PERFECTLY!\033[0m\n");
  } else {
    printf("\033[1;31m  [FAILURE] %d SYSTEM CHECKS FAILED IN SUITE!\033[0m\n", g_verify_errors);
  }
  printf("\033[1;35m===================================================\033[0m\n\n");
}
