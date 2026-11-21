/* Actual Value arithmetic and allocation-failure contracts.
 * Compiled against the one production Buddy, Value and registry.
 * No mock allocator, assertions, hidden memory manager or test bypass.
 */
#include "nexs_alloc.h"
#include "nexs_value.h"
#include "nexs_registry.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void verify(int condition, const char *description) {
  ++checks;
  if (!condition) {
    ++failures;
    fprintf(stderr, "VALUE_CHECKED_FAIL %s\n", description);
  }
}

static void test_checked_numeric(void) {
  Value max = val_int(INT64_MAX);
  Value min = val_int(INT64_MIN);
  Value neg = val_int(-1);
  Value one = val_int(1);
  Value zero = val_int(0);
  Value two = val_int(2);
  Value answer = val_add(&max, &one);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "positive addition overflow is an error, not UB");
  val_free(&answer);
  answer = val_add(&min, &neg);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "negative addition underflow is an error");
  val_free(&answer);
  answer = val_sub(&min, &one);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "signed subtraction underflow denied");
  val_free(&answer);
  answer = val_mul(&min, &neg);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "signed multiplication overflow denied");
  val_free(&answer);
  answer = val_mul(&min, &zero);
  verify(answer.type == TYPE_INT && answer.ival == 0,
         "multiplication by zero preserves signed range");
  val_free(&answer);
  answer = val_div(&min, &neg);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "division min by negative one denied before C UB");
  val_free(&answer);
  answer = val_mod(&min, &neg);
  verify(answer.type == TYPE_ERR && answer.err_code == 6,
         "modulo min by negative one denied before C UB");
  val_free(&answer);
  answer = val_div(&max, &one);
  verify(answer.type == TYPE_INT && answer.ival == INT64_MAX,
         "valid division keeps exact signed range");
  val_free(&answer);
  answer = val_add(&max, &zero);
  verify(answer.type == TYPE_INT && answer.ival == INT64_MAX,
         "valid boundary addition succeeds");
  val_free(&answer);
  answer = val_sub(&one, &two);
  verify(answer.type == TYPE_INT && answer.ival == -1,
         "valid negative subtraction succeeds");
  val_free(&answer);
  Value invalid_float = val_float(0x1p63);
  verify(val_to_int(&invalid_float) == 0,
         "out-of-range float never causes C undefined cast");
  Value normal_float = val_float(-42.5);
  verify(val_to_int(&normal_float) == -42,
         "in-range float conversion truncates toward zero");
}

static void test_ordered_comparators(void) {
  Value low = val_int(INT64_MIN);
  Value high = val_int(INT64_MAX);
  Value same = val_int(7);
  Value other = val_int(7);
  Value x = val_float(-1.5);
  Value y = val_float(3.25);
  Value unordered = val_float(NAN);
  Value answer = val_lt(&low, &high);
  verify(answer.type == TYPE_BOOL && answer.ival == 1,
         "explicit val_lt accepts full signed-int ordering");
  val_free(&answer);
  answer = val_lt(&y, &x);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "explicit val_lt rejects reversed floating ordering");
  val_free(&answer);
  answer = val_gt(&high, &low);
  verify(answer.type == TYPE_BOOL && answer.ival == 1,
         "explicit val_gt accepts signed-int extremities");
  val_free(&answer);
  answer = val_gt(&x, &y);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "explicit val_gt rejects reversed floating ordering");
  val_free(&answer);
  answer = val_le(&same, &other);
  verify(answer.type == TYPE_BOOL && answer.ival == 1,
         "explicit val_le includes equality");
  val_free(&answer);
  answer = val_le(&high, &low);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "explicit val_le denies reversed extremities");
  val_free(&answer);
  answer = val_ge(&same, &other);
  verify(answer.type == TYPE_BOOL && answer.ival == 1,
         "explicit val_ge includes equality");
  val_free(&answer);
  answer = val_ge(&low, &high);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "explicit val_ge denies reversed extremities");
  val_free(&answer);
  answer = val_lt(&unordered, &same);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "NaN is unordered: val_lt must be false");
  val_free(&answer);
  answer = val_le(&unordered, &same);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "NaN is unordered: val_le must be false");
  val_free(&answer);
  answer = val_gt(&same, &unordered);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "NaN is unordered: val_gt must be false");
  val_free(&answer);
  answer = val_ge(&same, &unordered);
  verify(answer.type == TYPE_BOOL && answer.ival == 0,
         "NaN is unordered: val_ge must be false");
  val_free(&answer);
  answer = val_lt(NULL, &low);
  verify(answer.type == TYPE_ERR, "null comparator operand rejected");
  val_free(&answer);
}

