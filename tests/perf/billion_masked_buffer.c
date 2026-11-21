/* Actual NXOLMO StateStore + signed capability + buffer lease.
 * Measures a billion zero-copy bit transformations, not a billion commits.
 * The one batch commit has its OWN measured time and SHA+CRC verification. */
#include "nexs_buffer.h"
#include "nexs_buffer_capability_adapter.h"
#include "nexs_capability.h"
#include "nexs_clock.h"
#include "nexs_cycle_counter.h"
#include "nexs_state_store.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "real NXOLMO masked-buffer failure line %d: %s\n", \
            __LINE__, #x); return 1; } \
} while (0)

typedef struct {
    nx_capability_slot_state state;
    uint64_t index;
} resolver_state;

static nx_status resolve_token(void *context, uint64_t index,
                               nx_capability_slot_state *state) {
    resolver_state *resolver = (resolver_state *)context;
    if (resolver == NULL || state == NULL || index != resolver->index)
        return NX_STATUS_NOT_FOUND;
    *state = resolver->state;
    return NX_STATUS_OK;
}

static nx_object_ref make_ref(uint64_t low) {
    nx_object_ref ref = {{UINT64_C(0x4249544d41534b53), low}, 1u};
    return ref;
}

static int issue_token(const nx_capability_keyring *keyring,
                       nx_capability_claims *claims,
                       nx_buffer_capability_context *context,
                       uint8_t token[NX_CAPABILITY_TOKEN_SIZE]) {
    if (nx_capability_issue(keyring, 44u, claims, 20u, token) != NX_STATUS_OK)
        return 1;
    context->token = (nx_const_span){token, NX_CAPABILITY_TOKEN_SIZE};
    return 0;
}

static int verify_lease_bytes(const uint8_t *bytes, size_t count,
                              uint64_t operations) {
    const uint64_t bit_count = (uint64_t)count * UINT64_C(8);
    const uint64_t full_cycles = operations / bit_count;
    const uint64_t remainder = operations % bit_count;
    size_t index;
    for (index = 0u; index < count; ++index) {
        uint8_t expected = (full_cycles & 1u) != 0u ? UINT8_MAX : 0u;
        uint64_t position = (uint64_t)index * 8u;
        uint32_t bit;
        for (bit = 0u; bit < 8u; ++bit) {
            if (position + bit < remainder)
                expected ^= (uint8_t)(1u << bit);
        }
        if (bytes[index] != expected) {
            fprintf(stderr, "masked byte mismatch at offset %zu\n", index);
            return 1;
        }
    }
    return 0;
}

static int parse_count(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0u)
        return 0;
    *out = (uint64_t)value;
    return 1;
}

