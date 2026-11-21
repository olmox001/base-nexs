/* Original pinned NXOLMO block conformance suite; real product linked. */
#include "nexs_block_memory.h"
#include "nexs_block_memory_device.h"
#include "nexs_block_device.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);       \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static int test_order_flush_crash(void) {
  _Alignas(uint64_t) uint8_t v[4096];
  uint8_t p[4096], w[1024], r[1024];
  nx_block_request rq[4];
  nx_block_completion cq[4], c;
  nx_block_memory d;
  uint64_t wi = 0u, fi = 0u, ri = 0u;
  uint32_t n = 0u;
  memset(w, 0x5a, sizeof(w));
  memset(r, 0, sizeof(r));
  CHECK(nx_block_memory_init(&d, v, p, 8u, 512u, rq, 4u, cq, 4u) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 0u, 1u, v, &wi) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_FLUSH, 0u, 0u, NULL,
                               (uint64_t *)(void *)v) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_block_memory_poll(&d, (nx_block_completion *)(void *)v) ==
        NX_STATUS_INVALID_ARGUMENT);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 1u, 2u, w, &wi) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_FLUSH, 0u, 0u, NULL, &fi) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_READ, 1u, 2u, r, &ri) ==
        NX_STATUS_OK);
  CHECK(wi < fi && fi < ri);
  CHECK(nx_block_memory_pump(&d, 3u, &n) == NX_STATUS_OK && n == 3u);
  CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_OK && c.request_id == wi &&
        c.completed_bytes == sizeof(w));
  CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_OK && c.request_id == fi &&
        c.completed_bytes == sizeof(v));
  CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_OK && c.request_id == ri &&
        memcmp(r, w, sizeof(r)) == 0);
  memset(v, 0, sizeof(v));
  CHECK(nx_block_memory_crash(&d) == NX_STATUS_OK);
  CHECK(memcmp(v + 512u, w, sizeof(w)) == 0);
  CHECK(nx_block_memory_validate(&d) == NX_STATUS_OK);
  return 0;
}
static int test_fault_backpressure_discard(void) {
  uint8_t v[2048], p[2048], w[512];
  nx_block_request rq[2];
  nx_block_completion cq[1], c;
  nx_block_memory d;
  uint64_t id = 0u;
  uint32_t n = 0u;
  memset(w, 0xa5, sizeof(w));
  CHECK(nx_block_memory_init(&d, v, p, 4u, 512u, rq, 2u, cq, 1u) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_fail_next(&d, 1u, NX_STATUS_IO_ERROR) == NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 0u, 1u, w, &id) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_pump(&d, 1u, &n) == NX_STATUS_OK && n == 1u);
  CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_OK &&
        c.status == NX_STATUS_IO_ERROR);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 0u, 1u, w, &id) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 1u, 1u, w, &id) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_pump(&d, 2u, &n) == NX_STATUS_OK && n == 1u);
  CHECK(nx_block_memory_pump(&d, 1u, &n) == NX_STATUS_NO_SPACE && n == 0u);
  CHECK(nx_block_memory_crash(&d) == NX_STATUS_OK);
  CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_NOT_FOUND && v[0] == 0u);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_READ, 0u, 1u, NULL, &id) ==
        NX_STATUS_INVALID_ARGUMENT);
  return 0;
}
static int test_queue_wraparound_and_corruption(void) {
  uint8_t v[1024], p[1024], w[512];
  nx_block_request rq[3];
  nx_block_completion cq[3], c;
  nx_block_memory d;
  uint64_t id = 0u;
  uint32_t processed = 0u, i = 0u;
  memset(w, 0x3c, sizeof(w));
  CHECK(nx_block_memory_init(&d, v, p, 2u, 512u, rq, 3u, cq, 3u) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_READ, 2u, 1u, w, &id) ==
        NX_STATUS_INVALID_ARGUMENT);
  for (i = 0u; i < 9u; ++i) {
    CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_FLUSH, 0u, 0u, NULL, &id) ==
          NX_STATUS_OK);
    CHECK(nx_block_memory_pump(&d, 1u, &processed) == NX_STATUS_OK &&
          processed == 1u);
    CHECK(nx_block_memory_poll(&d, &c) == NX_STATUS_OK && c.request_id == id);
  }
  CHECK(nx_block_memory_submit(&d, NX_BLOCK_OP_WRITE, 0u, 1u, w, &id) ==
        NX_STATUS_OK);
  rq[d.request_head].reserved = 1u;
  CHECK(nx_block_memory_validate(&d) == NX_STATUS_CORRUPT);
  rq[d.request_head].reserved = 0u;
  CHECK(nx_block_memory_validate(&d) == NX_STATUS_OK);
  CHECK(nx_block_memory_crash(&d) == NX_STATUS_OK);
  CHECK(nx_block_memory_validate(&d) == NX_STATUS_OK);
  return 0;
}

static int test_block_device_validation_contract(void) {
  uint8_t volatile_bytes[4096], durable_bytes[4096];
  nx_block_request requests[2];
  nx_block_completion completions[2];
  nx_block_memory backend;
  nx_block_memory_device adapter;
  nx_block_device invalid;
  CHECK(nx_block_memory_init(&backend, volatile_bytes, durable_bytes, 8u,
                             512u, requests, 2u, completions, 2u) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_device_init(&adapter, &backend) == NX_STATUS_OK);
  CHECK(nx_block_device_validate(&adapter.device) == NX_STATUS_OK);
  invalid = adapter.device;
  invalid.max_transfer_blocks = 0u;
  CHECK(nx_block_device_validate(&invalid) == NX_STATUS_INVALID_ARGUMENT);
  invalid = adapter.device;
  invalid.block_size = 768u;
  CHECK(nx_block_device_validate(&invalid) == NX_STATUS_INVALID_ARGUMENT);
  invalid = adapter.device;
  invalid.reserved = 1u;
  CHECK(nx_block_device_validate(&invalid) == NX_STATUS_INVALID_ARGUMENT);
  return 0;
}

static int test_transfer_exceeds_legacy_chunk_ceiling(void) {
  uint8_t volatile_bytes[256u * 512u];
  uint8_t durable_bytes[256u * 512u];
  uint8_t payload[129u * 512u];
  nx_block_request requests[2];
  nx_block_completion completions[2];
  nx_block_memory backend;
  nx_block_memory_device adapter;
  uint64_t request_id = 0u;
  uint32_t processed = 0u;
  memset(payload, 0x6du, sizeof(payload));
  CHECK(nx_block_memory_init(&backend, volatile_bytes, durable_bytes, 256u,
                             512u, requests, 2u, completions, 2u) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_device_init(&adapter, &backend) == NX_STATUS_OK);
  CHECK(adapter.device.max_transfer_blocks >= 129u);
  CHECK(nx_block_device_write(&adapter.device, 0u, 129u, payload) ==
        NX_STATUS_OK);
  CHECK(nx_block_memory_submit(&backend, NX_BLOCK_OP_WRITE, 127u, 129u,
                               payload, &request_id) == NX_STATUS_OK);
  CHECK(nx_block_memory_pump(&backend, 1u, &processed) == NX_STATUS_OK &&
        processed == 1u);
  return 0;
}

int main(void) {
  CHECK(test_order_flush_crash() == 0);
  CHECK(test_fault_backpressure_discard() == 0);
  CHECK(test_queue_wraparound_and_corruption() == 0);
  CHECK(test_block_device_validation_contract() == 0);
  CHECK(test_transfer_exceeds_legacy_chunk_ceiling() == 0);
  (void)puts("PASS: block memory ordering/flush/crash/fault/backpressure");
  return 0;
}
