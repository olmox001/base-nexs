/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_capability.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n",              \
                          __FILE__, __LINE__, #expression);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct resolver_context {
    nx_capability_slot_state state;
    uint64_t expected_index;
    uint64_t calls;
} resolver_context;

static nx_status resolve_slot(void *context, uint64_t index,
                              nx_capability_slot_state *state) {
    resolver_context *resolver = (resolver_context *)context;
    resolver->calls++;
    if (index != resolver->expected_index)
        return NX_STATUS_NOT_FOUND;
    *state = resolver->state;
    return NX_STATUS_OK;
}

static nx_object_ref make_ref(uint64_t high, uint64_t low, uint64_t generation) {
    nx_object_ref ref;
    ref.id.high = high;
    ref.id.low = low;
    ref.generation = generation;
    return ref;
}

static void init_keyring(nx_capability_key *key, nx_capability_keyring *keyring) {
    size_t i;
    memset(key, 0, sizeof(*key));
    key->key_id = 7u;
    key->active = 1u;
    key->not_before_tick = 10u;
    key->not_after_tick = 1000u;
    for (i = 0u; i < sizeof(key->secret); ++i)
        key->secret[i] = (uint8_t)(i * 13u + 5u);
    keyring->keys = key;
    keyring->key_count = 1u;
}

static void init_claims(nx_capability_claims *claims) {
    memset(claims, 0, sizeof(*claims));
    claims->virtual_slot_index = 3u;
    claims->slot_generation = 4u;
    claims->rights_mask = UINT64_C(0x0f);
    claims->scope_id = 55u;
    claims->lease_epoch = 9u;
    claims->expiry_tick = 500u;
    claims->nonce = 123u;
    claims->subject = make_ref(1u, 2u, 3u);
    claims->policy_version = 1u;
}

static void init_validation(nx_capability_validation *validation) {
    memset(validation, 0, sizeof(*validation));
    validation->principal = make_ref(1u, 2u, 3u);
    validation->required_rights = UINT64_C(0x01);
    validation->expected_scope_id = 55u;
    validation->expected_lease_epoch = 9u;
    validation->current_tick = 30u;
    validation->slot_capacity = 8u;
    validation->expected_policy_version = 1u;
}

static void init_resolver(resolver_context *resolver) {
    memset(resolver, 0, sizeof(*resolver));
    resolver->expected_index = 3u;
    resolver->state.object = make_ref(0xabcdu, 0x1234u, 4u);
    resolver->state.subject = make_ref(1u, 2u, 3u);
    resolver->state.generation = 4u;
    resolver->state.scope_id = 55u;
    resolver->state.lease_epoch = 9u;
    resolver->state.rights_ceiling = UINT64_C(0x0f);
    resolver->state.policy_version = 1u;
    resolver->state.active = 1u;
}

static int test_keyring_scales_by_supplied_count(void) {
    nx_capability_key keys[96];
    nx_capability_keyring keyring;
    size_t i, j;
    memset(keys, 0, sizeof(keys));
    for (i = 0u; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        keys[i].key_id = (uint32_t)(i + 1u);
        keys[i].active = 1u;
        keys[i].not_before_tick = 1u;
        keys[i].not_after_tick = 100u;
        for (j = 0u; j < sizeof(keys[i].secret); ++j)
            keys[i].secret[j] = (uint8_t)(i + j + 1u);
    }
    keyring.keys = keys;
    keyring.key_count = (uint32_t)(sizeof(keys) / sizeof(keys[0]));
    CHECK(nx_capability_keyring_validate(&keyring) == NX_STATUS_OK);
    keys[70].key_id = keys[69].key_id;
    CHECK(nx_capability_keyring_validate(&keyring) == NX_STATUS_CONFLICT);
    keys[70].key_id = 71u;
    keys[71].key_id = 70u;
    CHECK(nx_capability_keyring_validate(&keyring) == NX_STATUS_CONFLICT);
    return 0;
}

