#ifndef KERNEL_UNISTD_H
#define KERNEL_UNISTD_H
#include <stddef.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
typedef long ssize_t;
typedef unsigned int useconds_t;
typedef int pid_t;
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int pipe(int pipefd[2]);
int isatty(int fd);
int chdir(const char *path);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);
pid_t fork(void);
pid_t getpid(void);
char *getcwd(char *buf, size_t size);
int unlink(const char *pathname);
#endif
