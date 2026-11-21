/* NXREG authenticated object access through the existing NXOLMO capability
 * verifier. The token array is an endpoint-supplied collection of received
 * grants, NOT a secondary registry, capability issuer or rights database. */
#ifndef NEXS_NXREG_CAPABILITY_ADAPTER_H
#define NEXS_NXREG_CAPABILITY_ADAPTER_H

#include "nexs_nxreg.h"
#include "nexs_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nxreg_capability_context {
    /* Trusted endpoint supplies this identity, never an untrusted script. */
    const nxreg_auth_context *authenticated_caller;
    nx_object_ref principal;
    const nx_capability_keyring *keyring;
    const nx_const_span *tokens;
    size_t token_count;
    nx_capability_resolve_fn resolve;
    void *resolve_context;
    uint64_t current_tick;
    uint64_t slot_capacity;
    uint64_t scope_id;
    uint64_t lease_epoch;
    uint32_t policy_version;
    /* Reviewed upper bound of the actual slot resolver work per grant.
     * This cannot be selected by the untrusted request. */
    uint64_t resolver_fuel_ceiling;
} nxreg_capability_context;

/* Wire this as the existing nxreg_authorize_fn when the caller is bound by
 * an authenticated endpoint. Every token is MAC-verified and resolved through
 * the *existing* NXOLMO capability engine before access to a specific object.
 * A valid token for some OTHER object is never sufficient. No path grants
 * authority. This is H1 software-token admission, not a complete NXEC
 * hardware/namespace issuer or revocation fence. */
nx_status nxreg_capability_authorize(
    void *context, const nxreg_auth_context *caller,
    uint64_t operation_id, nx_object_ref target, uint64_t required_rights,
    nx_operation_budget *budget);

#ifdef __cplusplus
}
#endif
#endif
