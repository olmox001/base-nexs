/*
 * Canonical NEXS binary ABI, checked arithmetic and span contracts.
 *
 * Product-owned implementation. Status values, validated spans and all
 * arithmetic/bounds operations preserve the audited NXOLMO baseline ABI;
 * NXOLMO is a read-only source reference, never a production dependency.
 *
 * The same ABI is used by state/object/memory (NXOLMO role), trusted REG
 * transport (NXEC role) and compiler/runtime (Base-NEXS role).
 */
#ifndef NEXS_CHECKED_H
#define NEXS_CHECKED_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t nx_status;
enum {
  NX_STATUS_OK = UINT32_C(0x00000000),
  NX_STATUS_INVALID_ARGUMENT = UINT32_C(0x00000001),
  NX_STATUS_TRUNCATED = UINT32_C(0x00000002),
  NX_STATUS_MALFORMED = UINT32_C(0x00000003),
  NX_STATUS_LIMIT_EXCEEDED = UINT32_C(0x00000004),
  NX_STATUS_NO_SPACE = UINT32_C(0x00000005),
  NX_STATUS_OVERFLOW = UINT32_C(0x00000006),
  NX_STATUS_NOT_SUPPORTED = UINT32_C(0x00000007),
  NX_STATUS_NOT_FOUND = UINT32_C(0x00000008),
  NX_STATUS_STALE_REFERENCE = UINT32_C(0x00000009),
  NX_STATUS_CONFLICT = UINT32_C(0x0000000a),
  NX_STATUS_ACCESS_DENIED = UINT32_C(0x0000000b),
  NX_STATUS_BUSY = UINT32_C(0x0000000c),
  NX_STATUS_RESOURCE_EXHAUSTED = UINT32_C(0x0000000d),
  NX_STATUS_INTEGRITY_FAILURE = UINT32_C(0x0000000e),
  NX_STATUS_CORRUPT = UINT32_C(0x0000000f),
  NX_STATUS_CANCELLED = UINT32_C(0x00000010),
  NX_STATUS_TIMEOUT = UINT32_C(0x00000011),
  NX_STATUS_ALREADY_COMMITTED = UINT32_C(0x00000012),
  NX_STATUS_IO_ERROR = UINT32_C(0x00000013),
  NX_STATUS_RETRY = UINT32_C(0x00000014),
  NX_STATUS_INTERNAL = UINT32_C(0x00000015),
  NX_STATUS_BUDGET_EXHAUSTED = UINT32_C(0x00000016),
  NX_STATUS_VERSION_MISMATCH = UINT32_C(0x00000017),
  NX_STATUS_UNAVAILABLE = UINT32_C(0x00000018)
};

typedef struct nx_const_span {
  const uint8_t *data;
  size_t size;
} nx_const_span;

typedef struct nx_mut_span {
  uint8_t *data;
  size_t size;
} nx_mut_span;

const char *nx_status_name(nx_status status);

nx_status nx_size_add(size_t left, size_t right, size_t *result);
nx_status nx_size_mul(size_t left, size_t right, size_t *result);
nx_status nx_u64_add(uint64_t left, uint64_t right, uint64_t *result);
nx_status nx_u64_mul(uint64_t left, uint64_t right, uint64_t *result);

/* Signed operations: portable, non-UB and with an untouched single ABI.
 * A failed operation clears the output and never creates a partial result. */
nx_status nx_i64_add(int64_t left, int64_t right, int64_t *result);
nx_status nx_i64_sub(int64_t left, int64_t right, int64_t *result);
nx_status nx_i64_mul(int64_t left, int64_t right, int64_t *result);

/* Return -1 for invalid/overflowing pointer ranges, 0 for disjoint,
 * 1 for overlap; empty ranges are disjoint. No raw pointer dereference. */
int nx_pointer_ranges_overlap(const void *left, size_t left_size,
                              const void *right, size_t right_size);

/* End is zeroed on failure and inclusive upper bound is checked against
 * the exclusive end (offset+length <= limit). */
nx_status nx_range_validate(uint64_t offset, uint64_t length,
                            uint64_t limit, uint64_t *end);
nx_status nx_const_span_validate(nx_const_span span, size_t required_size);
nx_status nx_mut_span_validate(nx_mut_span span, size_t required_size);

#endif /* NEXS_CHECKED_H */
