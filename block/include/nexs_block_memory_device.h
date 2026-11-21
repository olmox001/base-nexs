/* Product-adapted pinned NXOLMO block ABI; original upstream source: memory_device.h.
 * Caller owns buffers and queues. This is not an NXEC hardware grant. */
#ifndef NEXS_BLOCK_MEMORY_DEVICE_H
#define NEXS_BLOCK_MEMORY_DEVICE_H

#include "nexs_block_device.h"
#include "nexs_block_memory.h"


typedef struct nx_block_memory_device {
    nx_block_memory *backend;
    nx_block_device device;
    uint64_t operation_count;
    uint64_t fail_at_operation;
    nx_status fail_status;
} nx_block_memory_device;

/* Binds the asynchronous hosted memory backend to the synchronous block
 * device contract used by metadata and journal engines. Calls require the
 * backend queues to be empty, preserving request/completion ownership. */
nx_status nx_block_memory_device_init(nx_block_memory_device *adapter,
                                      nx_block_memory *backend);
nx_status nx_block_memory_device_fail_operation(
    nx_block_memory_device *adapter, uint64_t operations_from_now,
    nx_status failure_status);


#endif
