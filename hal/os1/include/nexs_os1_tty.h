/* Process-scoped NexsOS1 console capability and USERKIT tty client. */
#ifndef NEXS_OS1_TTY_H
#define NEXS_OS1_TTY_H

int nexs_os1_tty_is_terminal(void);
int nexs_os1_tty_begin(void);
void nexs_os1_tty_end(void);
/* timeout_ms = -1: wait for a key; >=0: bounded escape-sequence wait.
 * Returns one byte 0..255 or -1 on timeout/EOF/real error. */
int nexs_os1_tty_read(int timeout_ms);

#endif
