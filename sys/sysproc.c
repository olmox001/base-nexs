/*
 * sys/sysproc.c — Plan 9-style Process Control Syscalls
 * ========================================================
 */

#include "include/nexs_sys.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* =========================================================
   SYSCALL IMPLEMENTATIONS
   ========================================================= */

int nexs_sleep(int msec) {
  if (msec <= 0) return 0;
  usleep((useconds_t)msec * 1000);
  return 0;
}

int nexs_exec(EvalCtx *ctx, const char *path) {
  if (!ctx || !path) return -1;
  EvalResult r = eval_file(ctx, path);
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

int nexs_alarm(int msec) {
  unsigned int secs = (msec > 0) ? (unsigned int)((msec + 999) / 1000) : 0;
  unsigned int old = alarm(secs);
  return (int)(old * 1000);
}

int nexs_rfork(int flags) {
  if (!(flags & NEXS_RFPROC)) return 0;
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) return 0;
  if (flags & NEXS_RFNOWAIT) signal(SIGCHLD, SIG_IGN);
  return (int)pid;
}

int nexs_await(char *buf, int nbuf) {
  if (!buf || nbuf <= 0) return -1;
  int status;
  pid_t pid = waitpid(-1, &status, 0);
  if (pid < 0) { strncpy(buf, "", (size_t)nbuf); return -1; }
  int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return snprintf(buf, (size_t)nbuf, "%d %d", (int)pid, exit_status);
}

/* =========================================================
   BUILT-IN WRAPPERS
   ========================================================= */

static Value bi_sleep(Value *args, int n) {
  if (n < 1) return val_err(4, "sleep: requires milliseconds");
  return val_int(nexs_sleep((int)val_to_int(&args[0])));
}

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

/* =========================================================
   REGISTRATION
   ========================================================= */

void sysproc_register_builtins(void) {
  reg_register_builtin("sleep",  bi_sleep);
  reg_register_builtin("exec",   bi_exec);
  reg_register_builtin("exits",  bi_exits);
  reg_register_builtin("alarm",  bi_alarm);
  reg_register_builtin("rfork",  bi_rfork);
  reg_register_builtin("await",  bi_await);
  reg_register_builtin("getpid", bi_getpid);
  reg_register_builtin("getwd",  bi_getwd);

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
