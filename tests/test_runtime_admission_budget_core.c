/* Real hosted NEXS runtime entry and evaluator: singleton reentry fencing
 * and explicit per-EvalCtx deterministic fuel; not a full multi-instance VM.
 */
#include "nexs_runtime.h"
#include "nexs_eval.h"
#include "nexs_registry.h"
#include "nexs_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void verify(int condition, const char *description) {
  ++checks;
  if (!condition) {
    ++failures;
    fprintf(stderr, "RUNTIME_ADMISSION_BUDGET_FAIL %s\n", description);
  }
}

static void test_live_namespace_reentry(void) {
  verify(nexs_runtime_init() == 0,
         "actual selected hosted runtime fully initializes once");
  verify(reg_set("/p2/admission/value", val_int(42), RK_ALL) == 0,
         "real runtime registry accepts persistent object");
  RegKey *original_root = g_registry.root;
  size_t count = g_registry.total_keys;
  verify(original_root != NULL && count >= 7U,
         "root and canonical bootstrap objects have real owners");
  verify(nexs_runtime_init() != 0,
         "second initialization refuses to zero live Buddy state");
  Value state = reg_get("/p2/admission/value");
  verify(g_registry.root == original_root && g_registry.total_keys == count &&
         state.type == TYPE_INT && state.ival == 42,
         "refused reentry preserves original object identity and value");
  val_free(&state);
}

static void test_fuel_admission_and_exhaustion(void) {
  EvalCtx first;
  EvalCtx second;
  eval_ctx_init(&first);
  eval_ctx_init(&second);
  verify(eval_ctx_set_fuel_budget(NULL, 7U) != 0,
         "invalid evaluator owner cannot receive budget");
  verify(eval_ctx_set_fuel_budget(&first, 0U) == 0,
         "zero fuel is admitted as a fail-closed execution budget");
  EvalResult result = eval_str(&first, "out 1\n");
  verify(result.sig == CTRL_ERR && (first.execution_budget.fuel_limit - first.execution_budget.fuel_remaining) == 0U &&
         first.execution_budget.fuel_remaining == 0U,
         "zero budget blocks real evaluator before AST dispatch");
  val_free(&result.ret_val);

  verify(eval_ctx_set_fuel_budget(&first, 9U) == 0,
         "explicit owner can restore finite execution budget");
  result = eval_str(&first, "loop {\n}\n");
  verify(result.sig == CTRL_ERR && result.ret_val.type == TYPE_ERR,
         "even zero-statement infinite loop stops with budget error");
  verify(first.execution_budget.fuel_remaining == 0U && (first.execution_budget.fuel_limit - first.execution_budget.fuel_remaining) == 9U,
         "empty loop consumes all exactly accounted AST fuel");
  val_free(&result.ret_val);

  verify(eval_ctx_set_fuel_budget(&second, 100U) == 0,
         "independent evaluator context gets independent budget");
  result = eval_str(&second, "a = 7\nb = a + 1\n");
  verify(result.sig == CTRL_NONE,
         "bounded real evaluator completes a finite two-statement program");
  verify((second.execution_budget.fuel_limit - second.execution_budget.fuel_remaining) > 0U && (second.execution_budget.fuel_limit - second.execution_budget.fuel_remaining) < 100U &&
         second.execution_budget.fuel_remaining + (second.execution_budget.fuel_limit - second.execution_budget.fuel_remaining) == 100U,
         "each actual evaluator AST dispatch consumes deterministic fuel");
  val_free(&result.ret_val);
  verify(first.execution_budget.fuel_remaining == 0U && (first.execution_budget.fuel_limit - first.execution_budget.fuel_remaining) == 9U,
         "other context execution does not reset exhausted fuel accounting");

  verify(eval_ctx_set_fuel_budget(&first, 100U) == 0,
         "exhausted context can receive a fresh explicit budget");
  result = eval_str(&first, "x = 11\n");
  verify(result.sig == CTRL_NONE && (first.execution_budget.fuel_limit - first.execution_budget.fuel_remaining) > 0U,
         "same context recovers after actual budget readmission");
  val_free(&result.ret_val);
}

int main(void) {
  test_live_namespace_reentry();
  test_fuel_admission_and_exhaustion();
  printf("RUNTIME_ADMISSION_BUDGET %s checks=%u failures=%u\n",
         failures == 0U ? "PASS" : "FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
