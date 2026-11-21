/* Product port of pinned NXOLMO external/references/nxolmo/src/hardware/capability.c; no reference linkage. */
#include "nexs_hardware_capability.h"

#include "nexs_checked.h"

#include <stdint.h>

#define NX_HARDWARE_ALL_RIGHTS \
    (NX_HARDWARE_RIGHT_DISCOVER | NX_HARDWARE_RIGHT_CONFIG_READ | \
     NX_HARDWARE_RIGHT_CONFIG_WRITE | NX_HARDWARE_RIGHT_MAP | \
     NX_HARDWARE_RIGHT_READ | NX_HARDWARE_RIGHT_WRITE | \
     NX_HARDWARE_RIGHT_IRQ_BIND | NX_HARDWARE_RIGHT_IRQ_ACK | \
     NX_HARDWARE_RIGHT_DMA_MAP | NX_HARDWARE_RIGHT_DMA_UNMAP | \
     NX_HARDWARE_RIGHT_DMA_DEVICE_READ | \
     NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE | \
     NX_HARDWARE_RIGHT_QUEUE_SUBMIT | NX_HARDWARE_RIGHT_QUEUE_CANCEL | \
     NX_HARDWARE_RIGHT_QUEUE_DRAIN | NX_HARDWARE_RIGHT_ENDPOINT_CALL | \
     NX_HARDWARE_RIGHT_FIRMWARE_READ | \
     NX_HARDWARE_RIGHT_FIRMWARE_UPDATE | NX_HARDWARE_RIGHT_RESET | \
     NX_HARDWARE_RIGHT_POWER)

#define NX_HARDWARE_ALL_FLAGS \
    (NX_HARDWARE_RESOURCE_EXCLUSIVE | NX_HARDWARE_RESOURCE_SHAREABLE | \
     NX_HARDWARE_RESOURCE_CACHE_COHERENT | \
     NX_HARDWARE_RESOURCE_WRITE_COMBINING)

static int nx_hardware_is_power_of_two(uint64_t value) {
    return value != 0u && (value & (value - UINT64_C(1))) == 0u;
}

static int nx_hardware_resource_has_range(uint32_t type) {
    return type == NX_HARDWARE_RESOURCE_MMIO_REGION ||
           type == NX_HARDWARE_RESOURCE_INTERRUPT ||
           type == NX_HARDWARE_RESOURCE_DMA_DOMAIN ||
           type == NX_HARDWARE_RESOURCE_IOMMU_MAPPING ||
           type == NX_HARDWARE_RESOURCE_BLOCK_QUEUE;
}

static int nx_hardware_parent_required(uint32_t type) {
    return type == NX_HARDWARE_RESOURCE_MMIO_REGION ||
           type == NX_HARDWARE_RESOURCE_INTERRUPT ||
           type == NX_HARDWARE_RESOURCE_DMA_DOMAIN ||
           type == NX_HARDWARE_RESOURCE_IOMMU_MAPPING ||
           type == NX_HARDWARE_RESOURCE_BLOCK_QUEUE ||
           type == NX_HARDWARE_RESOURCE_DEVICE_ENDPOINT ||
           type == NX_HARDWARE_RESOURCE_FIRMWARE_INTERFACE ||
           type == NX_HARDWARE_RESOURCE_RESET_CONTROL ||
           type == NX_HARDWARE_RESOURCE_POWER_CONTROL;
}

static uint64_t nx_hardware_type_rights(uint32_t type) {
    switch (type) {
    case NX_HARDWARE_RESOURCE_PCI_FUNCTION:
        return NX_HARDWARE_RIGHT_DISCOVER |
               NX_HARDWARE_RIGHT_CONFIG_READ |
               NX_HARDWARE_RIGHT_CONFIG_WRITE |
               NX_HARDWARE_RIGHT_RESET | NX_HARDWARE_RIGHT_POWER;
    case NX_HARDWARE_RESOURCE_MMIO_REGION:
        return NX_HARDWARE_RIGHT_MAP | NX_HARDWARE_RIGHT_READ |
               NX_HARDWARE_RIGHT_WRITE;
    case NX_HARDWARE_RESOURCE_INTERRUPT:
        return NX_HARDWARE_RIGHT_IRQ_BIND | NX_HARDWARE_RIGHT_IRQ_ACK;
    case NX_HARDWARE_RESOURCE_DMA_DOMAIN:
    case NX_HARDWARE_RESOURCE_IOMMU_MAPPING:
        return NX_HARDWARE_RIGHT_DMA_MAP | NX_HARDWARE_RIGHT_DMA_UNMAP |
               NX_HARDWARE_RIGHT_DMA_DEVICE_READ |
               NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE;
    case NX_HARDWARE_RESOURCE_BLOCK_QUEUE:
        return NX_HARDWARE_RIGHT_QUEUE_SUBMIT |
               NX_HARDWARE_RIGHT_QUEUE_CANCEL |
               NX_HARDWARE_RIGHT_QUEUE_DRAIN;
    case NX_HARDWARE_RESOURCE_DEVICE_ENDPOINT:
        return NX_HARDWARE_RIGHT_ENDPOINT_CALL;
    case NX_HARDWARE_RESOURCE_FIRMWARE_INTERFACE:
        return NX_HARDWARE_RIGHT_FIRMWARE_READ |
               NX_HARDWARE_RIGHT_FIRMWARE_UPDATE;
    case NX_HARDWARE_RESOURCE_RESET_CONTROL:
        return NX_HARDWARE_RIGHT_RESET;
    case NX_HARDWARE_RESOURCE_POWER_CONTROL:
        return NX_HARDWARE_RIGHT_POWER;
    default:
        return 0u;
    }
}

