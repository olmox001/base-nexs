/* Product port of pinned NXOLMO external/references/nxolmo/include/nxolmo/buffer/capability_adapter.h; single NX ABI. */
#ifndef NEXS_BUFFER_CAPABILITY_ADAPTER_H
#define NEXS_BUFFER_CAPABILITY_ADAPTER_H

#include "nexs_capability.h"
#include "nexs_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_buffer_capability_context {
    const nx_capability_keyring *keyring;
    nx_const_span token;
    nx_capability_resolve_fn resolve;
    void *resolve_context;
    uint64_t current_tick;
    uint64_t slot_capacity;
    uint64_t scope_id;
    uint32_t policy_version;
} nx_buffer_capability_context;

/* Use this callback in nx_buffer_authority to authenticate every buffer
 * operation through the position-bearing capability token ABI. */
nx_status nx_buffer_capability_authorize(
    void *context, nx_object_ref principal, nx_object_ref target,
    uint64_t requested_rights, uint64_t authority_epoch);

#ifdef __cplusplus
}
#endif

#endif
