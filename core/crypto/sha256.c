/* Canonical Base-NEXS crypto port, NXOLMO origin SHA256 and HMAC.
 * One software implementation on all targets. Uses canonical nx_status,
 * nx_const_span, nx_digest256. No host-specific crypto dependency.
 */
#include "nexs_crypto.h"



#include <string.h>



static void nx_sha256_secure_zero(void *memory, size_t size) {
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    while (size-- != 0u)
        *cursor++ = 0u;
}

static int nx_sha256_context_valid(const nx_sha256_context *context) {
    return context != NULL &&
           ((uintptr_t)context % (uintptr_t)_Alignof(nx_sha256_context)) == 0u &&
           context->magic == NX_SHA256_CONTEXT_MAGIC &&
           context->reserved == 0u &&
           context->buffered_bytes < sizeof(context->block) &&
           context->total_bytes <= UINT64_MAX / 8u &&
           context->buffered_bytes == (size_t)(context->total_bytes & 63u);
}

static const uint32_t nx_sha256_round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static uint32_t nx_rotr32(uint32_t value, unsigned int shift) {
    return (value >> shift) | (value << (32u - shift));
}

static uint32_t nx_read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static void nx_write_u32_be(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static void nx_write_u64_be(uint8_t *bytes, uint64_t value) {
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        bytes[i] = (uint8_t)(value >> (56u - (i * 8u)));
}

static void nx_sha256_transform(nx_sha256_context *context,
                                const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int i;
    for (i = 0u; i < 16u; ++i)
        words[i] = nx_read_u32_be(block + (size_t)i * 4u);
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = nx_rotr32(words[i - 15u], 7u) ^
                      nx_rotr32(words[i - 15u], 18u) ^
                      (words[i - 15u] >> 3u);
        uint32_t s1 = nx_rotr32(words[i - 2u], 17u) ^
                      nx_rotr32(words[i - 2u], 19u) ^
                      (words[i - 2u] >> 10u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }
    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (i = 0u; i < 64u; ++i) {
        uint32_t sum1 = nx_rotr32(e, 6u) ^ nx_rotr32(e, 11u) ^ nx_rotr32(e, 25u);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choice + nx_sha256_round_constants[i] + words[i];
        uint32_t sum0 = nx_rotr32(a, 2u) ^ nx_rotr32(a, 13u) ^ nx_rotr32(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
    nx_sha256_secure_zero(words, sizeof(words));
}

void nx_sha256_init(nx_sha256_context *context) {
    if (context == NULL)
        return;
    context->state[0] = UINT32_C(0x6a09e667);
    context->state[1] = UINT32_C(0xbb67ae85);
    context->state[2] = UINT32_C(0x3c6ef372);
    context->state[3] = UINT32_C(0xa54ff53a);
    context->state[4] = UINT32_C(0x510e527f);
    context->state[5] = UINT32_C(0x9b05688c);
    context->state[6] = UINT32_C(0x1f83d9ab);
    context->state[7] = UINT32_C(0x5be0cd19);
    context->total_bytes = 0u;
    context->buffered_bytes = 0u;
    context->magic = NX_SHA256_CONTEXT_MAGIC;
    context->reserved = 0u;
    memset(context->block, 0, sizeof(context->block));
}

nx_status nx_sha256_update(nx_sha256_context *context, nx_const_span input) {
    size_t offset = 0u;
    if (!nx_sha256_context_valid(context) ||
        (input.data == NULL && input.size != 0u) ||
        nx_pointer_ranges_overlap(context, sizeof(*context),
                                  input.data, input.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)input.size > (UINT64_MAX / 8u) - context->total_bytes)
        return NX_STATUS_OVERFLOW;
    context->total_bytes += (uint64_t)input.size;
    while (offset < input.size) {
        size_t available = sizeof(context->block) - context->buffered_bytes;
        size_t remaining = input.size - offset;
        size_t take = remaining < available ? remaining : available;
        memcpy(context->block + context->buffered_bytes, input.data + offset, take);
        context->buffered_bytes += take;
        offset += take;
        if (context->buffered_bytes == sizeof(context->block)) {
            nx_sha256_transform(context, context->block);
            context->buffered_bytes = 0u;
        }
    }
    return NX_STATUS_OK;
}

nx_status nx_sha256_final(nx_sha256_context *context, nx_digest256 *digest) {
    uint64_t bit_length;
    unsigned int i;
    if (!nx_sha256_context_valid(context) || digest == NULL ||
        nx_pointer_ranges_overlap(context, sizeof(*context),
                                  digest, sizeof(*digest)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    bit_length = context->total_bytes * UINT64_C(8);
    context->block[context->buffered_bytes++] = UINT8_C(0x80);
    if (context->buffered_bytes > 56u) {
        memset(context->block + context->buffered_bytes, 0,
               sizeof(context->block) - context->buffered_bytes);
        nx_sha256_transform(context, context->block);
        context->buffered_bytes = 0u;
    }
    memset(context->block + context->buffered_bytes, 0,
           56u - context->buffered_bytes);
    nx_write_u64_be(context->block + 56u, bit_length);
    nx_sha256_transform(context, context->block);
    for (i = 0u; i < 8u; ++i)
        nx_write_u32_be(digest->bytes + (size_t)i * 4u, context->state[i]);
    nx_sha256_secure_zero(context, sizeof(*context));
    return NX_STATUS_OK;
}

nx_status nx_sha256_compute(nx_const_span input, nx_digest256 *digest) {
    if (digest == NULL || (input.data == NULL && input.size != 0u) ||
        nx_pointer_ranges_overlap(input.data, input.size,
                                  digest, sizeof(*digest)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;

    nx_sha256_context context;
    nx_status status;
    nx_sha256_init(&context);
    status = nx_sha256_update(&context, input);
    if (status != NX_STATUS_OK) {
        nx_sha256_secure_zero(&context, sizeof(context));
        return status;
    }
    status = nx_sha256_final(&context, digest);
    if (status != NX_STATUS_OK)
        nx_sha256_secure_zero(&context, sizeof(context));
    return status;

}

static nx_status nx_hmac_prepare_key(
    nx_const_span key, uint8_t normalized_key[64],
    nx_digest256 *key_digest) {
    if (key.size > 64u) {
        nx_status status = nx_sha256_compute(key, key_digest);
        if (status != NX_STATUS_OK)
            return status;
        memcpy(normalized_key, key_digest->bytes,
               sizeof(key_digest->bytes));
    } else if (key.size != 0u) {
        memcpy(normalized_key, key.data, key.size);
    }
    return NX_STATUS_OK;
}

static void nx_hmac_build_pads(const uint8_t normalized_key[64],
                               uint8_t inner_pad[64],
                               uint8_t outer_pad[64]) {
    size_t index;
    for (index = 0u; index < 64u; ++index) {
        inner_pad[index] = (uint8_t)(normalized_key[index] ^ UINT8_C(0x36));
        outer_pad[index] = (uint8_t)(normalized_key[index] ^ UINT8_C(0x5c));
    }
}

static nx_status nx_hmac_compute_inner(
    nx_sha256_context *context, const uint8_t inner_pad[64],
    nx_const_span input, nx_digest256 *inner_digest) {
    nx_status status;
    nx_sha256_init(context);
    status = nx_sha256_update(context, (nx_const_span){inner_pad, 64u});
    if (status == NX_STATUS_OK)
        status = nx_sha256_update(context, input);
    if (status == NX_STATUS_OK)
        status = nx_sha256_final(context, inner_digest);
    return status;
}

static nx_status nx_hmac_compute_outer(
    nx_sha256_context *context, const uint8_t outer_pad[64],
    const nx_digest256 *inner_digest, nx_digest256 *digest) {
    nx_status status;
    nx_sha256_init(context);
    status = nx_sha256_update(context, (nx_const_span){outer_pad, 64u});
    if (status == NX_STATUS_OK)
        status = nx_sha256_update(
            context, (nx_const_span){inner_digest->bytes,
                                     sizeof(inner_digest->bytes)});
    if (status == NX_STATUS_OK)
        status = nx_sha256_final(context, digest);
    return status;
}

nx_status nx_hmac_sha256(nx_const_span key, nx_const_span input,
                         nx_digest256 *digest) {
    if (digest == NULL || (key.data == NULL && key.size != 0u) ||
        (input.data == NULL && input.size != 0u) ||
        nx_pointer_ranges_overlap(digest, sizeof(*digest),
                                  key.data, key.size) != 0 ||
        nx_pointer_ranges_overlap(digest, sizeof(*digest),
                                  input.data, input.size) != 0)
        return NX_STATUS_INVALID_ARGUMENT;

    uint8_t normalized_key[64] = {0u};
    uint8_t inner_pad[64] = {0u};
    uint8_t outer_pad[64] = {0u};
    nx_digest256 key_digest = {{0u}};
    nx_digest256 inner_digest = {{0u}};
    nx_sha256_context context = {0};
    nx_status status = nx_hmac_prepare_key(
        key, normalized_key, &key_digest);
    if (status == NX_STATUS_OK) {
        nx_hmac_build_pads(normalized_key, inner_pad, outer_pad);
        status = nx_hmac_compute_inner(
            &context, inner_pad, input, &inner_digest);
    }
    if (status == NX_STATUS_OK)
        status = nx_hmac_compute_outer(
            &context, outer_pad, &inner_digest, digest);
    nx_sha256_secure_zero(normalized_key, sizeof(normalized_key));
    nx_sha256_secure_zero(inner_pad, sizeof(inner_pad));
    nx_sha256_secure_zero(outer_pad, sizeof(outer_pad));
    nx_sha256_secure_zero(&key_digest, sizeof(key_digest));
    nx_sha256_secure_zero(&inner_digest, sizeof(inner_digest));
    nx_sha256_secure_zero(&context, sizeof(context));
    return status;

}

int nx_crypto_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                  size_t length) {
    uint8_t difference = 0u;
    size_t i;
    if ((left == NULL || right == NULL) && length != 0u)
        return 0;
    for (i = 0u; i < length; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0u;
}
