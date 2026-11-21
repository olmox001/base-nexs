/* NXOLMO result schema v1, now a single product-owned status/result ABI.
 * Do not serialize the native C struct: use checked field codecs instead. */
#ifndef NEXS_RESULT_H
#define NEXS_RESULT_H
#include "nexs_checked.h"
#include "nexs_types.h"

#define NX_RESULT_SCHEMA_ID UINT32_C(0x3152584e)
#define NX_RESULT_SCHEMA_VERSION UINT16_C(1)
enum {
  NX_RESULT_FLAG_CYCLES_UNKNOWN = UINT16_C(1) << 0,
  NX_RESULT_FLAG_PARTIAL = UINT16_C(1) << 1,
  NX_RESULT_FLAG_RETRYABLE = UINT16_C(1) << 2
};

typedef enum nx_result_domain {
  NX_RESULT_DOMAIN_NONE = 0,
  NX_RESULT_DOMAIN_BASE = 1,
  NX_RESULT_DOMAIN_RUNTIME = 2,
  NX_RESULT_DOMAIN_REGISTRY = 3,
  NX_RESULT_DOMAIN_STORAGE = 4,
  NX_RESULT_DOMAIN_PROTOCOL = 5,
  NX_RESULT_DOMAIN_SECURITY = 6,
  NX_RESULT_DOMAIN_IMAGE = 7
} nx_result_domain;
typedef struct nx_result {
  uint32_t schema_id;
  uint16_t schema_version;
  uint16_t flags;
  nx_status status;
  uint32_t domain;
  uint64_t operation_id;
  uint64_t completed;
  uint64_t detail;
  uint64_t cycles_used;
  uint64_t fuel_used;
} nx_result;

nx_status nx_result_init(nx_result *result, nx_status status,
                         nx_result_domain domain, uint64_t operation_id,
                         uint64_t completed, uint64_t detail,
                         uint64_t cycles_used, uint64_t fuel_used);
nx_status nx_result_validate(const nx_result *result);
#endif
