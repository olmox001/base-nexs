#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <poll.h>
/* Nexs includes for actual implementations */
#include "nexs_alloc.h"
#include "nexs_hal.h"
#include "nexs_utils.h"

static char dummy_stdout[16];
static char dummy_stderr[16];
static char dummy_stdin[16];

FILE* stdout = (FILE*)dummy_stdout;
FILE* stderr = (FILE*)dummy_stderr;
FILE* stdin  = (FILE*)dummy_stdin;

int errno = 0;

/* =========================================================
   Console Output / Formatting
   ========================================================= */

static void print_uint(char **buf, size_t *remain, unsigned long long val, int base) {
    char tmp[64];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    while (val) {
        int r = val % base;
        tmp[i++] = r < 10 ? '0' + r : 'a' + r - 10;
        val /= base;
    }
    while (i > 0) {
        if (*remain > 1) {
            *(*buf)++ = tmp[--i];
            (*remain)--;
        } else i--;
    }
}

static void print_int(char **buf, size_t *remain, long long val, int base) {
    if (val < 0) {
        if (*remain > 1) { *(*buf)++ = '-'; (*remain)--; }
        val = -val;
    }
    print_uint(buf, remain, (unsigned long long)val, base);
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) return 0;
    char *out = str;
    size_t remain = size;
    while (*format && remain > 1) {
        if (*format == '%') {
            format++;
            int is_long_long = 0;
            if (*format == 'l') {
                format++;
                if (*format == 'l') {
                    format++;
                    is_long_long = 1;
                }
            }
            if (*format == 'd') {
                if (is_long_long) {
                    print_int(&out, &remain, va_arg(ap, long long), 10);
                } else {
                    print_int(&out, &remain, va_arg(ap, int), 10);
                }
            } else if (*format == 'x') {
                if (is_long_long) {
                    print_uint(&out, &remain, va_arg(ap, unsigned long long), 16);
                } else {
                    print_uint(&out, &remain, va_arg(ap, unsigned int), 16);
                }
            } else if (*format == 's') {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && remain > 1) { *out++ = *s++; remain--; }
            } else if (*format == 'c') {
                *out++ = (char)va_arg(ap, int); remain--;
            } else {
                *out++ = *format; remain--;
            }
        } else {
            *out++ = *format; remain--;
        }
        format++;
    }
    *out = '\0';
    return out - str;
}

int snprintf(char* buffer, size_t count, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buffer, count, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char* buffer, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buffer, 4096, format, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    (void)stream;
    char buf[256];
    int ret = vsnprintf(buf, sizeof(buf), format, ap);
    nexs_hal_print(buf);
    return ret;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int printf(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stdout, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char* format, va_list va) {
    return vfprintf(stdout, format, va);
}

int fputc(int c, FILE* stream) { (void)stream; char s[2] = {(char)c, 0}; nexs_hal_print(s); return 0; }
int fputs(const char* s, FILE* stream) { (void)stream; nexs_hal_print(s); return 0; }
int fflush(FILE* stream) { (void)stream; return 0; }

/* File I/O - purely stubs for baremetal */
FILE *fopen(const char *filename, const char *mode) { (void)filename; (void)mode; return NULL; }
int fclose(FILE *stream) { (void)stream; return -1; }
static char line_buf[512];
static int line_buf_pos = 0;
static int line_buf_len = 0;

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (stream == stdin) {
        char *cptr = (char *)ptr;
        size_t total = size * nmemb;
        size_t i = 0;
        
        while (i < total) {
            if (line_buf_pos >= line_buf_len) {
                line_buf_pos = 0;
                line_buf_len = 0;
                
                while (1) {
                    int ch = -1;
                    while (ch == -1) {
                        ch = nexs_hal_getc();
                    }
                    if (ch == '\r' || ch == '\n') {
                        line_buf[line_buf_len++] = '\n';
                        nexs_hal_print("\r\n");
                        break;
                    } else if (ch == 0x08 || ch == 0x7F) {
                        if (line_buf_len > 0) {
                            line_buf_len--;
                            nexs_hal_print("\b \b");
                        }
                    } else if (line_buf_len < (int)sizeof(line_buf) - 2) {
                        line_buf[line_buf_len++] = (char)ch;
                        char echo[2] = {(char)ch, 0};
                        nexs_hal_print(echo);
                    }
                }
            }
            if (line_buf_pos < line_buf_len) {
                cptr[i++] = line_buf[line_buf_pos++];
            }
        }
        return nmemb;
    }
    (void)ptr; (void)size; (void)nmemb; (void)stream;
    return 0;
}
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
int fseek(FILE *stream, long offset, int whence) { (void)stream; (void)offset; (void)whence; return -1; }
long ftell(FILE *stream) { (void)stream; return -1; }
int remove(const char *filename) { (void)filename; return -1; }
FILE *fdopen(int fd, const char *mode) { (void)fd; (void)mode; return NULL; }
char *fgets(char *str, int n, FILE *stream) {
    if (n <= 0 || !str) return NULL;
    int i = 0;
    while (i < n - 1) {
        char ch;
        if (fread(&ch, 1, 1, stream) != 1) {
            break;
        }
        str[i++] = ch;
        if (ch == '\n') {
            break;
        }
    }
    str[i] = '\0';
    if (i == 0) return NULL;
    return str;
}

/* =========================================================
   Memory Allocation
   ========================================================= */

