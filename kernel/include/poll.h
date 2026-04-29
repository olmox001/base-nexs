#ifndef KERNEL_POLL_H
#define KERNEL_POLL_H
struct pollfd {
    int fd;
    short events;
    short revents;
};
#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
int poll(struct pollfd *fds, unsigned int nfds, int timeout);
#endif
