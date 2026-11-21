/* Product port of pinned NXOLMO external/references/nxolmo/src/authority/capability.c; no reference linkage. */
#include "nexs_capability.h"

#include "nexs_checked.h"
#include "nexs_crypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NX_CAPABILITY_DOMAIN "NXOLMO:CAPABILITY:TOKEN:V1"
#define NX_CAPABILITY_DOMAIN_SIZE (sizeof(NX_CAPABILITY_DOMAIN) - 1u)
#define NX_CAPABILITY_ALLOWED_FLAGS UINT16_C(0)

static void nx_capability_secure_zero(void *memory, size_t size) {
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    while (size-- != 0u)
        *cursor++ = 0u;
}


static int nx_capability_all_zero(const uint8_t *bytes, size_t size) {
    uint8_t aggregate = 0u;
    size_t i;
    for (i = 0u; i < size; ++i)
        aggregate |= bytes[i];
    return aggregate == 0u;
}

static nx_status nx_capability_keyring_storage_size(
    const nx_capability_keyring *keyring, size_t *keys_size) {
    if (keyring == NULL || keys_size == NULL ||
        ((uintptr_t)keyring % (uintptr_t)_Alignof(nx_capability_keyring)) != 0u ||
        keyring->keys == NULL || keyring->key_count == 0u ||
        ((uintptr_t)keyring->keys % (uintptr_t)_Alignof(nx_capability_key)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    return nx_size_mul((size_t)keyring->key_count,
                       sizeof(nx_capability_key), keys_size);
}

static uint16_t nx_cap_read_u16_le(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint32_t nx_cap_read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t nx_cap_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (i * 8u);
    return value;
}

static void nx_cap_write_u16_le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void nx_cap_write_u32_le(uint8_t *bytes, uint32_t value) {
    unsigned int i;
    for (i = 0u; i < 4u; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
}

static void nx_cap_write_u64_le(uint8_t *bytes, uint64_t value) {
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
}

static const nx_capability_key *nx_capability_find_key(
    const nx_capability_keyring *keyring, uint32_t key_id) {
    uint32_t lower = 0u;
    uint32_t upper = keyring->key_count;
    while (lower < upper) {
        uint32_t middle = lower + ((upper - lower) / 2u);
        uint32_t candidate = keyring->keys[middle].key_id;
        if (candidate < key_id)
            lower = middle + 1u;
        else
            upper = middle;
    }
    if (lower < keyring->key_count &&
        keyring->keys[lower].key_id == key_id &&
        keyring->keys[lower].active != 0u)
        return &keyring->keys[lower];
    return NULL;
}

static nx_status nx_capability_compute_tag(const uint8_t *payload,
                                           const nx_capability_key *key,
                                           uint8_t tag[NX_CAPABILITY_TOKEN_AUTH_SIZE]) {
    uint8_t mac_input[NX_CAPABILITY_DOMAIN_SIZE +
                      NX_CAPABILITY_TOKEN_AUTH_OFFSET];
    nx_digest256 digest;
    nx_status status;
    memcpy(mac_input, NX_CAPABILITY_DOMAIN, NX_CAPABILITY_DOMAIN_SIZE);
    memcpy(mac_input + NX_CAPABILITY_DOMAIN_SIZE, payload,
           NX_CAPABILITY_TOKEN_AUTH_OFFSET);
    status = nx_hmac_sha256((nx_const_span){key->secret, sizeof(key->secret)},
                            (nx_const_span){mac_input, sizeof(mac_input)},
                            &digest);
    if (status == NX_STATUS_OK)
        memcpy(tag, digest.bytes, NX_CAPABILITY_TOKEN_AUTH_SIZE);
    {
        nx_capability_secure_zero(mac_input, sizeof(mac_input));
        nx_capability_secure_zero(digest.bytes, sizeof(digest.bytes));
    }
    return status;
}

nx_status nx_capability_keyring_validate(const nx_capability_keyring *keyring) {
    uint32_t i;
    size_t keys_size;
    int overlap;
    nx_status status = nx_capability_keyring_storage_size(keyring, &keys_size);
    if (status != NX_STATUS_OK)
        return status;
    overlap = nx_pointer_ranges_overlap(keyring, sizeof(*keyring),
                                           keyring->keys, keys_size);
    if (overlap != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < keyring->key_count; ++i) {
        const nx_capability_key *key = &keyring->keys[i];
        if (key->active > 1u || key->key_id == 0u ||
            key->not_before_tick >= key->not_after_tick ||
            (key->active != 0u &&
             nx_capability_all_zero(key->secret, sizeof(key->secret))))
            return NX_STATUS_MALFORMED;
        if (i != 0u && keyring->keys[i - 1u].key_id >= key->key_id)
            return NX_STATUS_CONFLICT;
    }
    return NX_STATUS_OK;
}

static void nx_capability_encode_claims(
    uint8_t encoded[NX_CAPABILITY_TOKEN_SIZE], uint32_t key_id,
    const nx_capability_claims *claims) {
    memset(encoded, 0, NX_CAPABILITY_TOKEN_SIZE);
    nx_cap_write_u16_le(encoded, NX_CAPABILITY_TOKEN_VERSION);
    nx_cap_write_u16_le(encoded + 2u, NX_CAPABILITY_ALLOWED_FLAGS);
    nx_cap_write_u32_le(encoded + 4u, key_id);
    nx_cap_write_u64_le(encoded + 8u, claims->virtual_slot_index);
    nx_cap_write_u64_le(encoded + 16u, claims->slot_generation);
    nx_cap_write_u64_le(encoded + 24u, claims->rights_mask);
    nx_cap_write_u64_le(encoded + 32u, claims->scope_id);
    nx_cap_write_u64_le(encoded + 40u, claims->lease_epoch);
    nx_cap_write_u64_le(encoded + 48u, claims->expiry_tick);
    nx_cap_write_u64_le(encoded + 56u, claims->nonce);
    nx_cap_write_u64_le(encoded + 64u, claims->subject.id.high);
    nx_cap_write_u64_le(encoded + 72u, claims->subject.id.low);
    nx_cap_write_u64_le(encoded + 80u, claims->subject.generation);
    nx_cap_write_u32_le(encoded + 88u, claims->policy_version);
}

static nx_status nx_capability_issue_validate_claims(
    const nx_capability_claims *claims, uint64_t current_tick) {
    if (claims->slot_generation == 0u || claims->rights_mask == 0u ||
        claims->scope_id == 0u || claims->lease_epoch == 0u ||
        claims->expiry_tick <= current_tick || claims->nonce == 0u ||
        !nx_object_ref_is_valid(claims->subject) ||
        claims->policy_version == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

nx_status nx_capability_issue(const nx_capability_keyring *keyring,
                              uint32_t key_id,
                              const nx_capability_claims *claims,
                              uint64_t current_tick,
                              uint8_t token[NX_CAPABILITY_TOKEN_SIZE]) {
    const nx_capability_key *key;
    uint8_t encoded[NX_CAPABILITY_TOKEN_SIZE];
    uint8_t tag[NX_CAPABILITY_TOKEN_AUTH_SIZE];
    nx_status status;
    size_t keys_size;

    if (token == NULL || claims == NULL ||
        ((uintptr_t)claims % (uintptr_t)_Alignof(nx_capability_claims)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nx_capability_keyring_storage_size(keyring, &keys_size);
    if (status != NX_STATUS_OK)
        return status;
    if (nx_pointer_ranges_overlap(token, NX_CAPABILITY_TOKEN_SIZE,
                                     claims, sizeof(*claims)) != 0 ||
        nx_pointer_ranges_overlap(token, NX_CAPABILITY_TOKEN_SIZE,
                                     keyring, sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(token, NX_CAPABILITY_TOKEN_SIZE,
                                     keyring->keys, keys_size) != 0 ||
        nx_pointer_ranges_overlap(claims, sizeof(*claims),
                                     keyring, sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(claims, sizeof(*claims),
                                     keyring->keys, keys_size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    nx_capability_secure_zero(token, NX_CAPABILITY_TOKEN_SIZE);
    status = nx_capability_keyring_validate(keyring);
    if (status != NX_STATUS_OK)
        return status;
    key = nx_capability_find_key(keyring, key_id);
    if (key == NULL || current_tick < key->not_before_tick ||
        current_tick >= key->not_after_tick)
        return NX_STATUS_ACCESS_DENIED;
    status = nx_capability_issue_validate_claims(claims, current_tick);
    if (status != NX_STATUS_OK)
        return status;

    nx_capability_encode_claims(encoded, key_id, claims);
    status = nx_capability_compute_tag(encoded, key, tag);
    if (status == NX_STATUS_OK) {
        memcpy(encoded + NX_CAPABILITY_TOKEN_AUTH_OFFSET, tag, sizeof(tag));
        memcpy(token, encoded, sizeof(encoded));
    }
    nx_capability_secure_zero(encoded, sizeof(encoded));
    nx_capability_secure_zero(tag, sizeof(tag));
    return status;
}

static nx_status nx_capability_verify_aliases(
    const nx_capability_keyring *keyring, size_t keys_size,
    nx_const_span token, const nx_capability_validation *validation,
    nx_capability_claims *claims, nx_capability_slot_state *slot_state) {
    if (nx_pointer_ranges_overlap(claims, sizeof(*claims), slot_state,
                                  sizeof(*slot_state)) != 0 ||
        nx_pointer_ranges_overlap(claims, sizeof(*claims), token.data,
                                  token.size) != 0 ||
        nx_pointer_ranges_overlap(slot_state, sizeof(*slot_state), token.data,
                                  token.size) != 0 ||
        nx_pointer_ranges_overlap(claims, sizeof(*claims), validation,
                                  sizeof(*validation)) != 0 ||
        nx_pointer_ranges_overlap(slot_state, sizeof(*slot_state), validation,
                                  sizeof(*validation)) != 0 ||
        nx_pointer_ranges_overlap(token.data, token.size, validation,
                                  sizeof(*validation)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_pointer_ranges_overlap(claims, sizeof(*claims), keyring,
                                  sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(slot_state, sizeof(*slot_state), keyring,
                                  sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(validation, sizeof(*validation), keyring,
                                  sizeof(*keyring)) != 0 ||
        nx_pointer_ranges_overlap(token.data, token.size, keyring,
                                  sizeof(*keyring)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_pointer_ranges_overlap(claims, sizeof(*claims), keyring->keys,
                                  keys_size) != 0 ||
        nx_pointer_ranges_overlap(slot_state, sizeof(*slot_state), keyring->keys,
                                  keys_size) != 0 ||
        nx_pointer_ranges_overlap(validation, sizeof(*validation), keyring->keys,
                                  keys_size) != 0 ||
        nx_pointer_ranges_overlap(token.data, token.size, keyring->keys,
                                  keys_size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static nx_status nx_capability_verify_authenticate(
    const nx_capability_keyring *keyring, nx_const_span token,
    const nx_capability_validation *validation,
    uint8_t encoded[NX_CAPABILITY_TOKEN_SIZE]) {
    uint8_t expected_tag[NX_CAPABILITY_TOKEN_AUTH_SIZE];
    const nx_capability_key *key;
    nx_status status;
    uint32_t key_id;
    status = nx_capability_keyring_validate(keyring);
    if (status != NX_STATUS_OK)
        return NX_STATUS_ACCESS_DENIED;
    memcpy(encoded, token.data, NX_CAPABILITY_TOKEN_SIZE);
    if (nx_cap_read_u16_le(encoded) != NX_CAPABILITY_TOKEN_VERSION ||
        nx_cap_read_u16_le(encoded + 2u) != NX_CAPABILITY_ALLOWED_FLAGS ||
        nx_cap_read_u32_le(encoded + 92u) != 0u) {
        nx_capability_secure_zero(encoded, NX_CAPABILITY_TOKEN_SIZE);
        return NX_STATUS_MALFORMED;
    }
    key_id = nx_cap_read_u32_le(encoded + 4u);
    key = nx_capability_find_key(keyring, key_id);
    if (key == NULL || validation->current_tick < key->not_before_tick ||
        validation->current_tick >= key->not_after_tick) {
        nx_capability_secure_zero(encoded, NX_CAPABILITY_TOKEN_SIZE);
        return NX_STATUS_ACCESS_DENIED;
    }
    status = nx_capability_compute_tag(encoded, key, expected_tag);
    if (status != NX_STATUS_OK ||
        nx_crypto_constant_time_equal(expected_tag,
            encoded + NX_CAPABILITY_TOKEN_AUTH_OFFSET,
            NX_CAPABILITY_TOKEN_AUTH_SIZE) == 0) {
        nx_capability_secure_zero(encoded, NX_CAPABILITY_TOKEN_SIZE);
        nx_capability_secure_zero(expected_tag, sizeof(expected_tag));
        return NX_STATUS_ACCESS_DENIED;
    }
    nx_capability_secure_zero(expected_tag, sizeof(expected_tag));
    return NX_STATUS_OK;
}

static void nx_capability_decode_authenticated(
    const uint8_t encoded[NX_CAPABILITY_TOKEN_SIZE],
    nx_capability_claims *decoded) {
    memset(decoded, 0, sizeof(*decoded));
    decoded->virtual_slot_index = nx_cap_read_u64_le(encoded + 8u);
    decoded->slot_generation = nx_cap_read_u64_le(encoded + 16u);
    decoded->rights_mask = nx_cap_read_u64_le(encoded + 24u);
    decoded->scope_id = nx_cap_read_u64_le(encoded + 32u);
    decoded->lease_epoch = nx_cap_read_u64_le(encoded + 40u);
    decoded->expiry_tick = nx_cap_read_u64_le(encoded + 48u);
    decoded->nonce = nx_cap_read_u64_le(encoded + 56u);
    decoded->subject.id.high = nx_cap_read_u64_le(encoded + 64u);
    decoded->subject.id.low = nx_cap_read_u64_le(encoded + 72u);
    decoded->subject.generation = nx_cap_read_u64_le(encoded + 80u);
    decoded->policy_version = nx_cap_read_u32_le(encoded + 88u);
}

static nx_status nx_capability_validate_authenticated_claims(
    const nx_capability_claims *decoded,
    const nx_capability_validation *validation) {
    if (decoded->virtual_slot_index >= validation->slot_capacity ||
        decoded->slot_generation == 0u || decoded->rights_mask == 0u ||
        decoded->scope_id == 0u || decoded->lease_epoch == 0u ||
        decoded->expiry_tick <= validation->current_tick ||
        decoded->nonce == 0u || !nx_object_ref_is_valid(decoded->subject) ||
        decoded->policy_version == 0u ||
        !nx_object_ref_equal(decoded->subject, validation->principal) ||
        (validation->required_rights & ~decoded->rights_mask) != 0u ||
        (validation->expected_scope_id != 0u &&
         decoded->scope_id != validation->expected_scope_id) ||
        (validation->expected_lease_epoch != 0u &&
         decoded->lease_epoch != validation->expected_lease_epoch) ||
        (validation->expected_policy_version != 0u &&
         decoded->policy_version != validation->expected_policy_version))
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static nx_status nx_capability_resolve_authenticated_slot(
    nx_capability_resolve_fn resolve, void *resolve_context,
    const nx_capability_claims *decoded,
    nx_capability_slot_state *resolved) {
    nx_status status;
    memset(resolved, 0, sizeof(*resolved));
    status = resolve(resolve_context, decoded->virtual_slot_index, resolved);
    if (status != NX_STATUS_OK || resolved->active == 0u ||
        !nx_object_ref_is_valid(resolved->object) ||
        !nx_object_ref_equal(resolved->subject, decoded->subject) ||
        resolved->generation != decoded->slot_generation ||
        resolved->scope_id != decoded->scope_id ||
        resolved->lease_epoch != decoded->lease_epoch ||
        resolved->policy_version != decoded->policy_version ||
        (decoded->rights_mask & ~resolved->rights_ceiling) != 0u)
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static void nx_capability_publish_verified(
    nx_capability_claims *claims, nx_capability_slot_state *slot_state,
    nx_capability_claims *decoded, nx_capability_slot_state *resolved) {
    *claims = *decoded;
    *slot_state = *resolved;
    nx_capability_secure_zero(decoded, sizeof(*decoded));
    nx_capability_secure_zero(resolved, sizeof(*resolved));
}

nx_status nx_capability_verify(const nx_capability_keyring *keyring,
                               nx_const_span token,
                               const nx_capability_validation *validation,
                               nx_capability_resolve_fn resolve,
                               void *resolve_context,
                               nx_capability_claims *claims,
                               nx_capability_slot_state *slot_state) {
    uint8_t encoded[NX_CAPABILITY_TOKEN_SIZE];
    nx_capability_claims decoded;
    nx_capability_slot_state resolved;
    nx_status status;
    size_t keys_size;
    if (claims == NULL || slot_state == NULL || validation == NULL ||
        token.data == NULL ||
        ((uintptr_t)claims % (uintptr_t)_Alignof(nx_capability_claims)) != 0u ||
        ((uintptr_t)slot_state % (uintptr_t)_Alignof(nx_capability_slot_state)) != 0u ||
        ((uintptr_t)validation % (uintptr_t)_Alignof(nx_capability_validation)) != 0u)
        return NX_STATUS_MALFORMED;
    status = nx_capability_keyring_storage_size(keyring, &keys_size);
    if (status != NX_STATUS_OK)
        return NX_STATUS_ACCESS_DENIED;
    status = nx_capability_verify_aliases(keyring, keys_size, token, validation,
                                          claims, slot_state);
    if (status != NX_STATUS_OK)
        return status;
    if (token.size != NX_CAPABILITY_TOKEN_SIZE || resolve == NULL ||
        !nx_object_ref_is_valid(validation->principal) ||
        validation->slot_capacity == 0u || validation->required_rights == 0u)
        return NX_STATUS_MALFORMED;
    status = nx_capability_verify_authenticate(keyring, token, validation,
                                               encoded);
    if (status != NX_STATUS_OK)
        return status;
    nx_capability_decode_authenticated(encoded, &decoded);
    nx_capability_secure_zero(encoded, sizeof(encoded));
    status = nx_capability_validate_authenticated_claims(&decoded, validation);
    if (status != NX_STATUS_OK) {
        nx_capability_secure_zero(&decoded, sizeof(decoded));
        return status;
    }
    status = nx_capability_resolve_authenticated_slot(
        resolve, resolve_context, &decoded, &resolved);
    if (status != NX_STATUS_OK) {
        nx_capability_secure_zero(&decoded, sizeof(decoded));
        nx_capability_secure_zero(&resolved, sizeof(resolved));
        return status;
    }
    nx_capability_publish_verified(claims, slot_state, &decoded, &resolved);
    return NX_STATUS_OK;
}

nx_status nx_capability_attenuate(const nx_capability_claims *parent,
                                  uint64_t reduced_rights,
                                  uint64_t reduced_scope,
                                  uint64_t reduced_expiry,
                                  uint64_t new_nonce,
                                  nx_capability_claims *child) {
    if (parent == NULL || child == NULL ||
        ((uintptr_t)parent % (uintptr_t)_Alignof(nx_capability_claims)) != 0u ||
        ((uintptr_t)child % (uintptr_t)_Alignof(nx_capability_claims)) != 0u ||
        !nx_object_ref_is_valid(parent->subject) || parent->rights_mask == 0u ||
        parent->scope_id == 0u || parent->expiry_tick == 0u || parent->nonce == 0u ||
        reduced_rights == 0u || reduced_scope == 0u || reduced_expiry == 0u ||
        new_nonce == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (parent != child &&
        nx_pointer_ranges_overlap(parent, sizeof(*parent),
                                     child, sizeof(*child)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if ((reduced_rights & ~parent->rights_mask) != 0u ||
        reduced_scope != parent->scope_id ||
        reduced_expiry > parent->expiry_tick || new_nonce == parent->nonce)
        return NX_STATUS_ACCESS_DENIED;
    *child = *parent;
    child->rights_mask = reduced_rights;
    child->expiry_tick = reduced_expiry;
    child->nonce = new_nonce;
    return NX_STATUS_OK;
}
