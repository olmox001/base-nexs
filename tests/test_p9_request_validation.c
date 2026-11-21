/* Real product 9P2000 structural admission and bounds regression. */
#include "nexs_p9_frame.h"
#include "nexs_fs9p.h"
#include "nexs_endian.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "9P validation failure: %s line %d\n", #x, __LINE__); \
    return 1; \
} } while (0)

static nx_status check_wire(uint8_t *wire, uint32_t length) {
    nx_9p_frame_view view;
    size_t consumed = 0u;
    nx_const_span input = {wire, length};
    nx_status status = nx_9p_frame_decode(input, 8192u, &view, &consumed);
    if (status != NX_STATUS_OK)
        return status;
    if (consumed != length)
        return NX_STATUS_MALFORMED;
    return nx_9p_request_validate(&view);
}

static int test_clunk_and_frame_lengths(void) {
    uint8_t wire[32] = {0};
    nx_store_le32(wire, 11u);
    wire[4] = P9_TCLUNK;
    nx_store_le16(wire + 5u, 0x3210u);
    nx_store_le32(wire + 7u, 0x12345678u);
    CHECK(check_wire(wire, 11u) == NX_STATUS_OK);
    CHECK(check_wire(wire, 10u) == NX_STATUS_TRUNCATED);
    nx_store_le32(wire, 12u);
    CHECK(check_wire(wire, 11u) == NX_STATUS_TRUNCATED);
    nx_store_le32(wire, 10u);
    CHECK(check_wire(wire, 11u) == NX_STATUS_MALFORMED);
    nx_store_le32(wire, 11u);
    CHECK(check_wire(wire, 12u) == NX_STATUS_MALFORMED);
    wire[4] = 0u;
    CHECK(check_wire(wire, 11u) == NX_STATUS_NOT_SUPPORTED);
    return 0;
}

static int test_wire_fields(void) {
    uint8_t wire[64] = {0};
    nx_store_le32(wire, 19u);
    wire[4] = P9_TATTACH;
    /* fid4, afid4, uname string, aname string */
    CHECK(check_wire(wire, 19u) == NX_STATUS_OK);
    nx_store_le16(wire + 15u, 4u);
    CHECK(check_wire(wire, 19u) == NX_STATUS_TRUNCATED);
    nx_store_le16(wire + 15u, 0u);
    nx_store_le32(wire, 17u);
    wire[4] = P9_TVERSION;
    nx_store_le32(wire + 7u, 8192u);
    nx_store_le16(wire + 11u, 4u);
    memcpy(wire + 13u, "9P20", 4u);
    CHECK(check_wire(wire, 17u) == NX_STATUS_OK);
    nx_store_le16(wire + 11u, 5u);
    CHECK(check_wire(wire, 17u) == NX_STATUS_TRUNCATED);
    nx_store_le32(wire, 17u);
    wire[4] = P9_TWALK;
    nx_store_le16(wire + 15u, 0u);
    CHECK(check_wire(wire, 17u) == NX_STATUS_OK);
    nx_store_le16(wire + 15u, 1u);
    CHECK(check_wire(wire, 17u) == NX_STATUS_TRUNCATED);
    return 0;
}

static int test_write_byte_count(void) {
    uint8_t wire[32] = {0};
    nx_store_le32(wire, 26u);
    wire[4] = P9_TWRITE;
    nx_store_le32(wire + 19u, 3u);
    wire[23] = 'A'; wire[24] = 'B'; wire[25] = 'C';
    CHECK(check_wire(wire, 26u) == NX_STATUS_OK);
    nx_store_le32(wire + 19u, 4u);
    CHECK(check_wire(wire, 26u) == NX_STATUS_TRUNCATED);
    nx_store_le32(wire + 19u, 2u);
    CHECK(check_wire(wire, 26u) == NX_STATUS_MALFORMED);
    return 0;
}

static int test_all_truncations(void) {
    uint8_t wire[32] = {0};
    uint32_t length;
    nx_store_le32(wire, 26u);
    wire[4] = P9_TWRITE;
    nx_store_le32(wire + 19u, 3u);
    for (length = 0u; length < 26u; ++length)
        CHECK(check_wire(wire, length) != NX_STATUS_OK);
    CHECK(check_wire(wire, 26u) == NX_STATUS_OK);
    return 0;
}

int main(void) {
    CHECK(test_clunk_and_frame_lengths() == 0);
    CHECK(test_wire_fields() == 0);
    CHECK(test_write_byte_count() == 0);
    CHECK(test_all_truncations() == 0);
    puts("9P real request framing, types and truncation coverage: PASS");
    return 0;
}
