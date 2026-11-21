/* Ported and owned by Base-NEXS (NXOLMO cryptographic role).
 * Origin: pinned external/references/nxolmo/src/crypto.
 * Uses the existing canonical nexs_checked.h and nexs_types.h ABI; no
 * secondary types, source links, host library, or build-time profile.
 */
#include "nexs_crypto.h"

#include <stddef.h>

static const uint64_t nx_crc64_table[256] = {
#include "crc64_table.inc"
};

nx_status nx_crc64_ecma(nx_const_span input, uint64_t *checksum) {
    uint64_t state = 0u;
    size_t i;
    if (checksum == NULL || (input.data == NULL && input.size != 0u))
        return NX_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < input.size; ++i) {
        uint8_t index = (uint8_t)((state >> 56u) ^ input.data[i]);
        state = nx_crc64_table[index] ^ (state << 8u);
    }
    *checksum = state;
    return NX_STATUS_OK;
}

nx_status nx_crc64_ecma_bitwise(nx_const_span input, uint64_t *checksum) {
    uint64_t state = 0u;
    size_t i;
    if (checksum == NULL || (input.data == NULL && input.size != 0u))
        return NX_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < input.size; ++i) {
        unsigned int bit;
        state ^= (uint64_t)input.data[i] << 56u;
        for (bit = 0u; bit < 8u; ++bit) {
            if ((state & UINT64_C(0x8000000000000000)) != 0u)
                state = (state << 1u) ^ UINT64_C(0x42f0e1eba9ea3693);
            else
                state <<= 1u;
        }
    }
    *checksum = state;
    return NX_STATUS_OK;
}
