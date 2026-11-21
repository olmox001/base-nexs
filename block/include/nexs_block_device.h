/* Product-adapted pinned NXOLMO block ABI; original upstream source: device.h.
 * Caller owns buffers and queues. This is not an NXEC hardware grant. */
#ifndef NEXS_BLOCK_DEVICE_H
#define NEXS_BLOCK_DEVICE_H

#include "nexs_checked.h"

#include <stdint.h>


#define NX_BLOCK_DEVICE_ABI_VERSION UINT32_C(1)

typedef struct nx_block_device_ops {
    nx_status (*read)(void *context, uint64_t first_block,
                      uint32_t block_count, void *destination);
    nx_status (*write)(void *context, uint64_t first_block,
                       uint32_t block_count, const void *source);
    nx_status (*flush)(void *context);
} nx_block_device_ops;

typedef struct nx_block_device {
    const nx_block_device_ops *ops;
    void *context;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_transfer_blocks;
    uint32_t buffer_alignment;
    uint32_t abi_version;
    uint32_t reserved;
} nx_block_device;

nx_status nx_block_device_validate(const nx_block_device *device);
nx_status nx_block_device_read(nx_block_device *device, uint64_t first_block,
                               uint32_t block_count, void *destination);
nx_status nx_block_device_write(nx_block_device *device, uint64_t first_block,
                                uint32_t block_count, const void *source);
nx_status nx_block_device_flush(nx_block_device *device);


#endif