static int test_issue_verify_and_authenticated_position(void) {
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims input;
    nx_capability_claims output;
    nx_capability_slot_state resolved;
    nx_capability_validation validation;
    resolver_context resolver;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    uint8_t tampered[NX_CAPABILITY_TOKEN_SIZE];

    init_keyring(&key, &keyring);
    init_claims(&input);
    init_validation(&validation);
    init_resolver(&resolver);
    CHECK(nx_capability_keyring_validate(&keyring) == NX_STATUS_OK);
    CHECK(nx_capability_issue(&keyring, 7u, &input, 20u, token) ==
          NX_STATUS_OK);
    CHECK(token[0] == 1u && token[1] == 0u);
    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){token, sizeof(token)}, &validation,
          resolve_slot, &resolver, &output, &resolved) == NX_STATUS_OK);
    CHECK(output.virtual_slot_index == 3u);
    CHECK(output.slot_generation == 4u);
    CHECK(nx_object_ref_equal(output.subject, validation.principal) != 0);
    CHECK(resolved.generation == 4u && resolver.calls == 1u);

    memcpy(tampered, token, sizeof(tampered));
    tampered[8] ^= UINT8_C(0x01); /* Alter the position before authentication. */
    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){tampered, sizeof(tampered)}, &validation,
          resolve_slot, &resolver, &output, &resolved) == NX_STATUS_ACCESS_DENIED);
    CHECK(resolver.calls == 1u); /* Unauthenticated position was never resolved. */

    memcpy(tampered, token, sizeof(tampered));
    tampered[NX_CAPABILITY_TOKEN_AUTH_OFFSET] ^= UINT8_C(0x80);
    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){tampered, sizeof(tampered)}, &validation,
          resolve_slot, &resolver, &output, &resolved) == NX_STATUS_ACCESS_DENIED);
    CHECK(resolver.calls == 1u);

    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){token, sizeof(token) - 1u}, &validation,
          resolve_slot, &resolver, &output, &resolved) == NX_STATUS_MALFORMED);
    memcpy(tampered, token, sizeof(tampered));
    tampered[92] = 1u;
    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){tampered, sizeof(tampered)}, &validation,
          resolve_slot, &resolver, &output, &resolved) == NX_STATUS_MALFORMED);
    return 0;
}

static int test_policy_generation_and_key_failures(void) {
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims input;
    nx_capability_claims output;
    nx_capability_slot_state resolved;
    nx_capability_validation validation;
    resolver_context resolver;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];

    init_keyring(&key, &keyring);
    init_claims(&input);
    init_validation(&validation);
    init_resolver(&resolver);
    CHECK(nx_capability_issue(&keyring, 7u, &input, 20u, token) == NX_STATUS_OK);

    validation.principal = make_ref(9u, 9u, 1u);
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.principal = input.subject;
    validation.required_rights = UINT64_C(0x10);
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.required_rights = 1u;
    validation.expected_scope_id = 99u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.expected_scope_id = 55u;
    validation.expected_lease_epoch = 10u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.expected_lease_epoch = 9u;
    validation.expected_policy_version = 2u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.expected_policy_version = 1u;
    validation.current_tick = 500u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.current_tick = 30u;

    resolver.state.generation++;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    resolver.state.generation--;
    resolver.state.rights_ceiling = 1u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    resolver.state.rights_ceiling = 0x0fu;
    resolver.state.active = 0u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output, &resolved) ==
          NX_STATUS_ACCESS_DENIED);

    CHECK(nx_capability_issue(&keyring, 99u, &input, 20u, token) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_capability_issue(&keyring, 7u, &input, 1000u, token) ==
          NX_STATUS_ACCESS_DENIED);
    return 0;
}

static int test_slot_bounds_and_attenuation(void) {
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims parent;
    nx_capability_claims child;
    nx_capability_claims decoded;
    nx_capability_slot_state resolved;
    nx_capability_validation validation;
    resolver_context resolver;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];

    init_keyring(&key, &keyring);
    init_claims(&parent);
    init_validation(&validation);
    init_resolver(&resolver);
    parent.virtual_slot_index = 99u;
    CHECK(nx_capability_issue(&keyring, 7u, &parent, 20u, token) == NX_STATUS_OK);
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &decoded, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(resolver.calls == 0u);

    init_claims(&parent);
    CHECK(nx_capability_attenuate(&parent, 1u, 55u, 400u, 124u, &child) ==
          NX_STATUS_OK);
    CHECK(child.rights_mask == 1u && child.expiry_tick == 400u &&
          child.nonce == 124u);
    CHECK(nx_capability_attenuate(&parent, 0x10u, 55u, 400u, 125u, &child) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_capability_attenuate(&parent, 1u, 56u, 400u, 125u, &child) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_capability_attenuate(&parent, 1u, 55u, 600u, 125u, &child) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_capability_issue(&keyring, 7u, &child, 20u, token) == NX_STATUS_OK);
    validation.required_rights = 2u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &decoded, &resolved) ==
          NX_STATUS_ACCESS_DENIED);
    validation.required_rights = 1u;
    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &decoded, &resolved) == NX_STATUS_OK);
    return 0;
}

