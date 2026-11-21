/* Integration proof: Base-NEXS hosted runtime MUST use a REAL
 * monotonic clock provider; unsupported is not accepted on a host.
 */
#include "nexs_clock.h"
#include <stdio.h>
#include <stdint.h>
int main(void) {
    uint64_t first = 0U, second = 0U, resolution = 0U;
    if (nx_monotonic_time_ns(&first) != NX_STATUS_OK ||
        nx_monotonic_time_ns(&second) != NX_STATUS_OK ||
        nx_monotonic_clock_resolution_ns(&resolution) != NX_STATUS_OK ||
        first == NX_NANOSECONDS_UNKNOWN ||
        second < first || resolution == 0U ||
        resolution == NX_NANOSECONDS_UNKNOWN) {
        (void)fputs("hosted monotonic provider: FAIL\n", stderr);
        return 1;
    }
    (void)puts("hosted monotonic provider: PASS");
    return 0;
}
