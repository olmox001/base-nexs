/* Adversarial product-backed NXOLMO block model test.
 * Test code may use assertions/macros; production code follows Power12.
 * Every write, flush and read is a separate real product operation.
 */
#include "nexs_block_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr,"BLOCK_MODEL_FAIL line=%d condition=%s\n",__LINE__,#x); \
  return 1; } } while (0)

static uint64_t rng_step(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

int main(void) {
    enum { BLOCKS = 8, BLOCK_SIZE = 512, ITERATIONS = 8192 };
    uint8_t volatile_bytes[BLOCKS * BLOCK_SIZE];
    uint8_t durable_bytes[BLOCKS * BLOCK_SIZE];
    uint8_t expected[BLOCKS * BLOCK_SIZE];
    uint8_t payload[BLOCK_SIZE];
    uint8_t readback[BLOCK_SIZE];
    nx_block_request requests[4];
    nx_block_completion completions[2];
    nx_block_memory memory;
    nx_block_completion event;
    uint64_t seed = UINT64_C(0x98ab11731975a931);
    uint64_t write_id = 0u, flush_id = 0u, read_id = 0u;
    uint32_t processed = 0u;
    CHECK(nx_block_memory_init(&memory, volatile_bytes, durable_bytes,
                               BLOCKS, BLOCK_SIZE, requests, 4u,
                               completions, 2u) == NX_STATUS_OK);
    memset(expected, 0, sizeof(expected));
    CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_NOT_FOUND);
    for (uint32_t iteration = 0u; iteration < ITERATIONS; ++iteration) {
        uint32_t selected = (uint32_t)(rng_step(&seed) % BLOCKS);
        size_t offset = (size_t)selected * BLOCK_SIZE;
        for (size_t i = 0u; i < BLOCK_SIZE; ++i)
            payload[i] = (uint8_t)rng_step(&seed);
        memset(readback, 0xe6, sizeof(readback));
        CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_WRITE, selected,
                                     1u, payload, &write_id) == NX_STATUS_OK);
        CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_FLUSH, 0u, 0u,
                                     NULL, &flush_id) == NX_STATUS_OK);
        CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_READ, selected,
                                     1u, readback, &read_id) == NX_STATUS_OK);
        CHECK(write_id < flush_id && flush_id < read_id);
        /* Full completion ring limits progress; no dropped completion. */
        CHECK(nx_block_memory_pump(&memory, 3u, &processed) == NX_STATUS_OK);
        CHECK(processed == 2u);
        CHECK(nx_block_memory_pump(&memory, 1u, &processed) ==
              NX_STATUS_NO_SPACE && processed == 0u);
        CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_OK &&
              event.request_id == write_id && event.operation == NX_BLOCK_OP_WRITE &&
              event.status == NX_STATUS_OK && event.completed_bytes == BLOCK_SIZE);
        CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_OK &&
              event.request_id == flush_id && event.operation == NX_BLOCK_OP_FLUSH &&
              event.status == NX_STATUS_OK);
        CHECK(nx_block_memory_pump(&memory, 1u, &processed) == NX_STATUS_OK &&
              processed == 1u);
        CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_OK &&
              event.request_id == read_id && event.operation == NX_BLOCK_OP_READ &&
              event.status == NX_STATUS_OK);
        CHECK(memcmp(payload, readback, BLOCK_SIZE) == 0);
        memcpy(expected + offset, payload, BLOCK_SIZE);
        CHECK(memcmp(durable_bytes, expected, sizeof(expected)) == 0);
        CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_NOT_FOUND);
        if (iteration % 31u == 0u) {
            CHECK(nx_block_memory_crash(&memory) == NX_STATUS_OK);
            CHECK(memcmp(volatile_bytes, expected, sizeof(expected)) == 0);
        }
        if (iteration % 97u == 0u) {
            uint64_t invalid_id = UINT64_MAX;
            CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_WRITE,
                                         BLOCKS, 1u, payload, &invalid_id) ==
                  NX_STATUS_INVALID_ARGUMENT);
            CHECK(invalid_id == UINT64_MAX);
            CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_WRITE,
                                         selected, 1u, volatile_bytes,
                                         &invalid_id) ==
                  NX_STATUS_INVALID_ARGUMENT);
            CHECK(nx_block_memory_validate(&memory) == NX_STATUS_OK);
        }
    }
    /* Inject a real denied write; observable completion, no mutation. */
    CHECK(nx_block_memory_fail_next(&memory, 1u, NX_STATUS_IO_ERROR) ==
          NX_STATUS_OK);
    CHECK(nx_block_memory_submit(&memory, NX_BLOCK_OP_WRITE,
                                 0u, 1u, payload, &write_id) == NX_STATUS_OK);
    CHECK(nx_block_memory_pump(&memory, 1u, &processed) == NX_STATUS_OK &&
          processed == 1u);
    CHECK(nx_block_memory_poll(&memory, &event) == NX_STATUS_OK &&
          event.status == NX_STATUS_IO_ERROR);
    CHECK(memcmp(volatile_bytes, expected, sizeof(expected)) == 0);
    CHECK(nx_block_memory_validate(&memory) == NX_STATUS_OK);
    puts("NXOLMO_BLOCK_MODEL_PASS requests=24577 completions=24577 "
         "independent_data_oracle=1 backpressure=8192 crash=265 "
         "fault_injection=1");
    return 0;
}
