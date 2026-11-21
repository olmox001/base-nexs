/* Product port of pinned NXOLMO external/references/nxolmo/include/nxolmo/authority/capability.h; single NX ABI. */
#ifndef NEXS_CAPABILITY_H
#define NEXS_CAPABILITY_H

#include "nexs_checked.h"
#include "nexs_checked.h"
#include "nexs_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_CAPABILITY_TOKEN_VERSION UINT16_C(1)
#define NX_CAPABILITY_TOKEN_SIZE 112u
#define NX_CAPABILITY_TOKEN_AUTH_OFFSET 96u
#define NX_CAPABILITY_TOKEN_AUTH_SIZE 16u
#define NX_CAPABILITY_KEY_SIZE 32u

typedef struct nx_capability_key {
    uint32_t key_id;
    uint32_t active;
    uint64_t not_before_tick;
    uint64_t not_after_tick;
    uint8_t secret[NX_CAPABILITY_KEY_SIZE];
} nx_capability_key;

/* Keys are caller-owned, immutable for the duration of every call, and sorted
 * by strictly increasing key_id. key_count is a representation-width count,
 * not an arbitrary product ceiling. */
typedef struct nx_capability_keyring {
    const nx_capability_key *keys;
    uint32_t key_count;
} nx_capability_keyring;

typedef struct nx_capability_claims {
    uint64_t virtual_slot_index;
    uint64_t slot_generation;
    uint64_t rights_mask;
    uint64_t scope_id;
    uint64_t lease_epoch;
    uint64_t expiry_tick;
    uint64_t nonce;
    nx_object_ref subject;
    uint32_t policy_version;
} nx_capability_claims;

typedef struct nx_capability_slot_state {
    nx_object_ref object;
    nx_object_ref subject;
    uint64_t generation;
    uint64_t scope_id;
    uint64_t lease_epoch;
    uint64_t rights_ceiling;
    uint32_t policy_version;
    uint32_t active;
} nx_capability_slot_state;

typedef struct nx_capability_validation {
    nx_object_ref principal;
    uint64_t required_rights;
    uint64_t expected_scope_id;
    uint64_t expected_lease_epoch;
    uint64_t current_tick;
    uint64_t slot_capacity;
    uint32_t expected_policy_version;
} nx_capability_validation;

typedef nx_status (*nx_capability_resolve_fn)(
    void *context, uint64_t authenticated_slot_index,
    nx_capability_slot_state *state);

nx_status nx_capability_keyring_validate(const nx_capability_keyring *keyring);
nx_status nx_capability_issue(const nx_capability_keyring *keyring,
                              uint32_t key_id,
                              const nx_capability_claims *claims,
                              uint64_t current_tick,
                              uint8_t token[NX_CAPABILITY_TOKEN_SIZE]);
/*
 * On failure, output structures remain unchanged. Input/output ranges must be
 * disjoint; the keyring and its key array must remain immutable during a call.
 */
nx_status nx_capability_verify(const nx_capability_keyring *keyring,
                               nx_const_span token,
                               const nx_capability_validation *validation,
                               nx_capability_resolve_fn resolve,
                               void *resolve_context,
                               nx_capability_claims *claims,
                               nx_capability_slot_state *slot_state);
nx_status nx_capability_attenuate(const nx_capability_claims *parent,
                                  uint64_t reduced_rights,
                                  uint64_t reduced_scope,
                                  uint64_t reduced_expiry,
                                  uint64_t new_nonce,
                                  nx_capability_claims *child);

#ifdef __cplusplus
}
#endif

#endif
