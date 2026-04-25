#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
#define SIG_IGN ((void (*)(int))1)
int kill(int pid, int sig);
void (*signal(int signum, void (*handler)(int)))(int);
#endif
