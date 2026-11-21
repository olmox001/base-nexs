/* Baremetal and NexsOS1 do not provide a POSIX pipe transport.
 * Legacy pipe conversion must return UNSUPPORTED, never claim a
 * working shared endpoint or issue a new syscall. REG/console
 * resources are provided by the actual kernel/USERKIT object APIs. */
#include "../../registry/include/nexs_ipc_transport.h"

int nexs_ipc_pipe_open(int ends[2]) {
  if (ends != 0) {
    ends[0] = -1;
    ends[1] = -1;
  }
  return -1;
}

void nexs_ipc_pipe_abort(int ends[2]) {
  if (ends != 0) {
    ends[0] = -1;
    ends[1] = -1;
  }
}
