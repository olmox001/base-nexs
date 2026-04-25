#ifndef KERNEL_SYS_STAT_H
#define KERNEL_SYS_STAT_H
struct stat {
    long long st_size;
    unsigned int st_mode;
    long long st_mtime;
};
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISREG(m) (((m) & 0170000) == 0100000)
int stat(const char *path, struct stat *buf);
#endif
