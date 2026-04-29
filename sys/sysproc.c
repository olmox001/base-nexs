/*
 * sys/sysproc.c — Plan 9-style Process Control Syscalls
 * ========================================================
 */

#include "include/nexs_sys.h"
#include "../registry/include/nexs_registry.h"
#include "../lang/include/nexs_fn.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * nexs_embedded_lookup — provided by standalone compiled binaries (generated
 * by compiler/codegen.c).  When running as the interactive interpreter this
 * always returns NULL so eval_file() falls through to fopen() as usual.
 * Declared as a weak symbol so both build paths link cleanly.
 */
__attribute__((weak)) const char *nexs_embedded_lookup(const char *path) {
  (void)path;
  return NULL;
}

/* =========================================================
   SYSCALL IMPLEMENTATIONS
   ========================================================= */

#ifndef NEXS_BAREMETAL
int nexs_sleep(int msec) {
  if (msec <= 0) return 0;
  usleep((useconds_t)msec * 1000);
  return 0;
}
#endif

int nexs_exec(EvalCtx *ctx, const char *path) {
  if (!ctx || !path) return -1;
  EvalResult r;
  /* Bug 2 fix: check embedded dep table before touching the filesystem.
   * In standalone compiled binaries nexs_embedded_lookup() returns the
   * inlined source; in the interpreter it is a weak no-op returning NULL. */
  const char *embedded_src = nexs_embedded_lookup(path);
  if (embedded_src) {
    r = eval_str(ctx, embedded_src);
  } else {
    r = eval_file(ctx, path);
  }
  if (r.sig == CTRL_ERR) {
    val_print(&r.ret_val, ctx->err);
    fprintf(ctx->err, "\n");
    val_free(&r.ret_val);
    return -1;
  }
  val_free(&r.ret_val);
  return 0;
}

void nexs_exits(const char *status) {
  if (!status || status[0] == '\0') exit(0);
  fprintf(stderr, "[exits] %s\n", status);
  exit(1);
}

#ifndef NEXS_BAREMETAL
int nexs_alarm(int msec) {
  unsigned int secs = (msec > 0) ? (unsigned int)((msec + 999) / 1000) : 0;
  unsigned int old = alarm(secs);
  return (int)(old * 1000);
}

int nexs_rfork(int flags) {
  if (!(flags & NEXS_RFPROC)) return 0;
  pid_t pid = fork();
  if (pid < 0) return -1;
  /* Bug 3 fix: after fork() the registry lives in two separate heap copies.
   * Activate the pipe-backed IPC transport so sendmessage/receivemessage
   * cross the process boundary correctly in hosted (macOS/Linux) mode. */
  if (pid > 0) {
    /* Parent: enable pipes and set up wait if needed */
    reg_ipc_enable_pipes();
    if (flags & NEXS_RFNOWAIT) signal(SIGCHLD, SIG_IGN);
    return (int)pid;
  }
  /* Child (pid == 0) */
  reg_ipc_enable_pipes();
  return 0;
}

int nexs_await(char *buf, int nbuf) {
  if (!buf || nbuf <= 0) return -1;
  int status;
  pid_t pid = waitpid(-1, &status, 0);
  if (pid < 0) { strncpy(buf, "", (size_t)nbuf); return -1; }
  int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return snprintf(buf, (size_t)nbuf, "%d %d", (int)pid, exit_status);
}
#endif

/* =========================================================
   BUILT-IN WRAPPERS
   ========================================================= */

#ifndef NEXS_BAREMETAL
static Value bi_sleep(Value *args, int n) {
  if (n < 1) return val_err(4, "sleep: requires milliseconds");
  return val_int(nexs_sleep((int)val_to_int(&args[0])));
}
#endif

static Value bi_exec(Value *args, int n) {
  if (n < 1) return val_err(4, "exec: requires a path");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "exec: argument must be a string");
  EvalCtx ctx;
  eval_ctx_init(&ctx);
  return val_int(nexs_exec(&ctx, (char *)args[0].data));
}

