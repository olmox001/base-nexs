/* Temporary SIGSEGV handler to print backtrace */
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static void crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "\n=== SIGNAL %d ===\n", sig);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

__attribute__((constructor))
static void install_crash_handler(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGABRT, crash_handler);
}
