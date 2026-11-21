/* Product port of pinned NXOLMO external/references/nxolmo/src/buffer/capability_adapter.c; no reference linkage. */
#include "nexs_buffer_capability_adapter.h"

nx_status nx_buffer_capability_authorize(
    void *context, nx_object_ref principal, nx_object_ref target,
    uint64_t requested_rights, uint64_t authority_epoch) {
    nx_buffer_capability_context *adapter =
        (nx_buffer_capability_context *)context;
    nx_capability_validation validation;
    nx_capability_claims claims;
    nx_capability_slot_state slot;
    nx_status status;
    int target_is_zero;

    if (adapter == NULL || adapter->keyring == NULL ||
        adapter->token.data == NULL || adapter->token.size != NX_CAPABILITY_TOKEN_SIZE ||
        adapter->resolve == NULL || !nx_object_ref_is_valid(principal) ||
        requested_rights == 0u || adapter->slot_capacity == 0u)
        return NX_STATUS_ACCESS_DENIED;
    target_is_zero = target.id.high == 0u && target.id.low == 0u &&
                     target.generation == 0u;
    if (!target_is_zero && !nx_object_ref_is_valid(target))
        return NX_STATUS_ACCESS_DENIED;

    validation.principal = principal;
    validation.required_rights = requested_rights;
    validation.expected_scope_id = adapter->scope_id;
    validation.expected_lease_epoch = authority_epoch;
    validation.current_tick = adapter->current_tick;
    validation.slot_capacity = adapter->slot_capacity;
    validation.expected_policy_version = adapter->policy_version;
    status = nx_capability_verify(adapter->keyring, adapter->token,
                                  &validation, adapter->resolve,
                                  adapter->resolve_context, &claims, &slot);
    if (status != NX_STATUS_OK)
        return NX_STATUS_ACCESS_DENIED;
    if (!target_is_zero && !nx_object_ref_equal(slot.object, target))
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}
