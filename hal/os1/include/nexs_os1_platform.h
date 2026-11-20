/* Native NexsOS1 userland platform contract.
 * Privileged CPU and device operations remain owned by the system HAL.
 */
#ifndef NEXS_OS1_PLATFORM_H
#define NEXS_OS1_PLATFORM_H

void nexs_os1_arch_fence(void);
const char *nexs_os1_arch_name(void);
void nexs_platform_register(void);

#endif
