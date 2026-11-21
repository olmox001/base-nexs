/* Platform provider seam: POSIX pipe setup never belongs in REG code.
 * Unsupported freestanding/OS1 providers must reject this legacy mode.
 * This is not the NXEC REG data-plane ABI. */
#ifndef NEXS_IPC_TRANSPORT_H
#define NEXS_IPC_TRANSPORT_H
/* Open the two pipe endpoints as one admitted, nonblocking hosted resource.
 * Neither endpoint is published on failure. */
int nexs_ipc_pipe_open(int ends[2]);
/* Dispose a tentative pipe during failed queue admission. */
void nexs_ipc_pipe_abort(int ends[2]);
#endif