static nx_status nx_hardware_resource_validate_identity(
    const nx_hardware_resource *resource) {
    if (resource->abi_version != NX_HARDWARE_RESOURCE_ABI_VERSION ||
        resource->policy_version == 0u ||
        resource->resource_type < NX_HARDWARE_RESOURCE_PCI_FUNCTION ||
        resource->resource_type > NX_HARDWARE_RESOURCE_POWER_CONTROL ||
        resource->flags == 0u ||
        (resource->flags & ~NX_HARDWARE_ALL_FLAGS) != 0u ||
        (((resource->flags & NX_HARDWARE_RESOURCE_EXCLUSIVE) != 0u) ==
         ((resource->flags & NX_HARDWARE_RESOURCE_SHAREABLE) != 0u)) ||
        !nx_object_ref_is_valid(resource->object) ||
        resource->lease_epoch == 0u || resource->rights_ceiling == 0u ||
        (resource->rights_ceiling & ~NX_HARDWARE_ALL_RIGHTS) != 0u)
        return NX_STATUS_MALFORMED;
    if ((resource->rights_ceiling &
         ~nx_hardware_type_rights(resource->resource_type)) != 0u)
        return NX_STATUS_ACCESS_DENIED;
    if (nx_hardware_parent_required(resource->resource_type) != 0 &&
        (!nx_object_ref_is_valid(resource->parent) ||
         nx_object_id_equal(resource->parent.id, resource->object.id) != 0))
        return NX_STATUS_MALFORMED;
    if (nx_hardware_parent_required(resource->resource_type) == 0 &&
        (resource->parent.generation != 0u ||
         !nx_object_id_is_zero(resource->parent.id)))
        return NX_STATUS_MALFORMED;
    return NX_STATUS_OK;
}

static nx_status nx_hardware_resource_validate_range(
    const nx_hardware_resource *resource) {
    uint64_t range_end;
    nx_status status;
    if (nx_hardware_resource_has_range(resource->resource_type) != 0) {
        if (resource->resource_length == 0u ||
            !nx_hardware_is_power_of_two(resource->alignment) ||
            resource->resource_offset % resource->alignment != 0u)
            return NX_STATUS_MALFORMED;
        status = nx_u64_add(resource->resource_offset,
                            resource->resource_length, &range_end);
        if (status != NX_STATUS_OK)
            return NX_STATUS_OVERFLOW;
    } else if (resource->resource_length != 0u ||
               resource->resource_offset != 0u ||
               resource->alignment != 0u) {
        return NX_STATUS_MALFORMED;
    }
    return NX_STATUS_OK;
}

static nx_status nx_hardware_resource_validate_attributes(
    const nx_hardware_resource *resource) {
    uint32_t type = resource->resource_type;
    if ((resource->flags & NX_HARDWARE_RESOURCE_WRITE_COMBINING) != 0u &&
        type != NX_HARDWARE_RESOURCE_MMIO_REGION)
        return NX_STATUS_MALFORMED;
    if ((resource->flags & NX_HARDWARE_RESOURCE_CACHE_COHERENT) != 0u &&
        type != NX_HARDWARE_RESOURCE_DMA_DOMAIN &&
        type != NX_HARDWARE_RESOURCE_IOMMU_MAPPING)
        return NX_STATUS_MALFORMED;
    if ((type == NX_HARDWARE_RESOURCE_DMA_DOMAIN ||
         type == NX_HARDWARE_RESOURCE_IOMMU_MAPPING) &&
        (resource->domain_id == 0u || resource->max_transfer_bytes == 0u ||
         resource->max_transfer_bytes > resource->resource_length))
        return NX_STATUS_MALFORMED;
    if (type != NX_HARDWARE_RESOURCE_DMA_DOMAIN &&
        type != NX_HARDWARE_RESOURCE_IOMMU_MAPPING &&
        (resource->domain_id != 0u || resource->max_transfer_bytes != 0u))
        return NX_STATUS_MALFORMED;
    return NX_STATUS_OK;
}

