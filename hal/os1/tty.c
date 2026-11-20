/* NexsOS1 line-input driver: use the inherited, process-owned CONSOLE
 * capability (fd 0), not a global keyboard service, physical device,
 * independent IPC reader or a claimed focus/controller.
 *
 * nxttyd alone routes INPUT events to the session controller. OS1's CONSOLE
 * object read is nonblocking (-EAGAIN), unlike hosted POSIX read(0).
 * Use the existing capability-based OS1_object_read (not POSIX read(), which
 * blocks inside the syscall dispatcher) so ANSI escape-sequence timeouts
 * are real. Neither method touches the keyboard provider or global focus.
 *
 * The OS1 TTY contract is raw from the outset. termios in the SDK is a
 * compatibility façade; there is no second canonical input line discipline.
 */
#include "include/nexs_os1_tty.h"
#include <errno.h>
#include <stdint.h>
#include <os1.h>
#include <unistd.h>

int nexs_os1_tty_is_terminal(void) {
  return isatty(STDIN_FILENO);
}

int nexs_os1_tty_begin(void) {
  return nexs_os1_tty_is_terminal() ? 0 : -1;
}

void nexs_os1_tty_end(void) {
  /* Foreground ownership remains with nxttyd's controller. No raw state. */
}

int nexs_os1_tty_read(int timeout_ms) {
  if (timeout_ms < -1) return -1;
  /* One millisecond sleep quantum is provided by the existing SDK timer,
   * with bounded attempts even for an unbounded interactive session.
   */
  uint32_t attempts = timeout_ms < 0 ? UINT32_MAX
                     : (uint32_t)timeout_ms + UINT32_C(1);
  for (uint32_t i = 0U; i < attempts; ++i) {
    char byte = 0;
    long received = OS1_object_read(STDIN_FILENO, &byte, 1U);
    if (received == 1)
      return (int)(unsigned char)byte;
    if (received == 0)
      return -1;
    if (received != -EAGAIN && received != -EINTR) {
      OS1_report_error("nxscript.tty.read", (int)received);
      return -1;
    }
    if (i + UINT32_C(1) < attempts) OS1_sleep(1);
  }
  return -1;
}
