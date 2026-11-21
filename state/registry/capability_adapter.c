/* One NXREG adapter to the actual NXOLMO signed-capability authority API.
 * No independent registry, per-PID permission array or shadow rights table. */
#include "nexs_nxreg_capability_adapter.h"
#include "nexs_checked.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Conservative admission envelope:
 * - keyring verifier loops over all key records (64 work/key bound)
 * - up to 16 work/encoded byte for validation/HMAC input and output
 * - fixed 1024 work for lifecycle, MAC rounds, comparisons and probes
 * - caller-provided REVIEWED bound of the exact authenticated resolver.
 * One full budget reservation occurs BEFORE each token verification.
 * Rule11 final certification remains blocked on a measured proof of these
 * conservative verifier/resolver bounds; never treat 1 fuel as a MAC. */
static nx_status nxreg_signed_grant_cost(
    const nxreg_capability_context *context, uint64_t *cost) {
    uint64_t keys = 0u;
    uint64_t total = 0u;
    if (context == NULL || cost == NULL || context->keyring == NULL ||
        context->resolver_fuel_ceiling == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_u64_mul(context->keyring->key_count, UINT64_C(64), &keys)
        != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    if (nx_u64_add(keys, (uint64_t)NX_CAPABILITY_TOKEN_SIZE * UINT64_C(16),
                   &total) != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    if (nx_u64_add(total, UINT64_C(1024), &total) != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    return nx_u64_add(total, context->resolver_fuel_ceiling, cost);
}

static nx_status nxreg_verify_grant(
    const nxreg_capability_context *context, nx_const_span token,
    nx_object_ref target, uint64_t required_rights) {
    nx_capability_validation validation;
    nx_capability_claims claims;
    nx_capability_slot_state slot;
    nx_status status;
    memset(&validation, 0, sizeof(validation));
    validation.principal = context->principal;
    validation.required_rights = required_rights;
    validation.expected_scope_id = context->scope_id;
    validation.expected_lease_epoch = context->lease_epoch;
    validation.current_tick = context->current_tick;
    validation.slot_capacity = context->slot_capacity;
    validation.expected_policy_version = context->policy_version;
    memset(&claims, 0, sizeof(claims));
    memset(&slot, 0, sizeof(slot));
    status = nx_capability_verify(
        context->keyring, token, &validation, context->resolve,
        context->resolve_context, &claims, &slot);
    if (status == NX_STATUS_OK && !nx_object_ref_equal(slot.object, target))
        status = NX_STATUS_ACCESS_DENIED;
    memset(&claims, 0, sizeof(claims));
    memset(&slot, 0, sizeof(slot));
    return status;
}

nx_status nxreg_capability_authorize(
    void *context, const nxreg_auth_context *caller,
    uint64_t operation_id, nx_object_ref target, uint64_t required_rights,
    nx_operation_budget *budget) {
    const nxreg_capability_context *authority =
        (const nxreg_capability_context *)context;
    uint64_t token_cost = 0u;
    size_t token_bytes = 0u;
    int granted = 0;
    (void)operation_id;
    if (authority == NULL || caller == NULL ||
        caller != authority->authenticated_caller ||
        authority->keyring == NULL || authority->tokens == NULL ||
        authority->token_count == 0u || authority->resolve == NULL ||
        budget == NULL ||
        !nx_object_ref_is_valid(authority->principal) ||
        !nx_object_ref_is_valid(target) || authority->slot_capacity == 0u ||
        authority->scope_id == 0u || authority->lease_epoch == 0u ||
        authority->policy_version == 0u ||
        required_rights == 0u ||
        (required_rights & ~(NXREG_RIGHT_READ | NXREG_RIGHT_ENUMERATE)) != 0u)
        return NX_STATUS_ACCESS_DENIED;
    if (nx_size_mul(authority->token_count, sizeof(*authority->tokens),
                    &token_bytes) != NX_STATUS_OK || token_bytes == 0u)
        return NX_STATUS_OVERFLOW;
    if (nxreg_signed_grant_cost(authority, &token_cost) != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    /* Visit every provided token: a revoked or differently scoped grant
     * cannot widen another, and no unverified token is a new object ID. */
    for (size_t i = 0u; i < authority->token_count; ++i) {
        nx_status status = nx_operation_budget_consume(budget, token_cost);
        if (status != NX_STATUS_OK)
            return status;
        if (nxreg_verify_grant(authority, authority->tokens[i], target,
                               required_rights) == NX_STATUS_OK)
            granted = 1;
    }
    return granted ? NX_STATUS_OK : NX_STATUS_ACCESS_DENIED;
}
