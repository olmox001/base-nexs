/* Actual product-owned ABI conformance and adversarial tests.
 * All 9 exported functions have positive AND failure/edge test coverage.
 * No mock implementations, asserts, external runtime library or shortcuts. */
#include "nexs_checked.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int failed = 0U;
static unsigned int checked = 0U;

static void check(int condition, const char *label) {
  ++checked;
  if (!condition) {
    ++failed;
    fprintf(stderr, "CHECKED_ABI_FAILURE %s\n", label);
  }
}
static void test_size_add(void) {
  size_t n = 999U;
  check(nx_size_add(23U, 19U, &n) == NX_STATUS_OK && n == 42U,
        "size_add_success");
  check(nx_size_add(SIZE_MAX, 1U, &n) == NX_STATUS_OVERFLOW && n == 0U,
        "size_add_overflow_no_stale_output");
  check(nx_size_add(1U, 1U, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "size_add_null_output");
}
static void test_size_mul(void) {
  size_t n = 999U;
  check(nx_size_mul(6U, 7U, &n) == NX_STATUS_OK && n == 42U,
        "size_mul_success");
  check(nx_size_mul(SIZE_MAX, 2U, &n) == NX_STATUS_OVERFLOW && n == 0U,
        "size_mul_overflow_no_stale_output");
  check(nx_size_mul(SIZE_MAX, 0U, &n) == NX_STATUS_OK && n == 0U,
        "size_mul_zero");
  check(nx_size_mul(1U, 1U, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "size_mul_null_output");
}
static void test_u64_add(void) {
  uint64_t n = 99U;
  check(nx_u64_add(41U, 1U, &n) == NX_STATUS_OK && n == 42U,
        "u64_add_success");
  check(nx_u64_add(UINT64_MAX, 1U, &n) == NX_STATUS_OVERFLOW && n == 0U,
        "u64_add_overflow");
  check(nx_u64_add(0U, 0U, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "u64_add_null_output");
}
static void test_u64_mul(void) {
  uint64_t n = 99U;
  check(nx_u64_mul(21U, 2U, &n) == NX_STATUS_OK && n == 42U,
        "u64_mul_success");
  check(nx_u64_mul(UINT64_MAX, 2U, &n) == NX_STATUS_OVERFLOW && n == 0U,
        "u64_mul_overflow");
  check(nx_u64_mul(UINT64_MAX, 0U, &n) == NX_STATUS_OK && n == 0U,
        "u64_mul_zero");
  check(nx_u64_mul(0U, 0U, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "u64_mul_null_output");
}
static void test_signed_arithmetic(void) {
  int64_t out = 123;
  check(nx_i64_add(41, 1, &out) == NX_STATUS_OK && out == 42,
        "signed_add_positive");
  check(nx_i64_add(INT64_MIN, -1, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_add_negative_underflow");
  check(nx_i64_add(INT64_MAX, 1, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_add_positive_overflow");
  check(nx_i64_add(0, 0, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "signed_add_missing_output");

  check(nx_i64_sub(40, -2, &out) == NX_STATUS_OK && out == 42,
        "signed_sub_positive");
  check(nx_i64_sub(INT64_MIN, 1, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_sub_negative_underflow");
  check(nx_i64_sub(INT64_MAX, -1, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_sub_positive_overflow");
  check(nx_i64_sub(0, 0, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "signed_sub_missing_output");

  check(nx_i64_mul(21, 2, &out) == NX_STATUS_OK && out == 42,
        "signed_mul_positive");
  check(nx_i64_mul(-7, 6, &out) == NX_STATUS_OK && out == -42,
        "signed_mul_negative");
  check(nx_i64_mul(INT64_MIN, -1, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_mul_min_by_negative_one");
  check(nx_i64_mul(INT64_MAX, 2, &out) == NX_STATUS_OVERFLOW && out == 0,
        "signed_mul_positive_overflow");
  check(nx_i64_mul(INT64_MIN, 0, &out) == NX_STATUS_OK && out == 0,
        "signed_mul_zero_from_min");
  check(nx_i64_mul(0, 0, NULL) == NX_STATUS_INVALID_ARGUMENT,
        "signed_mul_missing_output");
}
static void test_pointer_ranges_overlap(void) {
  uint8_t bytes[64] = {0};
  check(nx_pointer_ranges_overlap(bytes, 16U, bytes + 15U, 3U) == 1,
        "ranges_overlap");
  check(nx_pointer_ranges_overlap(bytes, 16U, bytes + 16U, 3U) == 0,
        "ranges_touch_not_overlap");
  check(nx_pointer_ranges_overlap(NULL, 0U, bytes, 3U) == 0,
        "ranges_empty_disjoint");
  check(nx_pointer_ranges_overlap(NULL, 1U, bytes, 3U) == -1,
        "ranges_null_nonempty");
  check(nx_pointer_ranges_overlap((void *)(uintptr_t)(UINTPTR_MAX - 2U),
                                  8U, bytes, 3U) == -1,
        "ranges_overflow");
}
static void test_range_validate(void) {
  uint64_t end = 99U;
  check(nx_range_validate(12U, 30U, 42U, &end) == NX_STATUS_OK &&
        end == 42U, "range_success_at_limit");
  check(nx_range_validate(12U, 31U, 42U, &end) ==
        NX_STATUS_LIMIT_EXCEEDED && end == 0U,
        "range_limit");
  check(nx_range_validate(UINT64_MAX, 1U, UINT64_MAX, &end) ==
        NX_STATUS_OVERFLOW && end == 0U,
        "range_overflow");
  check(nx_range_validate(1U, 1U, 1U, NULL) ==
        NX_STATUS_INVALID_ARGUMENT,
        "range_null_output");
}
static void test_const_span(void) {
  uint8_t data[8] = {0};
  nx_const_span span = {data, sizeof(data)};
  nx_const_span bad = {NULL, 4U};
  nx_const_span empty = {NULL, 0U};
  check(nx_const_span_validate(span, 8U) == NX_STATUS_OK,
        "const_span_exact");
  check(nx_const_span_validate(span, 9U) == NX_STATUS_TRUNCATED,
        "const_span_truncated");
  check(nx_const_span_validate(bad, 1U) == NX_STATUS_INVALID_ARGUMENT,
        "const_span_null_nonempty");
  check(nx_const_span_validate(empty, 0U) == NX_STATUS_OK,
        "const_span_empty");
}
static void test_mut_span(void) {
  uint8_t data[8] = {0};
  nx_mut_span span = {data, sizeof(data)};
  nx_mut_span bad = {NULL, 4U};
  nx_mut_span empty = {NULL, 0U};
  check(nx_mut_span_validate(span, 8U) == NX_STATUS_OK,
        "mut_span_exact");
  check(nx_mut_span_validate(span, 9U) == NX_STATUS_NO_SPACE,
        "mut_span_no_space");
  check(nx_mut_span_validate(bad, 1U) == NX_STATUS_INVALID_ARGUMENT,
        "mut_span_null_nonempty");
  check(nx_mut_span_validate(empty, 0U) == NX_STATUS_OK,
        "mut_span_empty");
}
static void test_status_name(void) {
  static const char *const names[] = {
    "OK", "INVALID_ARGUMENT", "TRUNCATED", "MALFORMED",
    "LIMIT_EXCEEDED", "NO_SPACE", "OVERFLOW", "NOT_SUPPORTED",
    "NOT_FOUND", "STALE_REFERENCE", "CONFLICT", "ACCESS_DENIED",
    "BUSY", "RESOURCE_EXHAUSTED", "INTEGRITY_FAILURE", "CORRUPT",
    "CANCELLED", "TIMEOUT", "ALREADY_COMMITTED", "IO_ERROR",
    "RETRY", "INTERNAL", "BUDGET_EXHAUSTED",
    "VERSION_MISMATCH", "UNAVAILABLE"
  };
  for (size_t i = 0U; i < sizeof(names)/sizeof(names[0]); ++i)
    check(strcmp(nx_status_name((nx_status)i), names[i]) == 0,
          "status_code_name_exact_abi");
  check(strcmp(nx_status_name(UINT32_MAX), "UNKNOWN_STATUS") == 0,
        "status_unknown");
}
int main(void) {
  test_size_add();
  test_size_mul();
  test_u64_add();
  test_u64_mul();
  test_signed_arithmetic();
  test_pointer_ranges_overlap();
  test_range_validate();
  test_const_span();
  test_mut_span();
  test_status_name();
  if (failed == 0U)
    printf("CHECKED_CANONICAL_ABI_PASS %u assertions\n", checked);
  return failed == 0U ? 0 : 1;
}
