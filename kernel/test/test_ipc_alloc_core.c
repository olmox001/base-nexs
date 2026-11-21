/* Real NEXS kernel endpoint/notification owners + one selected Buddy/Pager.
 * Exhausts physical test-profile capacity without injecting fake failure.
 * Does not claim REG transport, cancellation or IPC authority qualification.
 */
#include "nexs_ipc.h"
#include "nexs_alloc.h"
#include <stdio.h>
#include <stddef.h>

static unsigned checks;
static unsigned failures;
static void check(int okay, const char *label) {
  ++checks;
  if (!okay) {
    ++failures;
    fprintf(stderr,"KERNEL_IPC_OWNER_FAIL %s\n",label);
  }
}
static void expect_reclaim(const char *name) {
  void *whole = buddy_alloc(POOL_SIZE);
  check(whole != NULL,name);
  if (whole != NULL)
    buddy_free(whole);
}
static void test_endpoint_ownership(void) {
  Endpoint *held[512] = {0};
  size_t count = 0U;
  check(ipc_endpoint_destroy(NULL) != 0,
        "reject nonowned endpoint release");
  while (count < sizeof(held)/sizeof(held[0])) {
    Endpoint *endpoint = ipc_endpoint_create();
    if (endpoint == NULL)
      break;
    held[count] = endpoint;
    ++count;
  }
  check(count >= 100U,
        "genuine endpoint reservations consume actual backing frames");
  check(ipc_endpoint_create() == NULL,
        "endpoint out-of-memory reported as NULL, never process-fatal");
  for (size_t i=0U; i<count; ++i)
    check(ipc_endpoint_destroy(held[i]) == 0,
          "empty endpoint release returns success");
  expect_reclaim("releasing all endpoints restores entire real 4KiB arena");
}
static void test_notification_ownership(void) {
  Notification *held[512] = {0};
  size_t count = 0U;
  check(ipc_notification_destroy(NULL) != 0,
        "reject nonowned notification release");
  while (count < sizeof(held)/sizeof(held[0])) {
    Notification *notification = ipc_notification_create();
    if (notification == NULL)
      break;
    held[count] = notification;
    ++count;
  }
  check(count >= 100U,
        "real notification reservations consume available backing");
  check(ipc_notification_create() == NULL,
        "notification OOM returns NULL without panic");
  for (size_t i=0U; i<count; ++i)
    check(ipc_notification_destroy(held[i]) == 0,
          "empty notification release returns success");
  expect_reclaim("all notifications return entire real provider arena");
}
int main(void) {
  check(POOL_SIZE == 4096U,"real 4KiB resource profile selected");
  test_endpoint_ownership();
  test_notification_ownership();
  printf("KERNEL_IPC_ALLOC_OWNER %s checks=%u failures=%u\n",
         failures == 0U ? "PASS":"FAIL",checks,failures);
  return failures == 0U ? 0 : 1;
}
