#ifndef KERNEL_SYS_WAIT_H
#define KERNEL_SYS_WAIT_H
typedef int pid_t;
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#endif
