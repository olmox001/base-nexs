/* Real NXOLMO HMAC issuance/verification against the product StateStore and
 * NXREG dispatcher. Slot resolution models a trusted issuer's CURRENT state;
 * this is not proof of a live NXEC kernel endpoint or hardware isolation. */
#include "nexs_nxreg_capability_adapter.h"
#include "nexs_state_store.h"
#include "nexs_checked.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "NXREG_SIGNED_CHECK failed at %d: %s\n", __LINE__, #x); \
    return 1; } } while (0)

struct nxreg_auth_context {
    uint64_t trusted_marker;
};

typedef struct nxreg_issuer_fixture {
    nx_capability_slot_state slots[4];
    uint64_t active_slots;
} nxreg_issuer_fixture;

static nx_status issuer_resolve_slot(void *context, uint64_t index,
                                    nx_capability_slot_state *state) {
    nxreg_issuer_fixture *issuer = context;
    if (issuer == NULL || state == NULL ||
        index >= issuer->active_slots || index >= 4u)
        return NX_STATUS_NOT_FOUND;
    *state = issuer->slots[index];
    return NX_STATUS_OK;
}

static int grant_issue(const nx_capability_keyring *ring,
                       nxreg_issuer_fixture *issuer, size_t slot,
                       nx_object_ref principal, nx_object_ref object,
                       uint64_t rights, uint8_t output[112]) {
    nx_capability_claims claims;
    memset(&claims, 0, sizeof(claims));
    if (slot >= 4u)
        return 0;
    claims.virtual_slot_index = slot;
    claims.slot_generation = 1u;
    claims.rights_mask = rights;
    claims.scope_id = 70u;
    claims.lease_epoch = 11u;
    claims.expiry_tick = 1000u;
    claims.nonce = 100u + slot;
    claims.subject = principal;
    claims.policy_version = 5u;
    memset(&issuer->slots[slot], 0, sizeof(issuer->slots[slot]));
    issuer->slots[slot].object = object;
    issuer->slots[slot].subject = principal;
    issuer->slots[slot].generation = claims.slot_generation;
    issuer->slots[slot].scope_id = claims.scope_id;
    issuer->slots[slot].lease_epoch = claims.lease_epoch;
    issuer->slots[slot].policy_version = claims.policy_version;
    issuer->slots[slot].rights_ceiling = rights;
    issuer->slots[slot].active = 1u;
    if (issuer->active_slots <= slot)
        issuer->active_slots = slot + 1u;
    return nx_capability_issue(ring, 7u, &claims, 10u, output)
        == NX_STATUS_OK;
}

typedef struct nxreg_signed_fixture {
    nx_state_store store;
    nx_state_slot slots[8];
    uint32_t index[16];
    uint32_t rehash[16];
    nx_object_ref root;
    nx_object_ref hidden;
    nx_object_ref visible;
    nx_object_ref principal;
    nx_capability_key key;
    nx_capability_keyring ring;
    nxreg_issuer_fixture issuer;
    uint8_t root_token[NX_CAPABILITY_TOKEN_SIZE];
    uint8_t visible_token[NX_CAPABILITY_TOKEN_SIZE];
    nx_const_span tokens[2];
    struct nxreg_auth_context caller;
    nxreg_capability_context auth;
    nxreg_api_v1 api;
    nxreg_state_store_adapter adapter;
    nxreg_client client;
} nxreg_signed_fixture;

static nx_state_node_spec node_spec(uint64_t type) {
    nx_state_node_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type_id = type;
    spec.schema_id = 1u;
    spec.schema_version = 1u;
    spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    return spec;
}

static int init_signed_fixture(nxreg_signed_fixture *fixture) {
    nx_result result;
    nx_state_node_spec spec = node_spec(1u);
    const uint64_t keys[2] = {UINT64_C(0xa0194e58524547),
                               UINT64_C(0x5369676e65645231)};
    const nxreg_authorization_fuel_limits fuel = {
        UINT64_C(30000), UINT64_C(30000), UINT64_C(30000),
        UINT64_C(30000), UINT64_C(30000)
    };
    memset(fixture, 0, sizeof(*fixture));
    if (nx_state_store_init(&fixture->store, fixture->slots, 8u,
                            fixture->index, fixture->rehash, 16u, 71u,
                            keys, &result) != NX_STATUS_OK)
        return 0;
    if (nx_state_store_create(&fixture->store, &spec, &fixture->principal,
                              &result) != NX_STATUS_OK)
        return 0;
    spec = node_spec(2u);
    if (nx_state_store_create(&fixture->store, &spec, &fixture->root,
                              &result) != NX_STATUS_OK)
        return 0;
    spec.parent = fixture->root;
    spec.type_id = 3u;
    if (nx_state_store_create(&fixture->store, &spec, &fixture->hidden,
                              &result) != NX_STATUS_OK)
        return 0;
    spec.type_id = 4u;
    if (nx_state_store_create(&fixture->store, &spec, &fixture->visible,
                              &result) != NX_STATUS_OK)
        return 0;
    fixture->key.key_id = 7u;
    fixture->key.active = 1u;
    fixture->key.not_before_tick = 1u;
    fixture->key.not_after_tick = 1100u;
    for (size_t i = 0u; i < sizeof(fixture->key.secret); ++i)
        fixture->key.secret[i] = (uint8_t)(i + 1u);
    fixture->ring.keys = &fixture->key;
    fixture->ring.key_count = 1u;
    if (!grant_issue(&fixture->ring, &fixture->issuer, 0u,
                     fixture->principal, fixture->root,
                     NXREG_RIGHT_READ | NXREG_RIGHT_ENUMERATE,
                     fixture->root_token) ||
        !grant_issue(&fixture->ring, &fixture->issuer, 1u,
                     fixture->principal, fixture->visible,
                     NXREG_RIGHT_READ, fixture->visible_token))
        return 0;
    fixture->tokens[0] = (nx_const_span){
        fixture->root_token, sizeof(fixture->root_token)};
    fixture->tokens[1] = (nx_const_span){
        fixture->visible_token, sizeof(fixture->visible_token)};
    fixture->caller.trusted_marker = UINT64_C(0x4e585245474d4143);
    fixture->auth.authenticated_caller = &fixture->caller;
    fixture->auth.principal = fixture->principal;
    fixture->auth.keyring = &fixture->ring;
    fixture->auth.tokens = fixture->tokens;
    fixture->auth.token_count = 2u;
    fixture->auth.resolve = issuer_resolve_slot;
    fixture->auth.resolve_context = &fixture->issuer;
    fixture->auth.current_tick = 10u;
    fixture->auth.slot_capacity = fixture->issuer.active_slots;
    fixture->auth.scope_id = 70u;
    fixture->auth.lease_epoch = 11u;
    fixture->auth.policy_version = 5u;
    fixture->auth.resolver_fuel_ceiling = 128u;
    if (nxreg_state_store_adapter_init(
            &fixture->api, &fixture->adapter, &fixture->store,
            nxreg_capability_authorize, &fixture->auth, &fuel)
            != NX_STATUS_OK)
        return 0;
    return nxreg_client_bind(&fixture->client, &fixture->api, 1u,
                              NXREG_FEATURE_KNOWN_MASK,
                              NXREG_FEATURE_KNOWN_MASK) == NX_STATUS_OK;
}

