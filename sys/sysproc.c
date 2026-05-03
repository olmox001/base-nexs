/*
 * sys/sysproc.c — Plan 9-style Process Control Syscalls
 * ========================================================
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

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

#ifdef NEXS_BAREMETAL
#  include "../hal/include/nexs_hal.h"
#  include "../kernel/include/nexs_sched.h"
#endif

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

int nexs_sleep(int msec) {
  if (msec <= 0) return 0;
#ifndef NEXS_BAREMETAL
  usleep((useconds_t)msec * 1000);
#else
  int yields = msec / 10 + 1;
  for (int i = 0; i < yields; i++) sched_yield();
#endif
  return 0;
}

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
#ifdef NEXS_BAREMETAL
  (void)status;
  nexs_hal_halt();
#else
  if (!status || status[0] == '\0') exit(0);
  fprintf(stderr, "[exits] %s\n", status);
  exit(1);
#endif
}

#ifndef NEXS_BAREMETAL
int nexs_alarm(int msec) {
  unsigned int secs = (msec > 0) ? (unsigned int)((msec + 999) / 1000) : 0;
  unsigned int old = alarm(secs);
  return (int)(old * 1000);
}
#endif

int nexs_rfork(int flags) {
#ifdef NEXS_BAREMETAL
  (void)flags;
  return -1;  /* address-space fork unavailable; use exec() directly */
#else
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
#endif
}

int nexs_await(char *buf, int nbuf) {
  if (!buf || nbuf <= 0) return -1;
#ifdef NEXS_BAREMETAL
  strncpy(buf, "", (size_t)nbuf);
  return -1;
#else
  int status;
  pid_t pid = waitpid(-1, &status, 0);
  if (pid < 0) { strncpy(buf, "", (size_t)nbuf); return -1; }
  int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return snprintf(buf, (size_t)nbuf, "%d %d", (int)pid, exit_status);
#endif
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

#ifndef NEXS_BAREMETAL
static Value bi_alarm(Value *args, int n) {
  if (n < 1) return val_err(4, "alarm: requires milliseconds");
  return val_int(nexs_alarm((int)val_to_int(&args[0])));
}
#endif

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
#ifdef NEXS_BAREMETAL
  NexsProc *p = proc_current();
  return val_int(p ? (int64_t)p->pid : 1);
#else
  return val_int((int64_t)getpid());
#endif
}

static Value bi_getwd(Value *args, int n) {
  (void)args; (void)n;
#ifdef NEXS_BAREMETAL
  Value cwd = reg_get("/sys/vfs/cwd");
  if (cwd.type == TYPE_STR && cwd.data) return cwd;
  val_free(&cwd);
  return val_str("/");
#else
  char buf[REG_PATH_MAX];
  if (!getcwd(buf, sizeof(buf))) return val_err(4, "getwd: failed");
  return val_str(buf);
#endif
}

/* =========================================================
   REGISTRATION
   ========================================================= */

#ifdef NEXS_BAREMETAL
#include "../kernel/include/nexs_ipc.h"
#include "../kernel/include/nexs_cap.h"
extern Cap *cap_lookup(uint32_t slot);
extern uint64_t nexs_brk(uint64_t new_brk);
#endif

static Value bi_ipc_send(Value *args, int n) {
  if (n < 2) return val_err(4, "ipc_send: requires (cap, msg)");
  uint32_t slot = (uint32_t)val_to_int(&args[0]);
  Value msg = args[1];

#ifdef NEXS_BAREMETAL
  Cap *c = cap_lookup(slot);
  if (!c || c->type != CAP_ENDPOINT) return val_err(4, "ipc_send: invalid cap");
  if (!(c->rights & CAP_WRITE)) return val_err(4, "ipc_send: no write rights");
  int res = nexs_ipc_send((Endpoint *)c->obj, msg, c->badge);
  return val_int(res);
#else
  (void)slot; (void)msg;
  return val_err(4, "ipc_send: only available on baremetal");
#endif
}

static Value bi_ipc_recv(Value *args, int n) {
  if (n < 1) return val_err(4, "ipc_recv: requires (cap)");
  uint32_t slot = (uint32_t)val_to_int(&args[0]);

#ifdef NEXS_BAREMETAL
  Cap *c = cap_lookup(slot);
  if (!c || c->type != CAP_ENDPOINT) return val_err(4, "ipc_recv: invalid cap");
  if (!(c->rights & CAP_READ)) return val_err(4, "ipc_recv: no read rights");
  Value out_msg;
  uint32_t badge = 0;
  int res = nexs_ipc_recv((Endpoint *)c->obj, &out_msg, &badge);
  if (res < 0) return val_err(4, "ipc_recv failed");
  /* TODO: handle badge if needed by user */
  return out_msg;
#else
  (void)slot;
  return val_err(4, "ipc_recv: only available on baremetal");
#endif
}

#define SIG(s) s " \xe2\x86\x92 "

static Value bi_brk(Value *args, int n) {
  uint64_t addr = (n >= 1) ? (uint64_t)val_to_int(&args[0]) : 0;
#ifdef NEXS_BAREMETAL
  return val_int((int64_t)nexs_brk(addr));
#else
  /* Hosted stub: return dummy or use sbrk() if available */
  (void)addr;
  return val_int(0x40000000ULL);
#endif
}

