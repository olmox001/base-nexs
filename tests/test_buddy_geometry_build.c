/* Independent compile-time admission test, NEVER part of the product.
 * This proves symbolic pool/tree shape for the SELECTED build profile
 * without project-banned _Static_assert in trusted production code. */
#include "nexs_common.h"
#if MIN_BLOCK <= 0
#error "MIN_BLOCK must be positive"
#endif
#if POOL_SIZE < MIN_BLOCK
#error "POOL_SIZE must admit one block"
#endif
#if (POOL_SIZE & (POOL_SIZE - 1)) != 0
#error "POOL_SIZE must be power-of-two"
#endif
#if (MIN_BLOCK & (MIN_BLOCK - 1)) != 0
#error "MIN_BLOCK must be power-of-two"
#endif
#if NUM_LEAVES <= 0
#error "Leaf count must be positive"
#endif
#if (NUM_LEAVES & (NUM_LEAVES - 1)) != 0
#error "Buddy leaf count must be power-of-two"
#endif
#if TREE_NODES != 2 * NUM_LEAVES - 1
#error "Buddy representation/tree dimension mismatch"
#endif
/* This is intentionally build-only; a successful syntax check is the proof. */
int buddy_geometry_compile_test(void) {
  return 0;
}
