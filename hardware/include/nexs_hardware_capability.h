/* Product port of pinned NXOLMO external/references/nxolmo/include/nxolmo/hardware/capability.h; single NX ABI. */
#ifndef NEXS_HARDWARE_CAPABILITY_H
#define NEXS_HARDWARE_CAPABILITY_H

#include "nexs_capability.h"
#include "nexs_checked.h"
#include "nexs_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_HARDWARE_RESOURCE_ABI_VERSION UINT32_C(1)
#define NX_HARDWARE_GRANT_ABI_VERSION UINT32_C(1)

typedef enum nx_hardware_resource_type {
    NX_HARDWARE_RESOURCE_PCI_FUNCTION = 1,
    NX_HARDWARE_RESOURCE_MMIO_REGION = 2,
    NX_HARDWARE_RESOURCE_INTERRUPT = 3,
    NX_HARDWARE_RESOURCE_DMA_DOMAIN = 4,
    NX_HARDWARE_RESOURCE_IOMMU_MAPPING = 5,
    NX_HARDWARE_RESOURCE_BLOCK_QUEUE = 6,
    NX_HARDWARE_RESOURCE_DEVICE_ENDPOINT = 7,
    NX_HARDWARE_RESOURCE_FIRMWARE_INTERFACE = 8,
    NX_HARDWARE_RESOURCE_RESET_CONTROL = 9,
    NX_HARDWARE_RESOURCE_POWER_CONTROL = 10
} nx_hardware_resource_type;

#define NX_HARDWARE_RIGHT_DISCOVER       (UINT64_C(1) << 0)
#define NX_HARDWARE_RIGHT_CONFIG_READ    (UINT64_C(1) << 1)
#define NX_HARDWARE_RIGHT_CONFIG_WRITE   (UINT64_C(1) << 2)
#define NX_HARDWARE_RIGHT_MAP             (UINT64_C(1) << 3)
#define NX_HARDWARE_RIGHT_READ            (UINT64_C(1) << 4)
#define NX_HARDWARE_RIGHT_WRITE           (UINT64_C(1) << 5)
#define NX_HARDWARE_RIGHT_IRQ_BIND        (UINT64_C(1) << 6)
#define NX_HARDWARE_RIGHT_IRQ_ACK         (UINT64_C(1) << 7)
#define NX_HARDWARE_RIGHT_DMA_MAP         (UINT64_C(1) << 8)
#define NX_HARDWARE_RIGHT_DMA_UNMAP       (UINT64_C(1) << 9)
#define NX_HARDWARE_RIGHT_DMA_DEVICE_READ (UINT64_C(1) << 10)
#define NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE (UINT64_C(1) << 11)
#define NX_HARDWARE_RIGHT_QUEUE_SUBMIT    (UINT64_C(1) << 12)
#define NX_HARDWARE_RIGHT_QUEUE_CANCEL    (UINT64_C(1) << 13)
#define NX_HARDWARE_RIGHT_QUEUE_DRAIN     (UINT64_C(1) << 14)
#define NX_HARDWARE_RIGHT_ENDPOINT_CALL   (UINT64_C(1) << 15)
#define NX_HARDWARE_RIGHT_FIRMWARE_READ   (UINT64_C(1) << 16)
#define NX_HARDWARE_RIGHT_FIRMWARE_UPDATE (UINT64_C(1) << 17)
#define NX_HARDWARE_RIGHT_RESET           (UINT64_C(1) << 18)
#define NX_HARDWARE_RIGHT_POWER           (UINT64_C(1) << 19)

#define NX_HARDWARE_RESOURCE_EXCLUSIVE       (UINT32_C(1) << 0)
#define NX_HARDWARE_RESOURCE_SHAREABLE       (UINT32_C(1) << 1)
#define NX_HARDWARE_RESOURCE_CACHE_COHERENT  (UINT32_C(1) << 2)
#define NX_HARDWARE_RESOURCE_WRITE_COMBINING (UINT32_C(1) << 3)

#define NX_HARDWARE_DMA_DEVICE_READS  UINT32_C(1)
#define NX_HARDWARE_DMA_DEVICE_WRITES UINT32_C(2)
#define NX_HARDWARE_DMA_BIDIRECTIONAL UINT32_C(3)

typedef struct nx_hardware_resource {
    uint32_t abi_version;
    uint32_t resource_type;
    uint32_t flags;
    uint32_t policy_version;
    nx_object_ref object;
    nx_object_ref parent;
    uint64_t resource_offset;
    uint64_t resource_length;
    uint64_t alignment;
    uint64_t domain_id;
    uint64_t lease_epoch;
    uint64_t rights_ceiling;
    uint64_t max_transfer_bytes;
} nx_hardware_resource;

/*
 * This is an authenticated authority result, not a bearer token. Obtain it
 * only after the kernel authority engine verifies a capability token and
 * resolves the current object generation, principal, rights and lease.
 */
typedef struct nx_hardware_grant {
    uint32_t abi_version;
    uint32_t policy_version;
    nx_object_ref resource;
    nx_object_ref principal;
    uint64_t rights;
    uint64_t scope_id;
    uint64_t lease_epoch;
    uint64_t expiry_tick;
    uint64_t max_transfer_bytes;
} nx_hardware_grant;

nx_status nx_hardware_resource_validate(
    const nx_hardware_resource *resource);
/* Derive a driver grant only from a token verified against the authority
 * resolver and the current kernel policy state. Output is unchanged on error. */
nx_status nx_hardware_grant_from_token(
    const nx_capability_keyring *keyring,
    nx_const_span token,
    const nx_capability_validation *validation,
    nx_capability_resolve_fn resolve,
    void *resolve_context,
    const nx_hardware_resource *resource,
    uint64_t required_rights,
    nx_hardware_grant *grant);
nx_status nx_hardware_grant_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t required_rights,
    uint64_t current_tick);
nx_status nx_hardware_range_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t offset,
    uint64_t length,
    uint64_t required_rights,
    uint64_t current_tick,
    uint64_t *end_offset);
nx_status nx_hardware_dma_validate(
    const nx_hardware_resource *resource,
    const nx_hardware_grant *grant,
    nx_object_ref expected_principal,
    uint64_t expected_scope_id,
    uint64_t offset,
    uint64_t length,
    uint64_t buffer_alignment,
    uint32_t direction,
    uint64_t current_tick);

#ifdef __cplusplus
}
#endif

#endif