nx_status nx_hardware_resource_validate(
    const nx_hardware_resource *resource) {
    nx_status status;
    if (resource == NULL ||
        ((uintptr_t)resource % (uintptr_t)_Alignof(nx_hardware_resource)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_hardware_resource_validate_identity(resource);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_hardware_resource_validate_range(resource);
    if (status != NX_STATUS_OK)
        return status;
    return nx_hardware_resource_validate_attributes(resource);
}

static nx_status nx_hardware_grant_validate_output(
    const nx_capability_keyring *keyring,
    nx_const_span token,
    const nx_capability_validation *validation,
    const nx_hardware_resource *resource,
    nx_hardware_grant *grant) {
    size_t key_bytes;
    nx_status status = nx_size_mul((size_t)keyring->key_count,
                                   sizeof(*keyring->keys), &key_bytes);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_pointer_ranges_overlap(grant, sizeof(*grant), resource,
                                  sizeof(*resource)) != 0 ||
        nx_pointer_ranges_overlap(grant, sizeof(*grant), validation,
                                  sizeof(*validation)) != 0 ||
        nx_pointer_ranges_overlap(grant, sizeof(*grant), keyring,
                                  sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(grant, sizeof(*grant), keyring->keys,
                                  key_bytes) != 0 ||
        nx_pointer_ranges_overlap(grant, sizeof(*grant), token.data,
                                  token.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static int nx_hardware_grant_matches(
    const nx_hardware_resource *resource,
    const nx_capability_claims *claims,
    const nx_capability_slot_state *slot) {
    return nx_object_ref_equal(slot->object, resource->object) != 0 &&
           nx_object_ref_equal(slot->subject, claims->subject) != 0 &&
           slot->rights_ceiling == resource->rights_ceiling &&
           slot->lease_epoch == resource->lease_epoch &&
           slot->scope_id == claims->scope_id &&
           slot->policy_version == resource->policy_version &&
           claims->policy_version == resource->policy_version &&
           (claims->rights_mask & ~resource->rights_ceiling) == 0u;
}

static void nx_hardware_grant_initialize(
    const nx_hardware_resource *resource,
    const nx_capability_claims *claims,
    nx_hardware_grant *grant) {
    grant->abi_version = NX_HARDWARE_GRANT_ABI_VERSION;
    grant->policy_version = claims->policy_version;
    grant->resource = resource->object;
    grant->principal = claims->subject;
    grant->rights = claims->rights_mask;
    grant->scope_id = claims->scope_id;
    grant->lease_epoch = claims->lease_epoch;
    grant->expiry_tick = claims->expiry_tick;
    grant->max_transfer_bytes = resource->max_transfer_bytes;
}

nx_status nx_hardware_grant_from_token(
    const nx_capability_keyring *keyring,
    nx_const_span token,
    const nx_capability_validation *validation,
    nx_capability_resolve_fn resolve,
    void *resolve_context,
    const nx_hardware_resource *resource,
    uint64_t required_rights,
    nx_hardware_grant *grant) {
    nx_capability_validation current_validation;
    nx_capability_claims claims;
    nx_capability_slot_state slot;
    nx_hardware_grant result;
    nx_status status;
    if (validation == NULL || resolve == NULL || grant == NULL ||
        required_rights == 0u ||
        (required_rights & ~NX_HARDWARE_ALL_RIGHTS) != 0u ||
        ((uintptr_t)validation %
         (uintptr_t)_Alignof(nx_capability_validation)) != 0u ||
        ((uintptr_t)grant % (uintptr_t)_Alignof(nx_hardware_grant)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_hardware_resource_validate(resource);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_capability_keyring_validate(keyring);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_const_span_validate(token, NX_CAPABILITY_TOKEN_SIZE);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_hardware_grant_validate_output(
        keyring, token, validation, resource, grant);
    if (status != NX_STATUS_OK)
        return status;
    current_validation = *validation;
    current_validation.required_rights = required_rights;
    status = nx_capability_verify(keyring, token, &current_validation,
                                  resolve, resolve_context, &claims, &slot);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_hardware_grant_matches(resource, &claims, &slot) == 0)
        return NX_STATUS_ACCESS_DENIED;
    nx_hardware_grant_initialize(resource, &claims, &result);
    *grant = result;
    return NX_STATUS_OK;
}

nx_status nx_hardware_grant_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t required_rights,
    uint64_t current_tick) {
    nx_status status = nx_hardware_resource_validate(resource);
    if (status != NX_STATUS_OK)
        return status;
    if (grant == NULL || !nx_object_ref_is_valid(expected_principal) ||
        expected_scope_id == 0u || required_rights == 0u ||
        (required_rights & ~NX_HARDWARE_ALL_RIGHTS) != 0u ||
        ((uintptr_t)grant % (uintptr_t)_Alignof(nx_hardware_grant)) != 0u ||
        nx_pointer_ranges_overlap(resource, sizeof(*resource), grant,
                                  sizeof(*grant)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (grant->abi_version != NX_HARDWARE_GRANT_ABI_VERSION ||
        grant->policy_version == 0u ||
        !nx_object_ref_is_valid(grant->resource) ||
        !nx_object_ref_is_valid(grant->principal) ||
        grant->rights == 0u || grant->scope_id == 0u ||
        grant->lease_epoch == 0u || grant->expiry_tick == 0u ||
        (grant->rights & ~NX_HARDWARE_ALL_RIGHTS) != 0u)
        return NX_STATUS_MALFORMED;
    if (!nx_object_ref_equal(grant->resource, resource->object) ||
        !nx_object_ref_equal(grant->principal, expected_principal) ||
        grant->scope_id != expected_scope_id ||
        grant->policy_version != resource->policy_version ||
        grant->lease_epoch != resource->lease_epoch ||
        (grant->rights & ~resource->rights_ceiling) != 0u ||
        (required_rights & ~grant->rights) != 0u ||
        grant->expiry_tick <= current_tick)
        return NX_STATUS_ACCESS_DENIED;
    if (grant->max_transfer_bytes > resource->max_transfer_bytes ||
        ((resource->resource_type == NX_HARDWARE_RESOURCE_DMA_DOMAIN ||
          resource->resource_type == NX_HARDWARE_RESOURCE_IOMMU_MAPPING) &&
         grant->max_transfer_bytes == 0u))
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

nx_status nx_hardware_range_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t offset,
    uint64_t length,
    uint64_t required_rights,
    uint64_t current_tick,
    uint64_t *end_offset) {
    uint64_t end;
    nx_status status;
    if (end_offset == NULL ||
        ((uintptr_t)end_offset % (uintptr_t)_Alignof(uint64_t)) != 0u ||
        nx_pointer_ranges_overlap(end_offset, sizeof(*end_offset), resource,
                                  sizeof(*resource)) != 0 ||
        nx_pointer_ranges_overlap(end_offset, sizeof(*end_offset), grant,
                                  sizeof(*grant)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_hardware_grant_validate(
        resource, grant, expected_principal, expected_scope_id,
        required_rights, current_tick);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_hardware_resource_has_range(resource->resource_type) == 0)
        return NX_STATUS_NOT_SUPPORTED;
    status = nx_range_validate(offset, length, resource->resource_length,
                               &end);
    if (status != NX_STATUS_OK)
        return status;
    *end_offset = end;
    return NX_STATUS_OK;
}

nx_status nx_hardware_dma_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t offset,
    uint64_t length,
    uint64_t buffer_alignment,
    uint32_t direction,
    uint64_t current_tick) {
    uint64_t required_rights = NX_HARDWARE_RIGHT_DMA_MAP;
    uint64_t end_offset;
    nx_status status;

    if (resource == NULL || grant == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_hardware_resource_validate(resource);
    if (status != NX_STATUS_OK)
        return status;
    if ((resource->resource_type != NX_HARDWARE_RESOURCE_DMA_DOMAIN &&
         resource->resource_type != NX_HARDWARE_RESOURCE_IOMMU_MAPPING) ||
        length == 0u || !nx_hardware_is_power_of_two(buffer_alignment) ||
        offset % buffer_alignment != 0u ||
        (direction != NX_HARDWARE_DMA_DEVICE_READS &&
         direction != NX_HARDWARE_DMA_DEVICE_WRITES &&
         direction != NX_HARDWARE_DMA_BIDIRECTIONAL))
        return NX_STATUS_INVALID_ARGUMENT;

    if ((direction & NX_HARDWARE_DMA_DEVICE_READS) != 0u)
        required_rights |= NX_HARDWARE_RIGHT_DMA_DEVICE_READ;
    if ((direction & NX_HARDWARE_DMA_DEVICE_WRITES) != 0u)
        required_rights |= NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE;
    status = nx_hardware_range_validate(
        resource, grant, expected_principal, expected_scope_id,
        offset, length, required_rights, current_tick, &end_offset);
    if (status != NX_STATUS_OK)
        return status;
    if (length > grant->max_transfer_bytes)
        return NX_STATUS_LIMIT_EXCEEDED;
    if (buffer_alignment < resource->alignment)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}
