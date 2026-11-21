/* Full binary base ABI contract tests (NXOLMO role integrated in NEXS).
 * No assert(), no mocks, no runtime links to the reference NXOLMO repo.
 * Every function has positive and invalid/edge-case coverage. */
#include "nexs_checked.h"
#include "nexs_endian.h"
#include "nexs_result.h"
#include "nexs_types.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int failures;
static unsigned int checks;
static void verify(int okay, const char *name) {
  ++checks;
  if (!okay) {
    ++failures;
    fprintf(stderr, "FOUNDATION_ABI_FAIL %s\n", name);
  }
}

static void test_object_identity(void) {
  nx_object_id a = {UINT64_C(100), UINT64_C(200)};
  nx_object_id b = {UINT64_C(100), UINT64_C(200)};
  nx_object_id c = {UINT64_C(100), UINT64_C(201)};
  nx_object_id z = {0U, 0U};
  nx_object_ref x = {a, 7U};
  nx_object_ref y = {b, 7U};
  nx_object_ref stale = {a, 8U};
  nx_object_ref zero_generation = {a, 0U};
  nx_object_ref zero_identity = {z, 3U};
  verify(nx_object_id_equal(a, b), "id_equal_true");
  verify(!nx_object_id_equal(a, c), "id_equal_false");
  verify(nx_object_id_is_zero(z), "id_zero_true");
  verify(!nx_object_id_is_zero(a), "id_zero_false");
  verify(nx_object_ref_equal(x, y), "ref_equal_true");
  verify(!nx_object_ref_equal(x, stale), "ref_equal_generation_mismatch");
  verify(nx_object_ref_is_valid(x), "ref_valid_nonzero");
  verify(!nx_object_ref_is_valid(zero_generation),
         "ref_invalid_zero_generation");
  verify(!nx_object_ref_is_valid(zero_identity), "ref_invalid_zero_id");
}

static void test_structured_result(void) {
  nx_result record = {0};
  verify(nx_result_init(&record, NX_STATUS_RETRY,
                        NX_RESULT_DOMAIN_PROTOCOL, 123U, 9U, 4U,
                        NX_CYCLES_UNKNOWN, 77U) == NX_STATUS_OK,
         "result_initialize_unknown_cycles");
  verify(record.schema_id == NX_RESULT_SCHEMA_ID &&
         record.schema_version == NX_RESULT_SCHEMA_VERSION &&
         record.operation_id == 123U && record.fuel_used == 77U &&
         record.status == NX_STATUS_RETRY &&
         record.domain == NX_RESULT_DOMAIN_PROTOCOL &&
         record.flags == NX_RESULT_FLAG_CYCLES_UNKNOWN,
         "result_fields_preserved");
  verify(nx_result_validate(&record) == NX_STATUS_OK,
         "result_validate_unknown_cycles");
  verify(nx_result_init(NULL, NX_STATUS_OK, NX_RESULT_DOMAIN_BASE,
                        1U, 2U, 3U, 4U, 5U) == NX_STATUS_INVALID_ARGUMENT,
         "result_init_null");
  record.schema_version = (uint16_t)(NX_RESULT_SCHEMA_VERSION + 1U);
  verify(nx_result_validate(&record) == NX_STATUS_VERSION_MISMATCH,
         "result_wrong_version");
  record.schema_version = NX_RESULT_SCHEMA_VERSION;
  record.flags |= UINT16_C(0x8000);
  verify(nx_result_validate(&record) == NX_STATUS_MALFORMED,
         "result_unknown_flags");
  record.flags = NX_RESULT_FLAG_CYCLES_UNKNOWN;
  record.cycles_used = 3U;
  verify(nx_result_validate(&record) == NX_STATUS_MALFORMED,
         "result_flag_cycle_mismatch");
  record.flags = 0U;
  record.domain = UINT32_C(255);
  verify(nx_result_validate(&record) == NX_STATUS_MALFORMED,
         "result_bad_domain");
  verify(nx_result_validate(NULL) == NX_STATUS_INVALID_ARGUMENT,
         "result_validate_null");
  verify(nx_result_init(&record, NX_STATUS_OK, NX_RESULT_DOMAIN_BASE,
                        0U, 0U, 0U, 3U, 2U) == NX_STATUS_OK &&
         nx_result_validate(&record) == NX_STATUS_OK &&
         record.flags == 0U,
         "result_normal_cycle_count");
}

