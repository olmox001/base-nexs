/* Actual NEXS allocator proof with the 4 KiB provider test profile.
 * The tested API must return NULL on exhausted buddy without exiting,
 * then admit new requests after an owned exact release. No mocks/asserts.
 */
#include "nexs_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned tests = 0U;
static unsigned failures = 0U;

static void verify(int valid, const char *label) {
  ++tests;
  if (!valid) {
    ++failures;
    fprintf(stderr, "RECOVERABLE_ALLOC_FAIL %s\n", label);
  }
}

int main(void) {
  void *allocations[24] = {0};
  size_t admitted = 0U;
  verify(POOL_SIZE == (4U * 1024U), "test profile exactly 4 KiB");
  for (size_t i = 0U; i < 24U; ++i) {
    void *p = nexs_alloc(512U);
    allocations[i] = p;
    if (p != NULL) {
      ++admitted;
      verify(((uintptr_t)p % MIN_BLOCK) == 0U,
             "buddy allocation has minimum architecture alignment");
      verify(memcmp(p, (unsigned char[512]){0}, 512U) == 0,
             "new allocation zero initialized");
      memset(p, 0x3e, 512U);
    } else {
      verify(i >= 8U, "OOM only after actual provider capacity consumed");
    }
  }
  verify(admitted == 8U, "exactly eight 512 byte buddy allocations");
  verify(nexs_alloc(512U) == NULL, "provider OOM recoverable");
  for (size_t i = 0U; i < 24U; ++i) {
    if (allocations[i] != NULL)
      nexs_free(allocations[i], 512U);
  }
  void *zero = nexs_alloc(0U);
  verify(zero != NULL, "zero length uses a minimal admitted block");
  if (zero != NULL)
    nexs_free(zero, 0U);
  /* Do not lose a freed block on the failure path. */
  void *resumed = nexs_alloc(512U);
  verify(resumed != NULL, "admission recovers after free");
  if (resumed != NULL)
    nexs_free(resumed, 512U);
  verify(nexs_alloc(SIZE_MAX) == NULL, "large arithmetic rejects overflow");
  fprintf(stdout, "RECOVERABLE_ALLOC_CORE %s checks=%u failures=%u\n",
          failures == 0 ? "PASS" : "FAIL", tests, failures);
  return failures == 0 ? 0 : 1;
}