static int test_signed_enumeration_and_parent(void) {
    nxreg_signed_fixture data;
    nx_result result;
    nx_object_ref output;
    nx_state_node state;
    uint32_t count = 0u;
    CHECK(init_signed_fixture(&data));
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &state, &result) == NX_STATUS_OK);
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.visible, &state, &result) == NX_STATUS_OK);
    CHECK(!nx_object_ref_is_valid(state.spec.parent));
    CHECK(!nx_object_ref_is_valid(state.spec.owner));
    CHECK(state.spec.payload_offset == 0u);
    CHECK(nx_state_store_read(&data.store, data.visible, &state,
                              &result) == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(state.spec.parent, data.root));
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.hidden, &state, &result)
          == NX_STATUS_ACCESS_DENIED);
    CHECK(nxreg_client_child_count(&data.client, &data.caller,
                                   data.root, &count, &result) == NX_STATUS_OK);
    CHECK(count == 1u);
    CHECK(nxreg_client_child_first(&data.client, &data.caller,
                                   data.root, &output, &result) == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(output, data.visible));
    CHECK(nxreg_client_parent_read(&data.client, &data.caller,
                                   data.visible, &output, &result)
          == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(output, data.root));
    CHECK(nxreg_client_child_next(&data.client, &data.caller,
                                  data.root, data.hidden, &output,
                                  &result) == NX_STATUS_ACCESS_DENIED);
    CHECK(!nx_object_ref_is_valid(output));
    CHECK(nxreg_client_child_next(&data.client, &data.caller,
                                  data.root, data.visible, &output,
                                  &result) == NX_STATUS_NOT_FOUND);
    CHECK(!nx_object_ref_is_valid(output));
    CHECK(nx_state_store_validate(&data.store, &result) == NX_STATUS_OK);
    return 0;
}

static int test_forgery_expiry_revocation_and_budget(void) {
    nxreg_signed_fixture data;
    nx_result result;
    nx_state_node node;
    nx_object_ref parent;
    uint8_t forged[NX_CAPABILITY_TOKEN_SIZE];
    struct nxreg_auth_context impostor = {UINT64_C(0x4e585245474d4143)};
    CHECK(init_signed_fixture(&data));
    CHECK(nxreg_client_object_read(&data.client, &impostor,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result) == NX_STATUS_OK);
    memcpy(forged, data.root_token, sizeof(forged));
    forged[38u] ^= 0x04u;
    data.tokens[0].data = forged;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    data.tokens[0].data = data.root_token;
    data.issuer.slots[0].lease_epoch++;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    data.issuer.slots[0].lease_epoch--;
    data.issuer.slots[0].active = 0u;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    data.issuer.slots[0].active = 1u;
    data.auth.current_tick = 1000u;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    data.auth.current_tick = 10u;
    data.auth.scope_id = 71u;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_ACCESS_DENIED);
    data.auth.scope_id = 70u;
    data.auth.token_count = 1u;
    data.tokens[0] = (nx_const_span){
        data.visible_token, sizeof(data.visible_token)};
    CHECK(nxreg_client_parent_read(&data.client, &data.caller,
                                   data.visible, &parent, &result)
          == NX_STATUS_NOT_FOUND);
    CHECK(!nx_object_ref_is_valid(parent));
    data.auth.token_count = 2u;
    data.tokens[0] = (nx_const_span){
        data.root_token, sizeof(data.root_token)};
    data.adapter.authorization_fuel_limits.object_read = 1u;
    CHECK(nxreg_client_object_read(&data.client, &data.caller,
                                   data.root, &node, &result)
          == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(result.fuel_used == 1u);
    CHECK(nx_state_store_validate(&data.store, &result) == NX_STATUS_OK);
    return 0;
}

int main(void) {
    CHECK(test_signed_enumeration_and_parent() == 0);
    CHECK(test_forgery_expiry_revocation_and_budget() == 0);
    puts("NXREG signed NXOLMO capability-backed masked child/parent access: PASS");
    return 0;
}
