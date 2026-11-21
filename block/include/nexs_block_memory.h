/* Product-adapted pinned NXOLMO block ABI; original upstream source: memory.h.
 * Caller owns buffers and queues. This is not an NXEC hardware grant. */
#ifndef NEXS_BLOCK_MEMORY_H
#define NEXS_BLOCK_MEMORY_H
#include "nexs_checked.h"
#include <stdint.h>
#define NX_BLOCK_MEMORY_ABI_VERSION UINT32_C(1)
typedef enum nx_block_operation {
  NX_BLOCK_OP_READ = 1,
  NX_BLOCK_OP_WRITE = 2,
  NX_BLOCK_OP_FLUSH = 3
} nx_block_operation;
typedef struct nx_block_request {
  uint64_t request_id;
  uint64_t first_block;
  uint32_t block_count;
  uint32_t operation;
  void *buffer;
  uint32_t reserved;
} nx_block_request;
typedef struct nx_block_completion {
  uint64_t request_id;
  nx_status status;
  uint32_t operation;
  uint64_t completed_bytes;
} nx_block_completion;
/* Single-owner hosted backend. Caller-owned I/O buffers remain valid until
 * completion is polled. */
typedef struct nx_block_memory {
  uint8_t *volatile_bytes;
  uint8_t *durable_bytes;
  uint64_t block_count;
  uint64_t byte_count;
  uint64_t next_request_id;
  nx_block_request *requests;
  uint32_t request_capacity;
  uint32_t request_head;
  uint32_t request_count;
  nx_block_completion *completions;
  uint32_t completion_capacity;
  uint32_t completion_head;
  uint32_t completion_count;
  uint32_t fail_next_count;
  nx_status fail_status;
  uint32_t initialized;
} nx_block_memory;
nx_status nx_block_memory_init(nx_block_memory *, uint8_t *, uint8_t *,
                               uint64_t, uint32_t, nx_block_request *, uint32_t,
                               nx_block_completion *, uint32_t);
nx_status nx_block_memory_submit(nx_block_memory *, nx_block_operation,
                                 uint64_t, uint32_t, void *, uint64_t *);
nx_status nx_block_memory_pump(nx_block_memory *, uint32_t, uint32_t *);
nx_status nx_block_memory_poll(nx_block_memory *, nx_block_completion *);
nx_status nx_block_memory_fail_next(nx_block_memory *, uint32_t, nx_status);
nx_status nx_block_memory_crash(nx_block_memory *);
nx_status nx_block_memory_validate(const nx_block_memory *);
#endif
