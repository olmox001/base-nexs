/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_p9_frame.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n",              \
                          __FILE__, __LINE__, #expression);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_round_trip(void) {
    const uint8_t body_bytes[] = {0x10u, 0x20u, 0x30u, 0x40u};
    uint8_t wire[32] = {0u};
    nx_const_span body = {body_bytes, sizeof(body_bytes)};
    nx_mut_span output = {wire, sizeof(wire)};
    nx_const_span input;
    nx_9p_frame_view view;
    size_t written = 0u;
    size_t consumed = 0u;

    CHECK(nx_9p_frame_encode(output, UINT8_C(0x71), UINT16_C(0x1234),
                            body, 32u, &written) == NX_STATUS_OK);
    CHECK(written == NX_9P_FRAME_HEADER_SIZE + sizeof(body_bytes));
    CHECK(wire[0] == (uint8_t)written);
    CHECK(wire[1] == 0u && wire[2] == 0u && wire[3] == 0u);
    CHECK(wire[4] == UINT8_C(0x71));
    CHECK(wire[5] == UINT8_C(0x34) && wire[6] == UINT8_C(0x12));

    input.data = wire;
    input.size = written;
    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) == NX_STATUS_OK);
    CHECK(consumed == written);
    CHECK(view.size == written);
    CHECK(view.type == UINT8_C(0x71));
    CHECK(view.tag == UINT16_C(0x1234));
    CHECK(view.body_size == sizeof(body_bytes));
    CHECK(view.body == wire + NX_9P_FRAME_HEADER_SIZE);
    CHECK(memcmp(view.body, body_bytes, sizeof(body_bytes)) == 0);
    return 0;
}

static int test_error_outputs_are_cleared(void) {
    uint8_t short_input[6] = {0u};
    nx_const_span input = {short_input, sizeof(short_input)};
    nx_9p_frame_view view;
    size_t consumed = 99u;

    memset(&view, 0xa5, sizeof(view));
    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) ==
          NX_STATUS_TRUNCATED);
    CHECK(view.size == 0u);
    CHECK(view.type == 0u);
    CHECK(view.tag == 0u);
    CHECK(view.body == NULL);
    CHECK(view.body_size == 0u);
    CHECK(consumed == 0u);
    return 0;
}

static int test_decode_failures(void) {
    uint8_t wire[NX_9P_FRAME_HEADER_SIZE] = {0u};
    nx_const_span input = {wire, sizeof(wire)};
    nx_9p_frame_view view;
    size_t consumed = 55u;

    wire[0] = 6u;
    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) ==
          NX_STATUS_MALFORMED);
    CHECK(consumed == 0u && view.size == 0u);

    wire[0] = 9u;
    CHECK(nx_9p_frame_decode(input, 8u, &view, &consumed) ==
          NX_STATUS_LIMIT_EXCEEDED);
    CHECK(consumed == 0u && view.size == 0u);

    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) ==
          NX_STATUS_TRUNCATED);
    CHECK(consumed == 0u && view.size == 0u);

    CHECK(nx_9p_frame_decode(input, 6u, &view, &consumed) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(consumed == 0u && view.size == 0u);

    input.data = NULL;
    input.size = 1u;
    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(consumed == 0u && view.size == 0u);

    input.data = NULL;
    input.size = 0u;
    CHECK(nx_9p_frame_decode(input, 32u, &view, &consumed) ==
          NX_STATUS_TRUNCATED);
    CHECK(consumed == 0u && view.size == 0u);

    CHECK(nx_9p_frame_decode(input, 32u, NULL, &consumed) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(consumed == 0u);
    CHECK(nx_9p_frame_decode(input, 32u, &view, NULL) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(view.size == 0u);
    return 0;
}

static int test_encode_failures(void) {
    uint8_t output_bytes[16] = {0u};
    uint8_t one_byte = 0x5au;
    nx_mut_span output = {output_bytes, sizeof(output_bytes)};
    nx_const_span body = {&one_byte, 1u};
    size_t written = 77u;

    output.size = 6u;
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 32u, &written) ==
          NX_STATUS_NO_SPACE);
    CHECK(written == 0u);

    output.size = sizeof(output_bytes);
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 7u, &written) ==
          NX_STATUS_LIMIT_EXCEEDED);
    CHECK(written == 0u);

    body.data = NULL;
    body.size = 1u;
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 32u, &written) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0u);

    body.data = &one_byte;
    body.size = (size_t)UINT32_MAX;
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, UINT32_MAX, &written) ==
          NX_STATUS_OVERFLOW);
    CHECK(written == 0u);

    body.data = NULL;
    body.size = 0u;
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 6u, &written) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0u);
    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 32u, NULL) ==
          NX_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_overlapping_encode(void) {
    uint8_t buffer[32] = {
        0x00u, 0xa1u, 0xb2u, 0xc3u, 0xd4u, 0xe5u, 0xf6u
    };
    nx_mut_span output = {buffer, sizeof(buffer)};
    nx_const_span body = {buffer + 1u, 3u};
    size_t written = 0u;

    CHECK(nx_9p_frame_encode(output, UINT8_C(0x55), UINT16_C(0x7788),
                             body, 32u, &written) == NX_STATUS_OK);
    CHECK(written == 10u);
    CHECK(buffer[4] == UINT8_C(0x55));
    CHECK(buffer[5] == UINT8_C(0x88) && buffer[6] == UINT8_C(0x77));
    CHECK(buffer[7] == 0xa1u && buffer[8] == 0xb2u && buffer[9] == 0xc3u);
    return 0;
}

static int test_empty_body(void) {
    uint8_t wire[8] = {0u};
    nx_mut_span output = {wire, sizeof(wire)};
    nx_const_span body = {NULL, 0u};
    nx_const_span input;
    nx_9p_frame_view view;
    size_t written = 0u;
    size_t consumed = 0u;

    CHECK(nx_9p_frame_encode(output, 1u, 2u, body, 8u, &written) ==
          NX_STATUS_OK);
    CHECK(written == NX_9P_FRAME_HEADER_SIZE);
    input.data = wire;
    input.size = written;
    CHECK(nx_9p_frame_decode(input, 8u, &view, &consumed) == NX_STATUS_OK);
    CHECK(consumed == written && view.body_size == 0u);
    CHECK(view.body == wire + NX_9P_FRAME_HEADER_SIZE);
    return 0;
}

int main(void) {
    CHECK(test_round_trip() == 0);
    CHECK(test_error_outputs_are_cleared() == 0);
    CHECK(test_decode_failures() == 0);
    CHECK(test_encode_failures() == 0);
    CHECK(test_overlapping_encode() == 0);
    CHECK(test_empty_body() == 0);
    (void)puts("9P frame unit tests: PASS");
    return 0;
}