static Value bi_ipc_signal(Value *args, int n) {
  if (n < 1) return val_err(4, "ipc_signal: requires (cap)");
  uint32_t slot = (uint32_t)val_to_int(&args[0]);
  uint32_t badge = (n >= 2) ? (uint32_t)val_to_int(&args[1]) : 1;
#ifdef NEXS_BAREMETAL
  Cap *c = cap_lookup(slot);
  if (!c || c->type != CAP_NOTIFICATION) return val_err(4, "ipc_signal: invalid cap");
  return val_int(nexs_ipc_signal((Notification *)c->obj, badge));
#else
  (void)slot; (void)badge;
  return val_err(4, "ipc_signal: baremetal only");
#endif
}

static Value bi_ipc_wait(Value *args, int n) {
  if (n < 1) return val_err(4, "ipc_wait: requires (cap)");
  uint32_t slot = (uint32_t)val_to_int(&args[0]);
#ifdef NEXS_BAREMETAL
  Cap *c = cap_lookup(slot);
  if (!c || c->type != CAP_NOTIFICATION) return val_err(4, "ipc_wait: invalid cap");
  uint32_t badge = 0;
  nexs_ipc_wait((Notification *)c->obj, &badge);
  return val_int((int64_t)badge);
#else
  (void)slot;
  return val_err(4, "ipc_wait: baremetal only");
#endif
}

static Value bi_ipc_create(Value *args, int n) {
  if (n < 1) return val_err(4, "ipc_create: requires type ('endpoint' or 'notification')");
  const char *type = (args[0].type == TYPE_STR) ? (char *)args[0].data : "";
#ifdef NEXS_BAREMETAL
  NexsProc *curr = proc_current();
  uint32_t slot = 0;
  while (slot < curr->cspace.size && curr->cspace.caps[slot].type != CAP_NONE) slot++;
  if (slot >= curr->cspace.size) return val_err(4, "ipc_create: cspace full");

  Cap c;
  memset(&c, 0, sizeof(c));
  if (strcmp(type, "endpoint") == 0) {
    c.type = CAP_ENDPOINT;
    c.obj  = ipc_endpoint_create();
    c.rights = CAP_READ | CAP_WRITE;
  } else if (strcmp(type, "notification") == 0) {
    c.type = CAP_NOTIFICATION;
    c.obj  = ipc_notification_create();
    c.rights = CAP_READ | CAP_WRITE;
  } else {
    return val_err(4, "ipc_create: unknown type");
  }

  extern int cap_insert(NexsProc *p, uint32_t slot, Cap c);
  cap_insert(curr, slot, c);
  return val_int(slot);
#else
  (void)type;
  return val_err(4, "ipc_create: baremetal only");
#endif
}

void sysproc_register_builtins(void) {
  fn_register_builtin_sig("ipc_create", bi_ipc_create,
    SIG("ipc_create(type str)") "slot int");
  fn_register_builtin_sig("ipc_signal", bi_ipc_signal,
    SIG("ipc_signal(cap int, badge int)") "int");
  fn_register_builtin_sig("ipc_wait",   bi_ipc_wait,
    SIG("ipc_wait(cap int)") "badge int");
  fn_register_builtin_sig("ipc_send", bi_ipc_send,
    SIG("ipc_send(cap int, msg any)") "int");
  fn_register_builtin_sig("ipc_recv", bi_ipc_recv,
    SIG("ipc_recv(cap int)") "any");
  fn_register_builtin_sig("brk",    bi_brk,
    SIG("brk(addr int)") "addr int");
  fn_register_builtin_sig("exec",   bi_exec,
    SIG("exec(path str)") "nil");
  fn_register_builtin_sig("exits",  bi_exits,
    SIG("exits(status str)") "nil");
  fn_register_builtin_sig("sleep",  bi_sleep,
    SIG("sleep(msec int)") "nil");
  fn_register_builtin_sig("rfork",  bi_rfork,
    SIG("rfork(flags int)") "pid int");
  fn_register_builtin_sig("await",  bi_await,
    SIG("await()") "str");
  fn_register_builtin_sig("getpid", bi_getpid,
    SIG("getpid()") "int");
  fn_register_builtin_sig("getwd",  bi_getwd,
    SIG("getwd()") "str");

#ifndef NEXS_BAREMETAL
  fn_register_builtin_sig("alarm",  bi_alarm,
    SIG("alarm(msec int)") "int");
#endif

  /* Store actual fn_table indices in /sys/<name> */
  {
#ifndef NEXS_BAREMETAL
    static const char *names[] = {
      "sleep","exec","exits","alarm","rfork","await","getpid","getwd","brk",
      "ipc_send","ipc_recv","ipc_create","ipc_signal","ipc_wait"
    };
    int count = 14;
#else
    static const char *names[] = {
      "sleep","exec","exits","rfork","await","getpid","getwd","brk",
      "ipc_send","ipc_recv","ipc_create","ipc_signal","ipc_wait"
    };
    int count = 13;
#endif
    char path[REG_PATH_MAX];
    for (int _i = 0; _i < count; _i++) {
      NexsFnDef *def = fn_lookup(names[_i]);
      if (def) {
        int idx = (int)(def - g_fn_table);
        snprintf(path, sizeof(path), "/sys/%s", names[_i]);
        reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
      }
    }
  }

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