int main(int argc, char **argv) {
    uint64_t operations = UINT64_C(200000000);
    uint64_t length64 = UINT64_C(1048576);
    nx_state_store registry;
    nx_state_slot registry_slots[16];
    uint32_t index[32], rehash[32];
    const uint64_t hash_key[2] = {31u, 45u};
    nx_buffer_manager manager;
    nx_buffer_slot buffer_slots[8];
    nx_buffer_lease_slot leases[16];
    nx_buffer_view view = {0};
    nx_result result;
    nx_capability_key key;
    nx_capability_keyring keyring;
    nx_capability_claims claims;
    nx_buffer_capability_context context;
    resolver_state resolver;
    nx_object_ref principal = make_ref(77u);
    nx_object_ref buffer;
    nx_buffer_authority authority;
    uint8_t token[NX_CAPABILITY_TOKEN_SIZE];
    uint8_t *arena, *bitmap;
    size_t arena_size, bitmap_size, i;
    uint64_t begin = 0u, after_updates = 0u, after_commit = 0u;
    nx_cycle_counter cycle_counter;
    nx_cycle_sample cycle_begin, cycle_end;
    int cycle_known = 0;
    uint64_t cycles_updates = 0u;
    uint64_t bit = 0u, bit_count;
    volatile uint8_t *mapped;
    nx_buffer_stats stats;

    if ((argc > 1 && !parse_count(argv[1], &operations)) ||
        (argc > 2 && !parse_count(argv[2], &length64)) || argc > 3 ||
        operations > SIZE_MAX || length64 > SIZE_MAX ||
        length64 > UINT64_MAX / 8u ||
        length64 < 64u || length64 % 64u != 0u)
        return 2;
    arena_size = (size_t)length64;
    bitmap_size = ((arena_size / 64u) + 7u) / 8u;
    arena = aligned_alloc(64u, arena_size);
    bitmap = calloc(bitmap_size, 1u);
    CHECK(arena != NULL && bitmap != NULL);
    memset(arena, 0, arena_size);
    CHECK(nx_state_store_init(&registry, registry_slots, 16u, index,
                              rehash, 32u, 77u, hash_key,
                              &result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_init(&manager, &registry, buffer_slots, 8u,
                                  leases, 16u, arena, length64, 64u,
                                  bitmap, bitmap_size,
                                  &result) == NX_STATUS_OK);

    memset(&key, 0, sizeof(key));
    key.key_id = 44u;
    key.active = 1u;
    key.not_before_tick = 1u;
    key.not_after_tick = 1000u;
    for (i = 0u; i < sizeof(key.secret); ++i)
        key.secret[i] = (uint8_t)(i + 19u);
    keyring.keys = &key;
    keyring.key_count = 1u;
    memset(&claims, 0, sizeof(claims));
    claims.virtual_slot_index = 1u;
    claims.slot_generation = 1u;
    claims.rights_mask = NX_BUFFER_RIGHT_CREATE;
    claims.scope_id = 88u;
    claims.lease_epoch = 12u;
    claims.expiry_tick = 900u;
    claims.nonce = 1u;
    claims.subject = principal;
    claims.policy_version = 3u;
    memset(&context, 0, sizeof(context));
    memset(&resolver, 0, sizeof(resolver));
    CHECK(issue_token(&keyring, &claims, &context, token) == 0);
    resolver.index = 1u;
    resolver.state.object = make_ref(999u);
    resolver.state.subject = principal;
    resolver.state.generation = 1u;
    resolver.state.scope_id = 88u;
    resolver.state.lease_epoch = 12u;
    resolver.state.rights_ceiling = NX_BUFFER_RIGHT_CREATE;
    resolver.state.policy_version = 3u;
    resolver.state.active = 1u;
    context.keyring = &keyring;
    context.resolve = resolve_token;
    context.resolve_context = &resolver;
    context.current_tick = 30u;
    context.slot_capacity = 8u;
    context.scope_id = 88u;
    context.policy_version = 3u;
    authority = (nx_buffer_authority){principal, 12u,
                                      nx_buffer_capability_authorize, &context};
    CHECK(nx_buffer_create(&manager, &authority, length64, 64u,
                           NX_BUFFER_FLAG_ZERO_ON_ALLOC, &buffer, &result) ==
          NX_STATUS_OK);

    claims.virtual_slot_index = 2u;
    claims.slot_generation = buffer.generation;
    claims.rights_mask = NX_BUFFER_RIGHT_READ | NX_BUFFER_RIGHT_WRITE |
                         NX_BUFFER_RIGHT_MAP | NX_BUFFER_RIGHT_DESTROY;
    claims.nonce = 2u;
    CHECK(issue_token(&keyring, &claims, &context, token) == 0);
    resolver.index = 2u;
    resolver.state.object = buffer;
    resolver.state.generation = buffer.generation;
    resolver.state.rights_ceiling = claims.rights_mask;
    context.current_tick = claims.expiry_tick;
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u,
                            length64, NX_BUFFER_RIGHT_WRITE,
                            &view, &result) == NX_STATUS_ACCESS_DENIED);
    context.current_tick = 30u;
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u,
                            length64, NX_BUFFER_RIGHT_WRITE,
                            &view, &result) == NX_STATUS_OK);
    CHECK(view.write_bytes.size == arena_size);
    CHECK(view.write_bytes.data != NULL);
    mapped = (volatile uint8_t *)view.write_bytes.data;
    bit_count = length64 * UINT64_C(8);
    CHECK(nx_monotonic_time_ns(&begin) == NX_STATUS_OK);
    cycle_known = nx_cycle_counter_init(&cycle_counter) == NX_STATUS_OK &&
                  nx_cycle_counter_read(&cycle_counter, &cycle_begin) == NX_STATUS_OK;
    for (i = 0u; (uint64_t)i < operations; ++i) {
        size_t byte = (size_t)(bit >> 3u);
        uint8_t mask = (uint8_t)(1u << (bit & 7u));
        mapped[byte] ^= mask;
        bit++;
        if (bit == bit_count)
            bit = 0u;
    }
    if (cycle_known &&
        nx_cycle_counter_read(&cycle_counter, &cycle_end) == NX_STATUS_OK &&
        cycle_begin.auxiliary == cycle_end.auxiliary &&
        cycle_begin.source == cycle_end.source &&
        cycle_end.value >= cycle_begin.value) {
        cycles_updates = cycle_end.value - cycle_begin.value;
    } else {
        cycle_known = 0;
    }
    CHECK(nx_monotonic_time_ns(&after_updates) == NX_STATUS_OK);
    CHECK(nx_buffer_view_commit(&manager, &authority, &view,
                                 length64, &result) == NX_STATUS_OK);
    CHECK(nx_monotonic_time_ns(&after_commit) == NX_STATUS_OK);
    CHECK(nx_buffer_verify(&manager, &authority, buffer,
                            &result) == NX_STATUS_OK);
    CHECK(nx_buffer_acquire(&manager, &authority, buffer, 0u,
                            length64, NX_BUFFER_RIGHT_READ,
                            &view, &result) == NX_STATUS_OK);
    CHECK(verify_lease_bytes(view.read_bytes.data,
                              view.read_bytes.size, operations) == 0);
    CHECK(nx_buffer_view_release(&manager, &authority, &view,
                                  &result) == NX_STATUS_OK);
    CHECK(nx_buffer_destroy(&manager, &authority, buffer,
                             &result) == NX_STATUS_OK);
    CHECK(nx_buffer_manager_validate(&manager, &result) == NX_STATUS_OK);
    CHECK(nx_buffer_stats_read(&manager, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.active_buffers == 0u && stats.active_leases == 0u &&
          stats.allocated_bytes == 0u);
    for (i = 0u; i < arena_size; ++i)
        CHECK(arena[i] == 0u);
    printf("NXOLMO real signed-capability zero-copy batch: updates=%" PRIu64
           " one_commit=1 bytes=%zu transforms_seconds=%.6f"
           " Gbit_updates_per_s=%.6f commit_seconds=%.6f PASS\n",
           operations, arena_size,
           (double)(after_updates - begin) / 1e9,
           (double)operations / (double)(after_updates - begin),
           (double)(after_commit - after_updates) / 1e9);
    if (cycle_known && operations != 0u)
        printf("NXOLMO_MASK_REAL_CPU_CYCLES known=1 batch_cycles=%" PRIu64
               " mean_cycles_per_mask=%.6f "
               "individual_percentiles=not_sampled\n",
               cycles_updates, (double)cycles_updates / operations);
    else
        puts("NXOLMO_MASK_REAL_CPU_CYCLES known=0 "
             "reason=provider_unavailable_or_cpu_migration");
    free(bitmap);
    free(arena);
    return 0;
}
