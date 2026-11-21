/* Executed on the ACTUAL baremetal libc stub in QEMU on each ISA.
 * A one-byte overrun cannot be hidden by a host libc or a mocked buffer.
 * Two independent short-source canaries plus truncation/zero-length cases.
 */
#include <stddef.h>
#include <string.h>

int nexs_test_freestanding_strncpy(void) {
    int errors = 0;
    char first[6] = {'X', 'X', 'X', 'X', 'X', 'X'};
    char *returned = strncpy(first, "A", 4u);
    if (returned != first || first[0] != 'A' ||
        first[1] != '\0' || first[2] != '\0' ||
        first[3] != '\0' || first[4] != 'X' || first[5] != 'X')
        errors++;

    char empty[7] = {'Z', 'Z', 'Z', 'Z', 'Z', 'Z', 'Z'};
    returned = strncpy(empty, "", 5u);
    if (returned != empty || empty[0] != '\0' ||
        empty[1] != '\0' || empty[2] != '\0' ||
        empty[3] != '\0' || empty[4] != '\0' ||
        empty[5] != 'Z' || empty[6] != 'Z')
        errors++;

    char truncated[5] = {'?', '?', '?', '?', '?'};
    returned = strncpy(truncated, "ABCD", 2u);
    if (returned != truncated || truncated[0] != 'A' ||
        truncated[1] != 'B' || truncated[2] != '?')
        errors++;

    char unchanged[3] = {'L', 'M', 'N'};
    returned = strncpy(unchanged, "anything", 0u);
    if (returned != unchanged || unchanged[0] != 'L' ||
        unchanged[1] != 'M' || unchanged[2] != 'N')
        errors++;

    return errors;
}