static Value bi_exits(Value *args, int n) {
  const char *status = NULL;
  if (n >= 1 && args[0].type == TYPE_STR && args[0].data)
    status = (char *)args[0].data;
  nexs_exits(status);
  return val_nil();
}

#ifndef NEXS_BAREMETAL
static Value bi_alarm(Value *args, int n) {
  if (n < 1) return val_err(4, "alarm: requires milliseconds");
  return val_int(nexs_alarm((int)val_to_int(&args[0])));
}

static Value bi_rfork(Value *args, int n) {
  int flags = (n >= 1) ? (int)val_to_int(&args[0]) : NEXS_RFPROC;
  return val_int(nexs_rfork(flags));
}

static Value bi_await(Value *args, int n) {
  (void)args; (void)n;
  char buf[256];
  int result = nexs_await(buf, sizeof(buf));
  if (result < 0) return val_err(4, "await: no children");
  return val_str(buf);
}

static Value bi_getpid(Value *args, int n) {
  (void)args; (void)n;
  return val_int((int64_t)getpid());
}

static Value bi_getwd(Value *args, int n) {
  (void)args; (void)n;
  char buf[REG_PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) return val_err(4, "getwd: failed");
  return val_str(buf);
}
#endif

/* =========================================================
   REGISTRATION
   ========================================================= */

#define SIG(s) s " \xe2\x86\x92 "

void sysproc_register_builtins(void) {
  fn_register_builtin_sig("exec",   bi_exec,
    SIG("exec(path str)") "nil");
  fn_register_builtin_sig("exits",  bi_exits,
    SIG("exits(status str)") "nil");

#ifndef NEXS_BAREMETAL
  fn_register_builtin_sig("sleep",  bi_sleep,
    SIG("sleep(msec int)") "nil");
  fn_register_builtin_sig("alarm",  bi_alarm,
    SIG("alarm(msec int)") "int");
  fn_register_builtin_sig("rfork",  bi_rfork,
    SIG("rfork(flags int)") "pid int");
  fn_register_builtin_sig("await",  bi_await,
    SIG("await()") "str");
  fn_register_builtin_sig("getpid", bi_getpid,
    SIG("getpid()") "int");
  fn_register_builtin_sig("getwd",  bi_getwd,
    SIG("getwd()") "str");

  /* Store actual fn_table indices in /sys/<name> */
  {
    static const char *names[] = {
      "sleep","exec","exits","alarm","rfork","await","getpid","getwd"
    };
    char path[REG_PATH_MAX];
    for (int _i = 0; _i < 8; _i++) {
      NexsFnDef *def = fn_lookup(names[_i]);
      if (def) {
        int idx = (int)(def - g_fn_table);
        snprintf(path, sizeof(path), "/sys/%s", names[_i]);
        reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
      }
    }
  }
#else
  /* Only register baremetal builtins */
  {
    static const char *names[] = { "exec", "exits" };
    char path[REG_PATH_MAX];
    for (int _i = 0; _i < 2; _i++) {
      NexsFnDef *def = fn_lookup(names[_i]);
      if (def) {
        int idx = (int)(def - g_fn_table);
        snprintf(path, sizeof(path), "/sys/%s", names[_i]);
        reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
      }
    }
  }
#endif

  reg_set("/sys/rfork/RFPROC",   val_int(NEXS_RFPROC),   RK_READ);
  reg_set("/sys/rfork/RFNOWAIT", val_int(NEXS_RFNOWAIT), RK_READ);
  reg_set("/sys/rfork/RFNAMEG",  val_int(NEXS_RFNAMEG),  RK_READ);
  reg_set("/sys/rfork/RFMEM",    val_int(NEXS_RFMEM),    RK_READ);
  reg_set("/sys/rfork/RFFDG",    val_int(NEXS_RFFDG),    RK_READ);

  reg_set("/sys/OREAD",  val_int(NEXS_OREAD),  RK_READ);
  reg_set("/sys/OWRITE", val_int(NEXS_OWRITE), RK_READ);
  reg_set("/sys/ORDWR",  val_int(NEXS_ORDWR),  RK_READ);
  reg_set("/sys/OTRUNC", val_int(NEXS_OTRUNC), RK_READ);
}
#undef SIG
