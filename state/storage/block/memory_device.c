/* NXOLMO block source adapted into Base-NEXS product ABI.
 * No implicit locks, threads, pointers across domains or hardware authority.
 * This backend has one owner; caller serialized transitions are mandatory. */
#include "nexs_block_memory_device.h"
#include "nexs_checked.h"

#include <stddef.h>
#include <string.h>

static nx_status nx_block_memory_device_execute(nx_block_memory_device *adapter,
                                                nx_block_operation operation,
                                                uint64_t first_block,
                                                uint32_t block_count,
                                                void *buffer) {
    uint64_t request_id = 0u;
    uint32_t processed = 0u;
    nx_block_completion completion;
    nx_status status;
    if (adapter == NULL || adapter->backend == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_block_memory_validate(adapter->backend);
    if (status != NX_STATUS_OK)
        return status;
    if (adapter->backend->request_count != 0u ||
        adapter->backend->completion_count != 0u)
        return NX_STATUS_BUSY;
    if (adapter->operation_count == UINT64_MAX)
        return NX_STATUS_RESOURCE_EXHAUSTED;
    adapter->operation_count++;
    if (adapter->fail_at_operation != 0u &&
        adapter->operation_count == adapter->fail_at_operation) {
        status = nx_block_memory_fail_next(adapter->backend, 1u,
                                           adapter->fail_status);
        adapter->fail_at_operation = 0u;
        if (status != NX_STATUS_OK)
            return status;
    }
    status = nx_block_memory_submit(adapter->backend, operation, first_block,
                                    block_count, buffer, &request_id);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_block_memory_pump(adapter->backend, 1u, &processed);
    if (status != NX_STATUS_OK || processed != 1u)
        return status == NX_STATUS_OK ? NX_STATUS_IO_ERROR : status;
    status = nx_block_memory_poll(adapter->backend, &completion);
    if (status != NX_STATUS_OK)
        return status;
    if (completion.request_id != request_id ||
        completion.operation != (uint32_t)operation)
        return NX_STATUS_CORRUPT;
    return completion.status;
}

static nx_status nx_block_memory_device_read(void *context,
                                             uint64_t first_block,
                                             uint32_t block_count,
                                             void *destination) {
    return nx_block_memory_device_execute(
        (nx_block_memory_device *)context, NX_BLOCK_OP_READ, first_block,
        block_count, destination);
}

static nx_status nx_block_memory_device_write(void *context,
                                              uint64_t first_block,
                                              uint32_t block_count,
                                              const void *source) {
    return nx_block_memory_device_execute(
        (nx_block_memory_device *)context, NX_BLOCK_OP_WRITE, first_block,
        block_count, (void *)source);
}

static nx_status nx_block_memory_device_flush(void *context) {
    return nx_block_memory_device_execute((nx_block_memory_device *)context,
                                          NX_BLOCK_OP_FLUSH, 0u, 0u, NULL);
}

static const nx_block_device_ops nx_block_memory_device_ops = {
    nx_block_memory_device_read,
    nx_block_memory_device_write,
    nx_block_memory_device_flush
};

nx_status nx_block_memory_device_init(nx_block_memory_device *adapter,
                                      nx_block_memory *backend) {
    nx_status status;
    if (adapter == NULL || backend == NULL ||
        nx_pointer_ranges_overlap(adapter, sizeof(*adapter),
                                  backend, sizeof(*backend)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_block_memory_validate(backend);
    if (status != NX_STATUS_OK)
        return status;
    memset(adapter, 0, sizeof(*adapter));
    adapter->backend = backend;
    adapter->fail_status = NX_STATUS_IO_ERROR;
    adapter->device.ops = &nx_block_memory_device_ops;
    adapter->device.context = adapter;
    adapter->device.block_count = backend->block_count;
    adapter->device.block_size =
        (uint32_t)(backend->byte_count / backend->block_count);
    adapter->device.max_transfer_blocks = backend->block_count > UINT32_MAX
        ? UINT32_MAX : (uint32_t)backend->block_count;
    adapter->device.buffer_alignment = 1u;
    adapter->device.abi_version = NX_BLOCK_DEVICE_ABI_VERSION;
    return nx_block_device_validate(&adapter->device);
}

nx_status nx_block_memory_device_fail_operation(
    nx_block_memory_device *adapter, uint64_t operations_from_now,
    nx_status failure_status) {
    if (adapter == NULL || adapter->backend == NULL ||
        operations_from_now == 0u || failure_status == NX_STATUS_OK ||
        failure_status == NX_STATUS_BUSY ||
        failure_status == NX_STATUS_NO_SPACE ||
        failure_status == NX_STATUS_INVALID_ARGUMENT ||
        adapter->operation_count > UINT64_MAX - operations_from_now)
        return NX_STATUS_INVALID_ARGUMENT;
    adapter->fail_at_operation = adapter->operation_count + operations_from_now;
    adapter->fail_status = failure_status;
    return NX_STATUS_OK;
}
