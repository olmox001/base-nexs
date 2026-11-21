/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_hardware_capability.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(nx_hardware_resource) == 120u,
               "hardware resource ABI size changed");
_Static_assert(offsetof(nx_hardware_resource, object) == 16u,
               "hardware resource object offset changed");
_Static_assert(offsetof(nx_hardware_resource, resource_offset) == 64u,
               "hardware resource range offset changed");
_Static_assert(offsetof(nx_hardware_resource, max_transfer_bytes) == 112u,
               "hardware resource transfer-limit offset changed");
_Static_assert(sizeof(nx_hardware_grant) == 96u,
               "hardware grant ABI size changed");
_Static_assert(offsetof(nx_hardware_grant, resource) == 8u,
               "hardware grant resource offset changed");
_Static_assert(offsetof(nx_hardware_grant, max_transfer_bytes) == 88u,
               "hardware grant transfer-limit offset changed");

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n",              \
                          __FILE__, __LINE__, #expression);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static nx_object_ref make_ref(uint64_t high, uint64_t low,
                              uint64_t generation) {
    nx_object_ref ref;
    ref.id.high = high;
    ref.id.low = low;
    ref.generation = generation;
    return ref;
}

static void init_mmio(nx_hardware_resource *resource) {
    memset(resource, 0, sizeof(*resource));
    resource->abi_version = NX_HARDWARE_RESOURCE_ABI_VERSION;
    resource->policy_version = 1u;
    resource->resource_type = NX_HARDWARE_RESOURCE_MMIO_REGION;
    resource->flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                      NX_HARDWARE_RESOURCE_WRITE_COMBINING;
    resource->object = make_ref(1u, 2u, 3u);
    resource->parent = make_ref(1u, 1u, 1u);
    resource->resource_offset = UINT64_C(0x1000);
    resource->resource_length = UINT64_C(0x2000);
    resource->alignment = UINT64_C(0x1000);
    resource->lease_epoch = 7u;
    resource->rights_ceiling = NX_HARDWARE_RIGHT_MAP |
                               NX_HARDWARE_RIGHT_READ |
                               NX_HARDWARE_RIGHT_WRITE;
}

static void init_grant(nx_hardware_grant *grant,
                       const nx_hardware_resource *resource) {
    memset(grant, 0, sizeof(*grant));
    grant->abi_version = NX_HARDWARE_GRANT_ABI_VERSION;
    grant->policy_version = 1u;
    grant->resource = resource->object;
    grant->principal = make_ref(9u, 8u, 7u);
    grant->rights = resource->rights_ceiling;
    grant->scope_id = 55u;
    grant->lease_epoch = resource->lease_epoch;
    grant->expiry_tick = 100u;
    grant->max_transfer_bytes = resource->max_transfer_bytes;
}

typedef struct hardware_resolver_context {
    nx_capability_slot_state state;
    uint64_t expected_index;
    uint64_t calls;
} hardware_resolver_context;

static nx_status resolve_hardware_slot(
    void *context, uint64_t index, nx_capability_slot_state *state) {
    hardware_resolver_context *resolver =
        (hardware_resolver_context *)context;
    resolver->calls++;
    if (index != resolver->expected_index)
        return NX_STATUS_NOT_FOUND;
    *state = resolver->state;
    return NX_STATUS_OK;
}

