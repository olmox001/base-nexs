#ifndef KERNEL_TERMIOS_H
#define KERNEL_TERMIOS_H
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_lflag;
    tcflag_t c_cflag;
    cc_t c_cc[NCCS];
};
#define ICRNL 0000400
#define IXON 0002000
#define BRKINT 0000002
#define ISTRIP 0000040
#define INPCK 0000020
#define ECHO 0000010
#define ICANON 0000002
#define IEXTEN 0100000
#define ISIG 0000001
#define CS8 0000060
#define VMIN 6
#define VTIME 5
#define TCSAFLUSH 2
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
#endif
