/* NXOLMO block source adapted into Base-NEXS product ABI.
 * No implicit locks, threads, pointers across domains or hardware authority.
 * This backend has one owner; caller serialized transitions are mandatory. */
#include "nexs_block_device.h"

#include "nexs_checked.h"

#include <stddef.h>
#include <stdint.h>

#define NX_BLOCK_DEVICE_MIN_BLOCK_SIZE UINT32_C(512)

nx_status nx_block_device_validate(const nx_block_device *device) {
    if (device == NULL || device->ops == NULL || device->context == NULL ||
        device->ops->read == NULL || device->ops->write == NULL ||
        device->ops->flush == NULL || device->block_count == 0u ||
        device->max_transfer_blocks == 0u ||
        device->buffer_alignment == 0u ||
        (device->buffer_alignment & (device->buffer_alignment - 1u)) != 0u ||
        device->abi_version != NX_BLOCK_DEVICE_ABI_VERSION ||
        device->reserved != 0u ||
        device->block_size < NX_BLOCK_DEVICE_MIN_BLOCK_SIZE ||
        (device->block_size & (device->block_size - 1u)) != 0u ||
        device->buffer_alignment > device->block_size ||
        device->block_size % device->buffer_alignment != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_block_device_transfer(nx_block_device *device,
                                          uint64_t first_block,
                                          uint32_t block_count,
                                          void *buffer,
                                          int write_operation) {
    uint64_t end;
    uint64_t bytes;
    nx_status status = nx_block_device_validate(device);
    if (status != NX_STATUS_OK)
        return status;
    if (buffer == NULL || block_count == 0u ||
        block_count > device->max_transfer_blocks ||
        ((uintptr_t)buffer % (uintptr_t)device->buffer_alignment) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_u64_add(first_block, (uint64_t)block_count, &end);
    if (status != NX_STATUS_OK)
        return status;
    if (end > device->block_count)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_u64_mul((uint64_t)block_count,
                        (uint64_t)device->block_size, &bytes);
    if (status != NX_STATUS_OK || bytes > (uint64_t)SIZE_MAX)
        return NX_STATUS_OVERFLOW;
    if (write_operation != 0)
        return device->ops->write(device->context, first_block, block_count,
                                  buffer);
    return device->ops->read(device->context, first_block, block_count,
                             buffer);
}

nx_status nx_block_device_read(nx_block_device *device, uint64_t first_block,
                               uint32_t block_count, void *destination) {
    return nx_block_device_transfer(device, first_block, block_count,
                                    destination, 0);
}

nx_status nx_block_device_write(nx_block_device *device, uint64_t first_block,
                                uint32_t block_count, const void *source) {
    return nx_block_device_transfer(device, first_block, block_count,
                                    (void *)source, 1);
}

nx_status nx_block_device_flush(nx_block_device *device) {
    nx_status status = nx_block_device_validate(device);
    if (status != NX_STATUS_OK)
        return status;
    return device->ops->flush(device->context);
}