void *malloc(size_t size) {
    size_t *ptr = nexs_alloc(size + sizeof(size_t));
    if (!ptr) return NULL;
    *ptr = size + sizeof(size_t);
    return (void *)(ptr + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    size_t *p = (size_t *)ptr - 1;
    nexs_free(p, *p);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    size_t *p = (size_t *)ptr - 1;
    size_t old_size = *p - sizeof(size_t);
    void *new_p = malloc(size);
    if (new_p) {
        memcpy(new_p, ptr, old_size < size ? old_size : size);
        free(ptr);
    }
    return new_p;
}

/* =========================================================
   Strings and Parsing
   ========================================================= */

void* memset(void* dest, int ch, size_t count) {
    unsigned char* p = dest;
    while (count--) *p++ = (unsigned char)ch;
    return dest;
}
void* memcpy(void* dest, const void* src, size_t count) {
    char* d = dest; const char* s = src;
    while (count--) *d++ = *s++;
    return dest;
}
void* memmove(void* dest, const void* src, size_t count) {
    char* d = dest; const char* s = src;
    if (d < s) while (count--) *d++ = *s++;
    else { d += count; s += count; while (count--) *--d = *--s; }
    return dest;
}
int memcmp(const void* lhs, const void* rhs, size_t count) {
    const unsigned char* p1 = lhs; const unsigned char* p2 = rhs;
    while (count--) { if (*p1 != *p2) return *p1 - *p2; p1++; p2++; }
    return 0;
}
size_t strlen(const char* str) { size_t l=0; while(*str++) l++; return l; }
char* strcpy(char* dest, const char* src) { char* d=dest; while((*d++ = *src++)); return dest; }
char* strncpy(char* dest, const char* src, size_t count) {
    char* d = dest;
    while (count && (*d++ = *src++)) count--;
    while (count--) *d++ = 0;
    return dest;
}
int strcmp(const char* lhs, const char* rhs) {
    while (*lhs && (*lhs == *rhs)) { lhs++; rhs++; }
    return *(unsigned char*)lhs - *(unsigned char*)rhs;
}
int strncmp(const char* lhs, const char* rhs, size_t count) {
    if (!count) return 0;
    while (--count && *lhs && *lhs == *rhs) { lhs++; rhs++; }
    return *(unsigned char*)lhs - *(unsigned char*)rhs;
}
char* strchr(const char* str, int ch) {
    while (*str && *str != (char)ch) str++;
    if (*str == (char)ch) return (char*)str;
    return NULL;
}
char* strrchr(const char* str, int ch) {
    char* ret = NULL;
    do { if (*str == (char)ch) ret = (char*)str; } while (*str++);
    return ret;
}
char* strstr(const char* haystack, const char* needle) {
    size_t n = strlen(needle);
    if (!n) return (char*)haystack;
    while (*haystack) {
        if (!memcmp(haystack, needle, n)) return (char*)haystack;
        haystack++;
    }
    return NULL;
}
char* strncat(char* dest, const char* src, size_t count) {
    char* p = dest;
    while (*p) p++;
    while (count-- && *src) *p++ = *src++;
    *p = '\0';
    return dest;
}
char* strtok(char* str, const char* delim) {
    static char* last = NULL;
    if (str == NULL) str = last;
    if (str == NULL) return NULL;
    while (*str && strchr(delim, *str)) str++;
    if (*str == '\0') { last = NULL; return NULL; }
    char* token = str;
    while (*str && !strchr(delim, *str)) str++;
    if (*str) { *str = '\0'; last = str + 1; }
    else { last = NULL; }
    return token;
}
char* strdup(const char* str) { (void)str; return NULL; }
char* strerror(int errnum) { (void)errnum; return "error"; }

long long atoll(const char *nptr) {
    long long res = 0;
    int sign = 1;
    while (isspace(*nptr)) nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;
    while (isdigit(*nptr)) { res = res * 10 + (*nptr - '0'); nptr++; }
    return res * sign;
}
double atof(const char *nptr) {
    double res = 0.0;
    double fract = 1.0;
    int sign = 1;
    while (isspace(*nptr)) nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;
    while (isdigit(*nptr)) { res = res * 10.0 + (*nptr - '0'); nptr++; }
    if (*nptr == '.') {
        nptr++;
        while (isdigit(*nptr)) {
            fract /= 10.0;
            res += (*nptr - '0') * fract;
            nptr++;
        }
    }
    return res * sign;
}
int atoi(const char *nptr) { return (int)atoll(nptr); }
int system(const char *command) { (void)command; return -1; }
void exit(int status) { (void)status; while(1); }

int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* =========================================================
   POSIX Stubs
   ========================================================= */

ssize_t read(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return -1; }
ssize_t write(int fd, const void *buf, size_t count) { (void)fd; (void)buf; (void)count; return -1; }
int close(int fd) { (void)fd; return -1; }
int pipe(int pipefd[2]) { (void)pipefd; return -1; }
int isatty(int fd) { (void)fd; return 0; }
int chdir(const char *path) { (void)path; return -1; }
int unlink(const char *pathname) { (void)pathname; return -1; }
int stat(const char *path, struct stat *buf) { (void)path; (void)buf; return -1; }
int kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int tcgetattr(int fd, struct termios *termios_p) { (void)fd; (void)termios_p; return -1; }
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) { (void)fd; (void)optional_actions; (void)termios_p; return -1; }
pid_t wait(int *wstatus) { (void)wstatus; return -1; }
pid_t waitpid(pid_t pid, int *wstatus, int options) { (void)pid; (void)wstatus; (void)options; return -1; }
int poll(struct pollfd *fds, unsigned int nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return -1; }
int usleep(useconds_t usec) { (void)usec; return 0; }
unsigned int alarm(unsigned int seconds) { (void)seconds; return 0; }
pid_t fork(void) { return -1; }
pid_t getpid(void) { return 1; }
char *getcwd(char *buf, size_t size) { (void)buf; (void)size; return NULL; }
void (*signal(int signum, void (*handler)(int)))(int) { (void)signum; (void)handler; return SIG_IGN; }