static int test_token_derived_grant(void) {
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims claims;
    nx_capability_validation validation;
    hardware_resolver_context resolver;
    nx_hardware_resource resource;
    nx_hardware_grant grant;
    nx_hardware_grant unchanged;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    size_t i;

    init_mmio(&resource);
    memset(&key, 0, sizeof(key));
    key.key_id = 7u;
    key.active = 1u;
    key.not_before_tick = 1u;
    key.not_after_tick = 1000u;
    for (i = 0u; i < sizeof(key.secret); ++i)
        key.secret[i] = (uint8_t)(i + 1u);
    keyring.keys = &key;
    keyring.key_count = 1u;

    memset(&claims, 0, sizeof(claims));
    claims.virtual_slot_index = 3u;
    claims.slot_generation = 3u;
    claims.rights_mask = NX_HARDWARE_RIGHT_READ;
    claims.scope_id = 55u;
    claims.lease_epoch = resource.lease_epoch;
    claims.expiry_tick = 90u;
    claims.nonce = 123u;
    claims.subject = make_ref(9u, 8u, 7u);
    claims.policy_version = 1u;

    memset(&validation, 0, sizeof(validation));
    validation.principal = claims.subject;
    validation.required_rights = NX_HARDWARE_RIGHT_READ;
    validation.expected_scope_id = claims.scope_id;
    validation.expected_lease_epoch = claims.lease_epoch;
    validation.current_tick = 10u;
    validation.slot_capacity = 8u;
    validation.expected_policy_version = claims.policy_version;

    memset(&resolver, 0, sizeof(resolver));
    resolver.expected_index = claims.virtual_slot_index;
    resolver.state.object = resource.object;
    resolver.state.subject = claims.subject;
    resolver.state.generation = claims.slot_generation;
    resolver.state.scope_id = claims.scope_id;
    resolver.state.lease_epoch = claims.lease_epoch;
    resolver.state.rights_ceiling = resource.rights_ceiling;
    resolver.state.policy_version = claims.policy_version;
    resolver.state.active = 1u;

    CHECK(nx_capability_issue(&keyring, key.key_id, &claims,
                              validation.current_tick, token) ==
          NX_STATUS_OK);
    memset(&grant, 0, sizeof(grant));
    CHECK(nx_hardware_grant_from_token(
              &keyring, (nx_const_span){token, sizeof(token)}, &validation,
              resolve_hardware_slot, &resolver, &resource,
              NX_HARDWARE_RIGHT_READ, &grant) == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(grant.resource, resource.object) != 0);
    CHECK(nx_object_ref_equal(grant.principal, claims.subject) != 0);
    CHECK(grant.rights == NX_HARDWARE_RIGHT_READ);
    CHECK(grant.scope_id == claims.scope_id);
    CHECK(grant.expiry_tick == claims.expiry_tick);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ,
              validation.current_tick) == NX_STATUS_OK);

    unchanged = grant;
    {
        nx_hardware_resource resource_copy = resource;
        uint8_t token_copy[NX_CAPABILITY_TOKEN_SIZE];
        memcpy(token_copy, token, sizeof(token_copy));
        CHECK(nx_hardware_grant_from_token(
                  &keyring, (nx_const_span){token, sizeof(token)}, &validation,
                  resolve_hardware_slot, &resolver, &resource,
                  NX_HARDWARE_RIGHT_READ,
                  (nx_hardware_grant *)(void *)&resource) ==
              NX_STATUS_INVALID_ARGUMENT);
        CHECK(memcmp(&resource, &resource_copy, sizeof(resource)) == 0);
        CHECK(nx_hardware_grant_from_token(
                  &keyring, (nx_const_span){token, sizeof(token)}, &validation,
                  resolve_hardware_slot, &resolver, &resource,
                  NX_HARDWARE_RIGHT_READ,
                  (nx_hardware_grant *)(void *)token) ==
              NX_STATUS_INVALID_ARGUMENT);
        CHECK(memcmp(token, token_copy, sizeof(token)) == 0);
    }
    token[8] ^= UINT8_C(1);
    CHECK(nx_hardware_grant_from_token(
              &keyring, (nx_const_span){token, sizeof(token)}, &validation,
              resolve_hardware_slot, &resolver, &resource,
              NX_HARDWARE_RIGHT_READ, &grant) == NX_STATUS_ACCESS_DENIED);
    CHECK(memcmp(&grant, &unchanged, sizeof(grant)) == 0);

    token[8] ^= UINT8_C(1);
    resource.object = make_ref(44u, 55u, 66u);
    CHECK(nx_hardware_grant_from_token(
              &keyring, (nx_const_span){token, sizeof(token)}, &validation,
              resolve_hardware_slot, &resolver, &resource,
              NX_HARDWARE_RIGHT_READ, &grant) == NX_STATUS_ACCESS_DENIED);
    CHECK(memcmp(&grant, &unchanged, sizeof(grant)) == 0);
    resource.object = make_ref(1u, 2u, 3u);
    resource.policy_version = 2u;
    CHECK(nx_hardware_grant_from_token(
              &keyring, (nx_const_span){token, sizeof(token)}, &validation,
              resolve_hardware_slot, &resolver, &resource,
              NX_HARDWARE_RIGHT_READ, &grant) == NX_STATUS_ACCESS_DENIED);
    CHECK(memcmp(&grant, &unchanged, sizeof(grant)) == 0);
    return 0;
}

