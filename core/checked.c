/* Complete canonical checked arithmetic/span contract (NXOLMO role).
 * Migrated into the only NEXS product core: no libnxolmo runtime linkage.
 * All functions return explicit status and clear an output on failure.
 */
#include "include/nexs_checked.h"
#include <limits.h>

nx_status nx_size_add(size_t left, size_t right, size_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0U;
  if (right > SIZE_MAX - left) return NX_STATUS_OVERFLOW;
  *result = left + right;
  return NX_STATUS_OK;
}

nx_status nx_size_mul(size_t left, size_t right, size_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0U;
  if (left != 0U && right > SIZE_MAX / left) return NX_STATUS_OVERFLOW;
  *result = left * right;
  return NX_STATUS_OK;
}

nx_status nx_u64_add(uint64_t left, uint64_t right, uint64_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0U;
  if (right > UINT64_MAX - left) return NX_STATUS_OVERFLOW;
  *result = left + right;
  return NX_STATUS_OK;
}

nx_status nx_u64_mul(uint64_t left, uint64_t right, uint64_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0U;
  if (left != 0U && right > UINT64_MAX / left) return NX_STATUS_OVERFLOW;
  *result = left * right;
  return NX_STATUS_OK;
}

nx_status nx_i64_add(int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0;
  if ((right > 0 && left > INT64_MAX - right) ||
      (right < 0 && left < INT64_MIN - right))
    return NX_STATUS_OVERFLOW;
  *result = left + right;
  return NX_STATUS_OK;
}

nx_status nx_i64_sub(int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0;
  if ((right < 0 && left > INT64_MAX + right) ||
      (right > 0 && left < INT64_MIN + right))
    return NX_STATUS_OVERFLOW;
  *result = left - right;
  return NX_STATUS_OK;
}

nx_status nx_i64_mul(int64_t left, int64_t right, int64_t *result) {
  if (result == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *result = 0;
  if ((left > 0 && right > 0 && left > INT64_MAX / right) ||
      (left > 0 && right < 0 && right < INT64_MIN / left) ||
      (left < 0 && right > 0 && left < INT64_MIN / right) ||
      (left < 0 && right < 0 && left < INT64_MAX / right))
    return NX_STATUS_OVERFLOW;
  *result = left * right;
  return NX_STATUS_OK;
}

int nx_pointer_ranges_overlap(const void *left, size_t left_size,
                              const void *right, size_t right_size) {
  if (left_size == 0U || right_size == 0U) return 0;
  if (left == NULL || right == NULL) return -1;
  uintptr_t ls = (uintptr_t)left;
  uintptr_t rs = (uintptr_t)right;
  if ((uintmax_t)left_size > (uintmax_t)(UINTPTR_MAX - ls) ||
      (uintmax_t)right_size > (uintmax_t)(UINTPTR_MAX - rs)) return -1;
  uintptr_t le = ls + (uintptr_t)left_size;
  uintptr_t re = rs + (uintptr_t)right_size;
  return (ls < re && rs < le) ? 1 : 0;
}

nx_status nx_range_validate(uint64_t offset, uint64_t length,
                            uint64_t limit, uint64_t *end) {
  if (end == NULL) return NX_STATUS_INVALID_ARGUMENT;
  *end = 0U;
  uint64_t computed = 0U;
  nx_status status = nx_u64_add(offset, length, &computed);
  if (status != NX_STATUS_OK) return status;
  if (computed > limit) return NX_STATUS_LIMIT_EXCEEDED;
  *end = computed;
  return NX_STATUS_OK;
}

nx_status nx_const_span_validate(nx_const_span span, size_t required_size) {
  if (span.data == NULL && span.size != 0U)
    return NX_STATUS_INVALID_ARGUMENT;
  if (required_size > span.size) return NX_STATUS_TRUNCATED;
  return NX_STATUS_OK;
}

nx_status nx_mut_span_validate(nx_mut_span span, size_t required_size) {
  if (span.data == NULL && span.size != 0U)
    return NX_STATUS_INVALID_ARGUMENT;
  if (required_size > span.size) return NX_STATUS_NO_SPACE;
  return NX_STATUS_OK;
}
