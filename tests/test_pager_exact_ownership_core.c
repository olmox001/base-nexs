/* Actual production pager+registry+buddy tests. No mock or assert().
 * Only the current hosted provider is exercised here; freestanding builds
 * are independently compiled/booted. This is a P1 partial invariant suite,
 * not a per-runtime NXOLMO buffer/lease implementation.
 */
#include "nexs_alloc.h"
#include "nexs_registry.h"

#include <stdint.h>
#include <stdio.h>

static unsigned checks = 0U;
static unsigned failed = 0U;

static void check(int condition, const char *name) {
  ++checks;
  if (!condition) {
    ++failed;
    fprintf(stderr, "PAGER_INTEGRITY_FAIL %s\n", name);
  }
}

static void test_exact_span_and_double_release(void) {
  unsigned char *p = (unsigned char *)page_alloc(2U);
  check(p != NULL, "2-page allocation succeeds");
  if (p == NULL)
    return;
  p[0] = 0x31U;
  p[NEXS_PAGE_SIZE] = 0x7aU;
  check(is_page_ptr(p) == 1, "live base recognized");
  check(is_page_ptr(p + 1U) == 0, "interior pointer is not a base");
  check(page_free_checked(p, 1U) != 0, "wrong page count rejected");
  check(is_page_ptr(p) == 1, "failed free retains ownership");
  check(p[0] == 0x31U && p[NEXS_PAGE_SIZE] == 0x7aU,
        "wrong-sized free did not destroy either page");
  check(page_free_checked(p, 2U) == 0, "exact span retires");
  check(is_page_ptr(p) == 0, "retired address not live");
  check(page_free_checked(p, 2U) != 0, "double free rejected");
}

static void test_invalid_address_and_overflow(void) {
  check(page_alloc(0U) == NULL, "zero pages rejected");
  check(page_alloc(SIZE_MAX) == NULL, "page multiplication overflow");
  check(page_free_checked(NULL, 1U) != 0, "NULL release rejected");
  check(page_free_checked((void *)(uintptr_t)0x1234U, 1U) != 0,
        "unowned pointer cannot be unmapped");
  check(nexs_alloc(SIZE_MAX) == NULL, "byte rounding overflow");
}

static void test_legacy_free_wrong_size_preserves_data(void) {
  size_t bytes = LARGE_ALLOC_THRESH + 1U;
  unsigned char *p = (unsigned char *)nexs_alloc(bytes);
  check(p != NULL, "large allocation returns recoverably");
  if (p == NULL)
    return;
  p[bytes - 1U] = 0xa5U;
  nexs_free(p, bytes + NEXS_PAGE_SIZE);
  check(is_page_ptr(p) == 1, "incorrect nexs_free size keeps lease");
  check(p[bytes - 1U] == 0xa5U, "incorrect release never unmaps");
  nexs_free(p, bytes);
  check(is_page_ptr(p) == 0, "correct nexs_free size releases");
}

static void test_hosted_virtual_page_is_not_forged_physical_object(void) {
  unsigned char *p = (unsigned char *)page_alloc(1U);
  check(p != NULL, "hosted virtual page admitted");
  if (p == NULL)
    return;
  char path[128];
  int count = snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/phys",
                       (unsigned long long)(uintptr_t)p);
  check(count > 0 && (size_t)count < sizeof(path),
        "hosted virtual page identity path representable");
  check(reg_lookup(path) == NULL,
        "hosted mmap pointer never claims a physical frame in NXREG");
  count = snprintf(path, sizeof(path), "/mem/virt/0/0x%llx/flags",
                   (unsigned long long)(uintptr_t)p);
  check(count > 0 && (size_t)count < sizeof(path),
        "hosted virtual page flags path representable");
  check(reg_lookup(path) == NULL,
        "hosted mmap pointer never claims MMU permissions in NXREG");
  check(page_free_checked(p, 1U) == 0,
        "hosted virtual page released by exact provider ownership");
}

static void test_page_slot_reuse(void) {
  for (size_t i = 0U; i < 600U; ++i) {
    void *p = page_alloc(1U);
    check(p != NULL, "repeated short-lived single page admitted");
    if (p == NULL)
      return;
    check(page_free_checked(p, 1U) == 0, "reusable slot freed exactly");
  }
}

int main(void) {
  reg_init();
  test_exact_span_and_double_release();
  test_invalid_address_and_overflow();
  test_legacy_free_wrong_size_preserves_data();
  test_hosted_virtual_page_is_not_forged_physical_object();
  test_page_slot_reuse();
  fprintf(stdout, "PAGER_OWNERSHIP_CORE %s checks=%u failures=%u\n",
          failed == 0 ? "PASS" : "FAIL", checks, failed);
  return failed == 0 ? 0 : 1;
}