static void test_endian_contract(void) {
  uint8_t bytes[16] = {0};
  verify(CHAR_BIT == 8 && sizeof(uint16_t) == 2U &&
         sizeof(uint32_t) == 4U && sizeof(uint64_t) == 8U,
         "wire_octet_width_platform_checked");
  nx_store_le16(bytes, UINT16_C(0x1122));
  verify(bytes[0] == 0x22U && bytes[1] == 0x11U,
         "store_le16_bytes");
  verify(nx_load_le16(bytes) == UINT16_C(0x1122),
         "load_le16_roundtrip");
  nx_store_le32(bytes, UINT32_C(0x11223344));
  verify(bytes[0] == 0x44U && bytes[1] == 0x33U &&
         bytes[2] == 0x22U && bytes[3] == 0x11U,
         "store_le32_bytes");
  verify(nx_load_le32(bytes) == UINT32_C(0x11223344),
         "load_le32_roundtrip");
  nx_store_le64(bytes, UINT64_C(0x1122334455667788));
  verify(bytes[0] == 0x88U && bytes[7] == 0x11U &&
         bytes[3] == 0x55U, "store_le64_bytes");
  verify(nx_load_le64(bytes) == UINT64_C(0x1122334455667788),
         "load_le64_roundtrip");
  nx_store_be16(bytes, UINT16_C(0x1122));
  verify(bytes[0] == 0x11U && bytes[1] == 0x22U,
         "store_be16_bytes");
  verify(nx_load_be16(bytes) == UINT16_C(0x1122),
         "load_be16_roundtrip");
  nx_store_be32(bytes, UINT32_C(0x11223344));
  verify(bytes[0] == 0x11U && bytes[1] == 0x22U &&
         bytes[2] == 0x33U && bytes[3] == 0x44U,
         "store_be32_bytes");
  verify(nx_load_be32(bytes) == UINT32_C(0x11223344),
         "load_be32_roundtrip");
  nx_store_be64(bytes, UINT64_C(0x1122334455667788));
  verify(bytes[0] == 0x11U && bytes[7] == 0x88U &&
         bytes[3] == 0x44U, "store_be64_bytes");
  verify(nx_load_be64(bytes) == UINT64_C(0x1122334455667788),
         "load_be64_roundtrip");
  nx_const_span truncated = {bytes, 7U};
  nx_const_span full = {bytes, 8U};
  verify(nx_const_span_validate(truncated, 8U) == NX_STATUS_TRUNCATED,
         "load_must_reject_short_span_before_reading");
  verify(nx_const_span_validate(full, 8U) == NX_STATUS_OK,
         "load_may_read_complete_span");
  nx_mut_span short_buffer = {bytes, 7U};
  nx_mut_span writable = {bytes, 8U};
  verify(nx_mut_span_validate(short_buffer, 8U) == NX_STATUS_NO_SPACE,
         "store_must_reject_short_span_before_writing");
  verify(nx_mut_span_validate(writable, 8U) == NX_STATUS_OK,
         "store_may_write_complete_span");
  nx_store_le16(bytes, 0U);
  nx_store_le32(bytes, 0U);
  nx_store_le64(bytes, 0U);
  verify(nx_load_le64(bytes) == 0U, "little_endian_zero_edge");
  nx_store_be16(bytes, UINT16_MAX);
  nx_store_be32(bytes, UINT32_MAX);
  nx_store_be64(bytes, UINT64_MAX);
  verify(nx_load_be64(bytes) == UINT64_MAX, "big_endian_max_edge");
  nx_store_le64(bytes + 1U, UINT64_C(0x0102030405060708));
  verify(nx_load_le64(bytes + 1U) == UINT64_C(0x0102030405060708),
         "unaligned_byte_oriented_access");
}

int main(void) {
  test_object_identity();
  test_structured_result();
  test_endian_contract();
  if (failures == 0U)
    printf("FOUNDATION_COMPLETE_BASE_ABI_PASS %u checks\n", checks);
  return failures == 0U ? 0 : 1;
}
