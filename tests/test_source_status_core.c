/* Real header contract, independent classification tests: two negatives
 * and two positive provider outcomes, no mocks and no production asserts. */
#include "nexs_source_status.h"
#include <stdio.h>

int main(void) {
    nx_status absent = nexs_source_status_from_errno(ENOENT);
    nx_status unavailable = nexs_source_status_from_errno(ENOSYS);
    nx_status denied = nexs_source_status_from_errno(EACCES);
    nx_status other = nexs_source_status_from_errno(0);
    if (absent != NX_STATUS_NOT_FOUND ||
        unavailable != NX_STATUS_NOT_SUPPORTED ||
        denied != NX_STATUS_ACCESS_DENIED ||
        other != NX_STATUS_IO_ERROR ||
        !nexs_source_embedded_admissible(absent) ||
        !nexs_source_embedded_admissible(unavailable) ||
        nexs_source_embedded_admissible(denied) ||
        nexs_source_embedded_admissible(other)) {
        fprintf(stderr, "NEXS_SOURCE_STATUS_CATALOG FAIL\n");
        return 1;
    }
    puts("NEXS_SOURCE_STATUS_CATALOG PASS");
    return 0;
}