static void test_untruncated_concat(void) {
  char left_bytes[901];
  char right_bytes[901];
  memset(left_bytes, 'a', sizeof(left_bytes)-1U);
  left_bytes[900] = '\0';
  memset(right_bytes, 'b', sizeof(right_bytes)-1U);
  right_bytes[900] = '\0';
  Value left = val_str(left_bytes);
  Value right = val_str(right_bytes);
  verify(left.type == TYPE_STR && right.type == TYPE_STR,
         "source values admitted by real Buddy");
  if (left.type == TYPE_STR && right.type == TYPE_STR) {
    Value concatenated = val_add(&left, &right);
    verify(concatenated.type == TYPE_STR && concatenated.data != NULL,
           "one contiguous concatenation allocation admitted");
    if (concatenated.type == TYPE_STR && concatenated.data != NULL) {
      const char *actual = (const char *)concatenated.data;
      verify(strlen(actual) == 1800U,
             "concatenation preserves text past profile MAX_STR_LEN");
      verify(memcmp(actual, left_bytes, 900U) == 0 &&
             memcmp(actual + 900U, right_bytes, 900U) == 0,
             "concatenation preserves both inputs byte-exactly");
    }
    val_free(&concatenated);
    Value integer = val_int(17);
    Value mixed = val_add(&left, &integer);
    verify(mixed.type == TYPE_STR && mixed.data != NULL &&
           strlen((char *)mixed.data) == 902U &&
           strcmp((char *)mixed.data + 900U, "17") == 0,
           "string plus integer retains exact numeric suffix");
    val_free(&mixed);
  }
  val_free(&left);
  val_free(&right);
}

static void test_exhausted_string_admission(void) {
  Value retained = val_str("retained");
  verify(retained.type == TYPE_STR, "save source before Buddy OOM");
  void *held[256] = {0};
  size_t count = 0U;
  while (count < sizeof(held)/sizeof(held[0])) {
    void *next = buddy_alloc(256U);
    if (next == NULL)
      break;
    held[count] = next;
    ++count;
  }
  /* The retained tiny string can leave smaller fragments after every
   * 256-byte block is occupied. Fill the REAL minimum blocks too. */
  void *small_held[256] = {0};
  size_t small_count = 0U;
  while (small_count < sizeof(small_held)/sizeof(small_held[0])) {
    void *next = buddy_alloc(MIN_BLOCK);
    if (next == NULL)
      break;
    small_held[small_count] = next;
    ++small_count;
  }
  verify(count > 1U && buddy_alloc(MIN_BLOCK) == NULL,
         "exhaust every real provider block, including small fragments");
  verify(buddy_strdup("unavailable") == NULL,
         "string copy fails recoverably without die");
  Value allocation = val_str("unavailable");
  verify(allocation.type == TYPE_ERR && allocation.err_code == 12,
         "string constructor surfaces OOM as Value error");
  val_free(&allocation);
  allocation = val_ref("/obj/unavailable");
  verify(allocation.type == TYPE_ERR && allocation.err_code == 12,
         "reference constructor surfaces OOM");
  val_free(&allocation);
  allocation = val_ptr("/obj/unavailable");
  verify(allocation.type == TYPE_ERR && allocation.err_code == 12,
         "pointer constructor surfaces OOM");
  val_free(&allocation);
  allocation = val_clone(&retained);
  verify(allocation.type == TYPE_ERR && allocation.err_code == 12,
         "string clone surfaces OOM");
  val_free(&allocation);
  allocation = val_add(&retained, &retained);
  verify(allocation.type == TYPE_ERR && allocation.err_code == 12,
         "concatenation surfaces real Buddy OOM");
  val_free(&allocation);
  for (size_t i = 0U; i < small_count; ++i)
    buddy_free(small_held[i]);
  for (size_t i = 0U; i < count; ++i)
    buddy_free(held[i]);
  val_free(&retained);
  allocation = val_str("after reclamation");
  verify(allocation.type == TYPE_STR && allocation.data != NULL,
         "reclaimed Buddy still admits real new values");
  val_free(&allocation);
}

