/* Guest-only entrypoint, linked with the ORIGINAL product libc stub.
 * It never executes in hosted binaries or shipping baremetal images. */
#include "nexs_hal.h"
int nexs_test_freestanding_strncpy(void);

void nexs_main_baremetal(void) {
    const int failures = nexs_test_freestanding_strncpy();
    if (failures == 0)
        nexs_hal_print("NEXS_STRNCPY_REAL_GUEST_PASS\n");
    else
        nexs_hal_print("NEXS_STRNCPY_REAL_GUEST_FAIL\n");
    nexs_hal_halt();
}