static int test_resource_validation(void) {
    nx_hardware_resource resource;
    init_mmio(&resource);
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_OK);
    resource.policy_version = 0u;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.policy_version = 1u;

    resource.flags = NX_HARDWARE_RESOURCE_WRITE_COMBINING;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                     NX_HARDWARE_RESOURCE_SHAREABLE;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                     NX_HARDWARE_RESOURCE_WRITE_COMBINING;
    resource.parent.generation = 0u;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.parent = make_ref(1u, 1u, 1u);
    resource.alignment = 3u;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.alignment = UINT64_C(0x1000);
    resource.resource_offset = UINT64_MAX - UINT64_C(0x0fff);
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_OVERFLOW);
    resource.resource_offset = UINT64_C(0x1000);
    resource.resource_type = NX_HARDWARE_RESOURCE_DMA_DOMAIN;
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                     NX_HARDWARE_RESOURCE_CACHE_COHERENT;
    resource.domain_id = 1u;
    resource.rights_ceiling = NX_HARDWARE_RIGHT_DMA_MAP |
                              NX_HARDWARE_RIGHT_DMA_DEVICE_READ |
                              NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE;
    resource.max_transfer_bytes = resource.resource_length + 1u;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.resource_type = NX_HARDWARE_RESOURCE_MMIO_REGION;
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                     NX_HARDWARE_RESOURCE_WRITE_COMBINING;
    resource.domain_id = 0u;
    resource.max_transfer_bytes = 0u;
    resource.rights_ceiling |= NX_HARDWARE_RIGHT_IRQ_BIND;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_ACCESS_DENIED);
    resource.rights_ceiling = NX_HARDWARE_RIGHT_MAP |
                              NX_HARDWARE_RIGHT_READ |
                              NX_HARDWARE_RIGHT_WRITE;
    resource.flags |= UINT32_C(0x80000000);
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);

    memset(&resource, 0, sizeof(resource));
    resource.abi_version = NX_HARDWARE_RESOURCE_ABI_VERSION;
    resource.policy_version = 1u;
    resource.resource_type = NX_HARDWARE_RESOURCE_DEVICE_ENDPOINT;
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE;
    resource.object = make_ref(2u, 3u, 4u);
    resource.lease_epoch = 1u;
    resource.rights_ceiling = NX_HARDWARE_RIGHT_ENDPOINT_CALL;
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    resource.parent = make_ref(2u, 2u, 2u);
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_OK);
    resource.parent = make_ref(2u, 3u, 999u);
    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_MALFORMED);
    return 0;
}

static int test_grant_and_range_validation(void) {
    nx_hardware_resource resource;
    nx_hardware_grant grant;
    uint64_t end_offset = UINT64_C(0xdeadbeef);

    init_mmio(&resource);
    init_grant(&grant, &resource);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 50u) ==
          NX_STATUS_OK);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(1u, 2u, 3u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_ACCESS_DENIED);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(56),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_ACCESS_DENIED);
    grant.principal = make_ref(1u, 1u, 1u);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_ACCESS_DENIED);
    grant.principal = make_ref(9u, 8u, 7u);
    grant.policy_version = 2u;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_ACCESS_DENIED);
    grant.policy_version = 1u;
    resource.policy_version = 2u;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_ACCESS_DENIED);
    resource.policy_version = 1u;
    CHECK(nx_hardware_range_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), UINT64_C(0x1000), UINT64_C(0x1000),
              NX_HARDWARE_RIGHT_READ, 50u, &end_offset) == NX_STATUS_OK);
    CHECK(end_offset == UINT64_C(0x2000));
    CHECK(nx_hardware_range_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), UINT64_C(0x1000), 1u,
              NX_HARDWARE_RIGHT_READ, 50u,
              (uint64_t *)(void *)&resource.resource_offset) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(resource.resource_offset == UINT64_C(0x1000));
    CHECK(nx_hardware_grant_validate(
              &resource, (const nx_hardware_grant *)(const void *)&resource, make_ref(9u, 8u, 7u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 50u) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), UINT64_C(1) << 63, 50u) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_range_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), UINT64_C(0x1fff), 2u,
              NX_HARDWARE_RIGHT_READ, 50u, &end_offset) ==
          NX_STATUS_LIMIT_EXCEEDED);
    CHECK(end_offset == UINT64_C(0x2000));
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_IRQ_ACK, 50u) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 100u) ==
          NX_STATUS_ACCESS_DENIED);

    grant.resource = make_ref(3u, 4u, 5u);
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 50u) ==
          NX_STATUS_ACCESS_DENIED);
    grant.resource = resource.object;
    grant.lease_epoch++;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 50u) ==
          NX_STATUS_ACCESS_DENIED);
    grant.lease_epoch = resource.lease_epoch;
    grant.rights |= NX_HARDWARE_RIGHT_IRQ_ACK;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 50u) ==
          NX_STATUS_ACCESS_DENIED);
    grant.rights = resource.rights_ceiling;
    grant.scope_id = 0u;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_READ, 50u) ==
          NX_STATUS_MALFORMED);
    grant.scope_id = 55u;

    resource.resource_type = NX_HARDWARE_RESOURCE_PCI_FUNCTION;
    resource.resource_length = 0u;
    resource.resource_offset = 0u;
    resource.alignment = 0u;
    resource.flags = NX_HARDWARE_RESOURCE_SHAREABLE;
    resource.parent = (nx_object_ref){0};
    resource.rights_ceiling = NX_HARDWARE_RIGHT_DISCOVER;
    grant.resource = resource.object;
    grant.rights = NX_HARDWARE_RIGHT_DISCOVER;
    CHECK(nx_hardware_range_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 0u, 1u, NX_HARDWARE_RIGHT_DISCOVER,
              50u, &end_offset) == NX_STATUS_NOT_SUPPORTED);
    return 0;
}