static int test_aliasing_and_key_material_hygiene(void) {
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims claims;
    nx_capability_slot_state resolved;
    nx_capability_validation validation;
    resolver_context resolver;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    uint8_t token_copy[NX_CAPABILITY_TOKEN_SIZE];
    union issue_alias_storage {
        nx_capability_claims claims;
        uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    } issue_alias;
    union key_alias_storage {
        nx_capability_key key;
        uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    } key_alias;
    union verify_alias_storage {
        nx_capability_claims claims;
        uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    } verify_alias;
    union output_alias_storage {
        nx_capability_claims claims;
        nx_capability_slot_state slot;
    } output_alias;

    init_keyring(&key, &keyring);
    init_claims(&claims);
    init_validation(&validation);
    init_resolver(&resolver);
    memset(token, 0xa5, sizeof(token));
    CHECK(nx_capability_issue(&keyring, 7u, &claims, 20u, token) == NX_STATUS_OK);

    /* Output must never overwrite claims or key material supplied as input. */
    memset(&issue_alias, 0, sizeof(issue_alias));
    init_claims(&issue_alias.claims);
    claims = issue_alias.claims;
    CHECK(nx_capability_issue(&keyring, 7u, &issue_alias.claims, 20u,
                              issue_alias.token) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&issue_alias.claims, &claims, sizeof(claims)) == 0);

    memset(&key_alias, 0, sizeof(key_alias));
    init_keyring(&key_alias.key, &keyring);
    CHECK(nx_capability_issue(&keyring, 7u, &claims, 20u,
                              key_alias.token) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(key_alias.key.key_id == 7u && key_alias.key.active == 1u);

    /* An active all-zero MAC key is a provisioning error, never a valid key. */
    init_keyring(&key, &keyring);
    memset(key.secret, 0, sizeof(key.secret));
    CHECK(nx_capability_keyring_validate(&keyring) == NX_STATUS_MALFORMED);
    init_keyring(&key, &keyring);

    /* Verify rejects aliased outputs without destroying the input bearer token. */
    memcpy(token_copy, token, sizeof(token));
    memset(&verify_alias, 0, sizeof(verify_alias));
    memcpy(verify_alias.token, token_copy, sizeof(token_copy));
    CHECK(nx_capability_verify(&keyring,
          (nx_const_span){verify_alias.token, sizeof(verify_alias.token)},
          &validation, resolve_slot, &resolver, &verify_alias.claims, &resolved) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(verify_alias.token, token_copy, sizeof(token_copy)) == 0);

    CHECK(nx_capability_verify(&keyring, (nx_const_span){token, sizeof(token)},
          &validation, resolve_slot, &resolver, &output_alias.claims,
          &output_alias.slot) == NX_STATUS_INVALID_ARGUMENT);

    /* A rejected issue leaves a disjoint output token scrubbed. */
    memset(token, 0xa5, sizeof(token));
    claims.rights_mask = 0u;
    CHECK(nx_capability_issue(&keyring, 7u, &claims, 20u, token) ==
          NX_STATUS_INVALID_ARGUMENT);
    {
        size_t i;
        for (i = 0u; i < sizeof(token); ++i)
            CHECK(token[i] == 0u);
    }
    return 0;
}

int main(void) {
    CHECK(test_keyring_scales_by_supplied_count() == 0);
    CHECK(test_issue_verify_and_authenticated_position() == 0);
    CHECK(test_policy_generation_and_key_failures() == 0);
    CHECK(test_slot_bounds_and_attenuation() == 0);
    CHECK(test_aliasing_and_key_material_hygiene() == 0);
    (void)puts("capability token/authority unit tests: PASS");
    return 0;
}
