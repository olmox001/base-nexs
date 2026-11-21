/* Production Base-NEXS crypto against the original NXOLMO vectors
 * and additional CRC32C valid and invalid bounds tests.
 */
#include "nexs_crypto.h"


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

static int test_sha256_vectors(void) {
    static const uint8_t abc[] = "abc";
    static const uint8_t expected_abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const uint8_t expected_empty[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
    };
    nx_digest256 digest;
    nx_sha256_context context;
    CHECK(nx_sha256_compute((nx_const_span){abc, 3u}, &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected_abc, 32u) != 0);
    CHECK(nx_sha256_compute((nx_const_span){NULL, 0u}, &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected_empty, 32u) != 0);
    nx_sha256_init(&context);
    CHECK(nx_sha256_update(&context, (nx_const_span){abc, 1u}) == NX_STATUS_OK);
    CHECK(nx_sha256_update(&context, (nx_const_span){abc + 1, 2u}) == NX_STATUS_OK);
    CHECK(nx_sha256_final(&context, &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected_abc, 32u) != 0);
    return 0;
}

static int test_context_lifecycle_and_aliasing(void) {
    nx_sha256_context context;
    nx_digest256 digest;
    static const uint8_t payload[] = "alias-check";
    static const uint8_t key[] = "key-material";
    union context_digest_alias {
        nx_sha256_context context;
        nx_digest256 digest;
    } context_alias;
    union input_digest_alias {
        uint8_t input[32];
        nx_digest256 digest;
    } input_alias;
    union key_digest_alias {
        uint8_t key[32];
        nx_digest256 digest;
    } key_alias;

    memset(&context, 0, sizeof(context));
    CHECK(nx_sha256_update(&context, (nx_const_span){payload, sizeof(payload) - 1u}) ==
          NX_STATUS_INVALID_ARGUMENT);
    nx_sha256_init(&context);
    CHECK(nx_sha256_update(&context,
          (nx_const_span){context.block, 1u}) == NX_STATUS_INVALID_ARGUMENT);
    context.buffered_bytes = sizeof(context.block);
    CHECK(nx_sha256_update(&context, (nx_const_span){payload, 1u}) ==
          NX_STATUS_INVALID_ARGUMENT);

    nx_sha256_init(&context);
    CHECK(nx_sha256_update(&context,
          (nx_const_span){payload, sizeof(payload) - 1u}) == NX_STATUS_OK);
    CHECK(nx_sha256_final(&context, &digest) == NX_STATUS_OK);
    CHECK(nx_sha256_final(&context, &digest) == NX_STATUS_INVALID_ARGUMENT);

    nx_sha256_init(&context_alias.context);
    CHECK(nx_sha256_final(&context_alias.context, &context_alias.digest) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_sha256_final(&context_alias.context, &digest) == NX_STATUS_OK);

    memset(input_alias.input, 0x5a, sizeof(input_alias.input));
    CHECK(nx_sha256_compute((nx_const_span){input_alias.input,
                                            sizeof(input_alias.input)},
                            &input_alias.digest) == NX_STATUS_INVALID_ARGUMENT);
    memset(key_alias.key, 0x6b, sizeof(key_alias.key));
    CHECK(nx_hmac_sha256((nx_const_span){key_alias.key, sizeof(key_alias.key)},
                         (nx_const_span){payload, sizeof(payload) - 1u},
                         &key_alias.digest) == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_hmac_sha256((nx_const_span){key, sizeof(key) - 1u},
                         (nx_const_span){input_alias.input,
                                         sizeof(input_alias.input)},
                         &input_alias.digest) == NX_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_hmac_sha256_vectors(void) {
    static const uint8_t key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t data[] = "Hi There";
    static const uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    nx_digest256 digest;
    uint8_t altered[32];
    CHECK(nx_hmac_sha256((nx_const_span){key, sizeof(key)},
                         (nx_const_span){data, sizeof(data) - 1u},
                         &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected, 32u) != 0);
    CHECK(nx_crypto_constant_time_equal(expected, expected, 32u) != 0);
    memcpy(altered, digest.bytes, sizeof(altered));
    altered[31] ^= UINT8_C(0x01);
    CHECK(nx_crypto_constant_time_equal(expected, altered, 32u) == 0);
    return 0;
}

static int test_sha256_long_stream_and_hmac_long_key(void) {
    static const uint8_t expected_million_a[32] = {
        0xcd,0xc7,0x6e,0x5c,0x99,0x14,0xfb,0x92,0x81,0xa1,0xc7,0xe2,0x84,0xd7,0x3e,0x67,
        0xf1,0x80,0x9a,0x48,0xa4,0x97,0x20,0x0e,0x04,0x6d,0x39,0xcc,0xc7,0x11,0x2c,0xd0
    };
    static const uint8_t expected_hmac[32] = {
        0x60,0xe4,0x31,0x59,0x1e,0xe0,0xb6,0x7f,0x0d,0x8a,0x26,0xaa,0xcb,0xf5,0xb7,0x7f,
        0x8e,0x0b,0xc6,0x21,0x37,0x28,0xc5,0x14,0x05,0x46,0x04,0x0f,0x0e,0xe3,0x7f,0x54
    };
    uint8_t block[1000];
    uint8_t long_key[131];
    static const uint8_t long_data[] =
        "Test Using Larger Than Block-Size Key - Hash Key First";
    nx_sha256_context context;
    nx_digest256 digest;
    size_t i;
    memset(block, 'a', sizeof(block));
    nx_sha256_init(&context);
    for (i = 0u; i < 1000u; ++i)
        CHECK(nx_sha256_update(&context, (nx_const_span){block, sizeof(block)}) ==
              NX_STATUS_OK);
    CHECK(nx_sha256_final(&context, &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected_million_a, 32u) != 0);

    memset(long_key, 0xaa, sizeof(long_key));
    CHECK(nx_hmac_sha256((nx_const_span){long_key, sizeof(long_key)},
                         (nx_const_span){long_data, sizeof(long_data) - 1u},
                         &digest) == NX_STATUS_OK);
    CHECK(nx_crypto_constant_time_equal(digest.bytes, expected_hmac, 32u) != 0);
    return 0;
}

static int test_crc64_vectors_and_equivalence(void) {
    static const uint8_t vector[] = "123456789";
    uint8_t sample[513];
    uint64_t fast = 0u;
    uint64_t slow = 0u;
    size_t i;
    for (i = 0u; i < sizeof(sample); ++i)
        sample[i] = (uint8_t)(i * 37u);
    CHECK(nx_crc64_ecma((nx_const_span){vector, 9u}, &fast) == NX_STATUS_OK);
    CHECK(fast == UINT64_C(0x6c40df5f0b497347));
    CHECK(nx_crc64_ecma((nx_const_span){sample, sizeof(sample)}, &fast) ==
          NX_STATUS_OK);
    CHECK(nx_crc64_ecma_bitwise((nx_const_span){sample, sizeof(sample)}, &slow) ==
          NX_STATUS_OK);
    CHECK(fast == slow);
    return 0;
}

static int test_crc32c_vectors_and_invalid_inputs(void) {
    static const uint8_t payload[] = "123456789";
    uint32_t checksum = 0u;
    CHECK(nx_crc32c(UINT32_MAX, (nx_const_span){payload, sizeof(payload) - 1u},
                    &checksum) == NX_STATUS_OK);
    CHECK(checksum == UINT32_C(0x1cf96d7c));
    CHECK(nx_crc32c(0u, (nx_const_span){NULL, 1u}, &checksum)
          == NX_STATUS_INVALID_ARGUMENT);
    CHECK(nx_crc32c(0u, (nx_const_span){NULL, 0u}, NULL)
          == NX_STATUS_INVALID_ARGUMENT);
    return 0;
}
int main(void) {
    CHECK(test_crc32c_vectors_and_invalid_inputs() == 0);
    CHECK(test_sha256_vectors() == 0);
    CHECK(test_context_lifecycle_and_aliasing() == 0);
    CHECK(test_hmac_sha256_vectors() == 0);
    CHECK(test_sha256_long_stream_and_hmac_long_key() == 0);
    CHECK(test_crc64_vectors_and_equivalence() == 0);
    (void)puts("crypto SHA-256/HMAC/CRC64 tests: PASS");
    return 0;
}
