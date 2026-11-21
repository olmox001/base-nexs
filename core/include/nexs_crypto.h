/* Canonical product-owned cryptographic integrity ABI.
 * Algorithms ported from pinned NXOLMO reference, retaining function
 * semantics and NXOLMO ABI names. One provider shared by registry, memory,
 * transport and HCD consumers; no second checksum implementation.
 * SHA/HMAC software path is target-independent (macOS/Linux/OS1/guest).
 */
#ifndef NEXS_CRYPTO_H
#define NEXS_CRYPTO_H
#include "nexs_checked.h"
#include "nexs_types.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NX_SHA256_CONTEXT_MAGIC UINT32_C(0x4e585332)
typedef struct nx_sha256_context {
    uint32_t state[8];
    uint64_t total_bytes;
    size_t buffered_bytes;
    uint32_t magic;
    uint32_t reserved;
    uint8_t block[64];
} nx_sha256_context;
nx_status nx_crc32c(uint32_t seed, nx_const_span input, uint32_t *checksum);
nx_status nx_crc64_ecma(nx_const_span input, uint64_t *checksum);
nx_status nx_crc64_ecma_bitwise(nx_const_span input, uint64_t *checksum);
void nx_sha256_init(nx_sha256_context *context);
nx_status nx_sha256_update(nx_sha256_context *context, nx_const_span input);
nx_status nx_sha256_final(nx_sha256_context *context, nx_digest256 *digest);
nx_status nx_sha256_compute(nx_const_span input, nx_digest256 *digest);
nx_status nx_hmac_sha256(nx_const_span key, nx_const_span input,
                         nx_digest256 *digest);
int nx_crypto_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                  size_t length);
#ifdef __cplusplus
}
#endif
#endif