static int test_alignment_rejection(void) {
    nx_hardware_resource resource;
    nx_hardware_grant grant;
    _Alignas(nx_hardware_resource)
        uint8_t resource_storage[sizeof(nx_hardware_resource) + 1u];
    _Alignas(nx_hardware_grant)
        uint8_t grant_storage[sizeof(nx_hardware_grant) + 1u];
    _Alignas(uint64_t) uint8_t end_storage[sizeof(uint64_t) + 1u];

    init_mmio(&resource);
    init_grant(&grant, &resource);
    memset(resource_storage, 0, sizeof(resource_storage));
    memset(grant_storage, 0, sizeof(grant_storage));
    memset(end_storage, 0, sizeof(end_storage));
    CHECK(nx_hardware_resource_validate(
              (const nx_hardware_resource *)(const void *)(resource_storage + 1u)) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_dma_validate(
              (const nx_hardware_resource *)(const void *)(resource_storage + 1u),
              &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 0u, 1u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_grant_validate(
              &resource,
              (const nx_hardware_grant *)(const void *)(grant_storage + 1u),
              make_ref(9u, 8u, 7u), UINT64_C(55),
              NX_HARDWARE_RIGHT_READ, 10u) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_range_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 0u, 1u, NX_HARDWARE_RIGHT_READ, 10u,
              (uint64_t *)(void *)(end_storage + 1u)) ==
          NX_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_dma_validation(void) {
    nx_hardware_resource resource;
    nx_hardware_grant grant;
    uint64_t dma_rights = NX_HARDWARE_RIGHT_DMA_MAP |
                          NX_HARDWARE_RIGHT_DMA_DEVICE_READ |
                          NX_HARDWARE_RIGHT_DMA_DEVICE_WRITE;

    memset(&resource, 0, sizeof(resource));
    resource.abi_version = NX_HARDWARE_RESOURCE_ABI_VERSION;
    resource.policy_version = 1u;
    resource.resource_type = NX_HARDWARE_RESOURCE_DMA_DOMAIN;
    resource.flags = NX_HARDWARE_RESOURCE_EXCLUSIVE |
                     NX_HARDWARE_RESOURCE_CACHE_COHERENT;
    resource.object = make_ref(10u, 20u, 1u);
    resource.parent = make_ref(10u, 1u, 1u);
    resource.resource_length = 8192u;
    resource.alignment = 64u;
    resource.domain_id = 2u;
    resource.lease_epoch = 4u;
    resource.max_transfer_bytes = 1024u;
    resource.rights_ceiling = dma_rights;
    init_grant(&grant, &resource);
    grant.rights = dma_rights;
    grant.max_transfer_bytes = 1024u;

    CHECK(nx_hardware_resource_validate(&resource) == NX_STATUS_OK);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) == NX_STATUS_OK);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 0u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 64u,
              NX_HARDWARE_DMA_BIDIRECTIONAL, 10u) == NX_STATUS_OK);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 65u, 512u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 2048u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) ==
          NX_STATUS_LIMIT_EXCEEDED);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 32u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 64u, 0u, 10u) ==
          NX_STATUS_INVALID_ARGUMENT);
    grant.rights = NX_HARDWARE_RIGHT_DMA_MAP |
                   NX_HARDWARE_RIGHT_DMA_DEVICE_READ;
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 64u,
              NX_HARDWARE_DMA_DEVICE_WRITES, 10u) ==
          NX_STATUS_ACCESS_DENIED);

    grant.rights = dma_rights;
    grant.max_transfer_bytes = 0u;
    CHECK(nx_hardware_grant_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), NX_HARDWARE_RIGHT_DMA_MAP, 10u) ==
          NX_STATUS_ACCESS_DENIED);
    CHECK(nx_hardware_dma_validate(
              &resource, &grant, make_ref(9u, 8u, 7u), UINT64_C(55), 64u, 512u, 64u,
              NX_HARDWARE_DMA_DEVICE_READS, 10u) ==
          NX_STATUS_ACCESS_DENIED);
    return 0;
}

int main(void) {
    CHECK(test_token_derived_grant() == 0);
    CHECK(test_resource_validation() == 0);
    CHECK(test_grant_and_range_validation() == 0);
    CHECK(test_alignment_rejection() == 0);
    CHECK(test_dma_validation() == 0);
    (void)puts("hardware capability contract tests: ok");
    return 0;
}