static void test_array_clone_oom_preserves_existing_slot(void) {
  DynArray *array = arr_create_anon();
  verify(array != NULL, "real anonymous array admitted from Buddy");
  if (array == NULL)
    return;
  Value old = val_str("stable");
  Value replacement = val_str("replacement");
  Value original_error = val_err(77, "retained diagnostic");
  verify(original_error.type == TYPE_ERR && original_error.err_msg != NULL,
         "original error diagnostic is independently owned");
  verify(old.type == TYPE_STR && replacement.type == TYPE_STR,
         "two independently owned Value strings exist before OOM");
  if (old.type != TYPE_STR || replacement.type != TYPE_STR) {
    val_free(&old);
    val_free(&replacement);
    val_free(&original_error);
    arr_unref(array);
    return;
  }
  verify(arr_set_owned(array, 0U, old) == 0,
         "original array slot installed before forced exhaustion");
  size_t capacity = array->capacity;
  size_t count = array->size;
  void *held[4096] = {0};
  size_t consumed = 0U;
  while (consumed < sizeof(held)/sizeof(held[0])) {
    void *block = buddy_alloc(MIN_BLOCK);
    if (block == NULL)
      break;
    held[consumed] = block;
    ++consumed;
  }
  verify(consumed > 10U && buddy_alloc(MIN_BLOCK) == NULL,
         "all remaining provider leaves exhausted with real allocations");
  verify(arr_set(array, 0U, replacement) != 0,
         "replacement clone refuses genuine Buddy exhaustion");
  verify(array->size == count && array->capacity == capacity &&
         array->items[0].type == TYPE_STR &&
         strcmp(array->items[0].data, "stable") == 0,
         "failed clone preserves original Value, size and capacity");
  Value unchanged = val_int(42);
  verify(val_clone_checked(NULL, &unchanged) != 0 &&
         unchanged.type == TYPE_INT && unchanged.ival == 42,
         "checked clone rejects missing source without modifying output");
  verify(val_clone_checked(&original_error, &unchanged) != 0 &&
         unchanged.type == TYPE_INT && unchanged.ival == 42,
         "checked clone rejects loss of a TYPE_ERR diagnostic on OOM");
  verify(arr_set(array, 0U, original_error) != 0,
         "array rejects truncated error-message ownership on OOM");
  verify(array->size == count && array->items[0].type == TYPE_STR &&
         strcmp(array->items[0].data, "stable") == 0,
         "failed TYPE_ERR clone cannot replace valid old array value");
  verify(arr_set(array, count + 100U, replacement) != 0,
         "failed large-index write cannot force partial capacity growth");
  verify(array->size == count && array->capacity == capacity &&
         strcmp(array->items[0].data, "stable") == 0,
         "failed growth does not alter any existing member");
  for (size_t i=0U; i<consumed; ++i)
    buddy_free(held[i]);
  verify(arr_set(array, 0U, original_error) == 0 &&
         array->items[0].type == TYPE_ERR &&
         array->items[0].err_msg != NULL &&
         strcmp(array->items[0].err_msg, "retained diagnostic") == 0,
         "released provider permits exact TYPE_ERR diagnostic clone");
  verify(arr_set(array, 0U, replacement) == 0,
         "same array accepts replacement after provider reclamation");
  verify(array->items[0].type == TYPE_STR &&
         strcmp(array->items[0].data, "replacement") == 0,
         "recovered replacement contains exact intended bytes");
  val_free(&replacement);
  val_free(&original_error);
  arr_unref(array);
  void *whole = buddy_alloc(POOL_SIZE);
  verify(whole != NULL, "Value and array release return full 32KiB pool");
  if (whole != NULL)
    buddy_free(whole);
}

static void test_registry_error_clone_oom_atomicity(void) {
  verify(reg_init() == 0,
         "single real registry admitted after independent arena tests");
  verify(reg_set("/typed_error", val_int(99), RK_ALL) == 0,
         "original typed value installed in canonical namespace");
  size_t keys = g_registry.total_keys;
  Value error = val_err(83, "must preserve original error diagnostic");
  verify(error.type == TYPE_ERR && error.err_msg != NULL,
         "typed source error owns diagnostic before registry OOM");
  void *held[4096] = {0};
  size_t consumed = 0U;
  while (consumed < sizeof(held) / sizeof(held[0])) {
    void *block = buddy_alloc(MIN_BLOCK);
    if (block == NULL)
      break;
    held[consumed] = block;
    ++consumed;
  }
  verify(consumed > 10U && buddy_alloc(MIN_BLOCK) == NULL,
         "registry clone test actually exhausted the real Buddy");
  verify(reg_set("/typed_error", error, RK_ALL) != 0,
         "registry refuses partially cloned error under real OOM");
  Value retained = reg_get("/typed_error");
  verify(g_registry.total_keys == keys && retained.type == TYPE_INT &&
         retained.ival == 99,
         "failed typed error clone preserves exact old registry Value");
  val_free(&retained);
  for (size_t i = 0U; i < consumed; ++i)
    buddy_free(held[i]);
  verify(reg_set("/typed_error", error, RK_ALL) == 0,
         "registry accepts same source error after real provider reclamation");
  retained = reg_get("/typed_error");
  verify(retained.type == TYPE_ERR && retained.err_code == 83 &&
         retained.err_msg != NULL &&
         strcmp(retained.err_msg, "must preserve original error diagnostic") == 0,
         "registry reads back exact cloned error code and full message");
  val_free(&retained);
  val_free(&error);
  verify(reg_delete("/typed_error") == 0,
         "released registry value cleans up its original owned diagnostic");
}

int main(void) {
  verify(POOL_SIZE == 32768U, "real 32KiB provider test profile selected");
  test_checked_numeric();
  test_ordered_comparators();
  test_untruncated_concat();
  test_exhausted_string_admission();
  test_array_clone_oom_preserves_existing_slot();
  test_registry_error_clone_oom_atomicity();
  printf("VALUE_CHECKED_REAL_SOURCE %s checks=%u failures=%u\n",
         failures == 0U ? "PASS" : "FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
