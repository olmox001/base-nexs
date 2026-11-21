/* Canonical byte-oriented endian encoding. Full NXOLMO base/endian.h
 * functionality without the prohibited compile-time assert macros.
 * Callers MUST validate their buffer spans before invoking these inlines.
 * Never cast a wire buffer to a native struct or serialize raw pointers. */
#ifndef NEXS_ENDIAN_H
#define NEXS_ENDIAN_H
#include <stdint.h>
#include <stddef.h>

static inline uint16_t nx_load_le16(const uint8_t *b) {
  return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}
static inline uint32_t nx_load_le32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) |
         ((uint32_t)b[2] << 16U) | ((uint32_t)b[3] << 24U);
}
static inline uint64_t nx_load_le64(const uint8_t *b) {
  return (uint64_t)nx_load_le32(b) |
         ((uint64_t)nx_load_le32(b + 4U) << 32U);
}
static inline uint16_t nx_load_be16(const uint8_t *b) {
  return (uint16_t)(((uint16_t)b[0] << 8U) | (uint16_t)b[1]);
}
static inline uint32_t nx_load_be32(const uint8_t *b) {
  return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
         ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}
static inline uint64_t nx_load_be64(const uint8_t *b) {
  return ((uint64_t)nx_load_be32(b) << 32U) |
         (uint64_t)nx_load_be32(b + 4U);
}
static inline void nx_store_le16(uint8_t *b, uint16_t v) {
  b[0] = (uint8_t)v;
  b[1] = (uint8_t)(v >> 8U);
}
static inline void nx_store_le32(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8U);
  b[2] = (uint8_t)(v >> 16U); b[3] = (uint8_t)(v >> 24U);
}
static inline void nx_store_le64(uint8_t *b, uint64_t v) {
  nx_store_le32(b, (uint32_t)v);
  nx_store_le32(b + 4U, (uint32_t)(v >> 32U));
}
static inline void nx_store_be16(uint8_t *b, uint16_t v) {
  b[0] = (uint8_t)(v >> 8U);
  b[1] = (uint8_t)v;
}
static inline void nx_store_be32(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)(v >> 24U); b[1] = (uint8_t)(v >> 16U);
  b[2] = (uint8_t)(v >> 8U); b[3] = (uint8_t)v;
}
static inline void nx_store_be64(uint8_t *b, uint64_t v) {
  nx_store_be32(b, (uint32_t)(v >> 32U));
  nx_store_be32(b + 4U, (uint32_t)v);
}
#endif
