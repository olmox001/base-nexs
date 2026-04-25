/*
 * sys/sysio.c — Plan 9-style File I/O Syscalls
 * ===============================================
 */

#include "include/nexs_sys.h"
#include "../registry/include/nexs_registry.h"
#include "../lang/include/nexs_fn.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* =========================================================
   GLOBAL STATE
   ========================================================= */

NexsFd g_fd_table[NEXS_MAX_FDS];
static char g_errstr[256] = "";

/* =========================================================
   INITIALISATION
   ========================================================= */

void sysio_init(void) {
  memset(g_fd_table, 0, sizeof(g_fd_table));

  g_fd_table[0].fp = stdin;
  strncpy(g_fd_table[0].path, "/dev/stdin", REG_PATH_MAX - 1);
  g_fd_table[0].in_use = 1;
  g_fd_table[0].flags  = NEXS_OREAD;

  g_fd_table[1].fp = stdout;
  strncpy(g_fd_table[1].path, "/dev/stdout", REG_PATH_MAX - 1);
  g_fd_table[1].in_use = 1;
  g_fd_table[1].flags  = NEXS_OWRITE;

  g_fd_table[2].fp = stderr;
  strncpy(g_fd_table[2].path, "/dev/stderr", REG_PATH_MAX - 1);
  g_fd_table[2].in_use = 1;
  g_fd_table[2].flags  = NEXS_OWRITE;
}

/* =========================================================
   HELPERS
   ========================================================= */

static int fd_alloc(void) {
  for (int i = 3; i < NEXS_MAX_FDS; i++)
    if (!g_fd_table[i].in_use) return i;
  return -1;
}

static void set_errstr(const char *msg) {
  if (msg) strncpy(g_errstr, msg, sizeof(g_errstr) - 1);
  else g_errstr[0] = '\0';
  g_errstr[sizeof(g_errstr) - 1] = '\0';
}

/* =========================================================
   SYSCALLS
   ========================================================= */

int nexs_open(const char *path, int mode) {
  if (!path) { set_errstr("open: NULL path"); return -1; }
  int fd = fd_alloc();
  if (fd < 0) { set_errstr("open: fd table full"); return -1; }
  const char *fmode;
  int base = mode & 0x0F;
  int trunc = mode & NEXS_OTRUNC;
  switch (base) {
  case NEXS_OREAD:  fmode = "r"; break;
  case NEXS_OWRITE: fmode = trunc ? "w" : "a"; break;
  case NEXS_ORDWR:  fmode = trunc ? "w+" : "r+"; break;
  default: set_errstr("open: invalid mode"); return -1;
  }
  FILE *fp = fopen(path, fmode);
  if (!fp) { set_errstr("open: cannot open file"); return -1; }
  g_fd_table[fd].fp = fp;
  strncpy(g_fd_table[fd].path, path, REG_PATH_MAX - 1);
  g_fd_table[fd].path[REG_PATH_MAX - 1] = '\0';
  g_fd_table[fd].in_use = 1;
  g_fd_table[fd].flags  = mode;
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_set(regpath, val_str(path), RK_READ);
  return fd;
}

int nexs_create(const char *path, int mode, int perm) {
  (void)perm;
  if (!path) { set_errstr("create: NULL path"); return -1; }
  int fd = fd_alloc();
  if (fd < 0) { set_errstr("create: fd table full"); return -1; }
  const char *fmode;
  int base = mode & 0x0F;
  switch (base) {
  case NEXS_OREAD:  fmode = "w"; break;
  case NEXS_OWRITE: fmode = "w"; break;
  case NEXS_ORDWR:  fmode = "w+"; break;
  default:          fmode = "w";
  }
  FILE *fp = fopen(path, fmode);
  if (!fp) { set_errstr("create: cannot create file"); return -1; }
  g_fd_table[fd].fp = fp;
  strncpy(g_fd_table[fd].path, path, REG_PATH_MAX - 1);
  g_fd_table[fd].path[REG_PATH_MAX - 1] = '\0';
  g_fd_table[fd].in_use = 1;
  g_fd_table[fd].flags  = mode;
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_set(regpath, val_str(path), RK_READ);
  return fd;
}

int nexs_close(int fd) {
  if (fd < 0 || fd >= NEXS_MAX_FDS) { set_errstr("close: invalid fd"); return -1; }
  if (!g_fd_table[fd].in_use) { set_errstr("close: fd not open"); return -1; }
  if (fd >= 3 && g_fd_table[fd].fp) fclose(g_fd_table[fd].fp);
  g_fd_table[fd].fp = NULL;
  g_fd_table[fd].path[0] = '\0';
  g_fd_table[fd].in_use = 0;
  g_fd_table[fd].flags  = 0;
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", fd);
  reg_delete(regpath);
  return 0;
}

int nexs_pread(int fd, char *buf, int n, int64_t offset) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use)
    { set_errstr("pread: invalid fd"); return -1; }
  if (!buf || n <= 0) { set_errstr("pread: invalid params"); return -1; }
  FILE *fp = g_fd_table[fd].fp;
  if (!fp) { set_errstr("pread: NULL fp"); return -1; }
  if (offset >= 0 && fseek(fp, (long)offset, SEEK_SET) != 0)
    { set_errstr("pread: seek failed"); return -1; }
  return (int)fread(buf, 1, (size_t)n, fp);
}

int nexs_pwrite(int fd, const char *buf, int n, int64_t offset) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use)
    { set_errstr("pwrite: invalid fd"); return -1; }
  if (!buf || n <= 0) { set_errstr("pwrite: invalid params"); return -1; }
  FILE *fp = g_fd_table[fd].fp;
  if (!fp) { set_errstr("pwrite: NULL fp"); return -1; }
  if (offset >= 0 && fseek(fp, (long)offset, SEEK_SET) != 0)
    { set_errstr("pwrite: seek failed"); return -1; }
  size_t wr = fwrite(buf, 1, (size_t)n, fp);
  fflush(fp);
  return (int)wr;
}

int64_t nexs_seek(int fd, int64_t offset, int whence) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use)
    { set_errstr("seek: invalid fd"); return -1; }
  FILE *fp = g_fd_table[fd].fp;
  if (!fp) { set_errstr("seek: NULL fp"); return -1; }
  int w;
  switch (whence) {
  case 0: w = SEEK_SET; break;
  case 1: w = SEEK_CUR; break;
  case 2: w = SEEK_END; break;
  default: set_errstr("seek: invalid whence"); return -1;
  }
  if (fseek(fp, (long)offset, w) != 0) { set_errstr("seek: failed"); return -1; }
  return (int64_t)ftell(fp);
}

int nexs_dup(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= NEXS_MAX_FDS || !g_fd_table[oldfd].in_use)
    { set_errstr("dup: invalid oldfd"); return -1; }
  if (newfd < 0) newfd = fd_alloc();
  if (newfd < 0 || newfd >= NEXS_MAX_FDS) { set_errstr("dup: invalid newfd"); return -1; }
  if (g_fd_table[newfd].in_use && newfd >= 3) nexs_close(newfd);
  g_fd_table[newfd] = g_fd_table[oldfd];
  char regpath[REG_PATH_MAX];
  snprintf(regpath, sizeof(regpath), "/sys/fd/%d", newfd);
  reg_set(regpath, val_str(g_fd_table[newfd].path), RK_READ);
  return newfd;
}

int nexs_fd2path(int fd, char *buf, int nbuf) {
  if (fd < 0 || fd >= NEXS_MAX_FDS || !g_fd_table[fd].in_use)
    { set_errstr("fd2path: invalid fd"); return -1; }
  if (!buf || nbuf <= 0) { set_errstr("fd2path: invalid buf"); return -1; }
  strncpy(buf, g_fd_table[fd].path, (size_t)(nbuf - 1));
  buf[nbuf - 1] = '\0';
  return 0;
}

int nexs_remove(const char *path) {
  if (!path) { set_errstr("remove: NULL path"); return -1; }
  if (remove(path) != 0) { set_errstr("remove: failed"); return -1; }
  return 0;
}

int nexs_pipe(int fd[2]) {
  if (!fd) { set_errstr("pipe: NULL fd"); return -1; }
  int pipefd[2];
  if (pipe(pipefd) != 0) { set_errstr("pipe: creation failed"); return -1; }
  int fd0 = fd_alloc();
  if (fd0 < 0) { close(pipefd[0]); close(pipefd[1]); set_errstr("pipe: fd table full"); return -1; }
  g_fd_table[fd0].fp = fdopen(pipefd[0], "r");
  strncpy(g_fd_table[fd0].path, "/dev/pipe/r", REG_PATH_MAX - 1);
  g_fd_table[fd0].in_use = 1;
  g_fd_table[fd0].flags  = NEXS_OREAD;
  int fd1 = fd_alloc();
  if (fd1 < 0) {
    fclose(g_fd_table[fd0].fp);
    g_fd_table[fd0].in_use = 0;
    close(pipefd[1]);
    set_errstr("pipe: fd table full");
    return -1;
  }
  g_fd_table[fd1].fp = fdopen(pipefd[1], "w");
  strncpy(g_fd_table[fd1].path, "/dev/pipe/w", REG_PATH_MAX - 1);
  g_fd_table[fd1].in_use = 1;
  g_fd_table[fd1].flags  = NEXS_OWRITE;
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

Value nexs_stat(const char *path) {
  if (!path) return val_err(4, "stat: NULL path");
  struct stat sb;
  if (stat(path, &sb) != 0) return val_err(4, "stat: file not found");
  char buf[512];
  snprintf(buf, sizeof(buf),
           "path=%s size=%lld mode=%o mtime=%lld type=%s",
           path, (long long)sb.st_size, (unsigned int)(sb.st_mode & 0777),
           (long long)sb.st_mtime,
           S_ISDIR(sb.st_mode) ? "dir" : S_ISREG(sb.st_mode) ? "file" : "other");
  return val_str(buf);
}

int nexs_chdir(const char *path) {
  if (!path) { set_errstr("chdir: NULL path"); return -1; }
  if (chdir(path) != 0) { set_errstr("chdir: failed"); return -1; }
  return 0;
}

void nexs_errstr(char *buf, int nbuf) {
  if (!buf || nbuf <= 0) return;
  char tmp[256];
  strncpy(tmp, g_errstr, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  strncpy(g_errstr, buf, sizeof(g_errstr) - 1);
  g_errstr[sizeof(g_errstr) - 1] = '\0';
  strncpy(buf, tmp, (size_t)(nbuf - 1));
  buf[nbuf - 1] = '\0';
}

/* =========================================================
   BUILT-IN WRAPPERS
   ========================================================= */

static Value bi_open(Value *args, int n) {
  if (n < 1) return val_err(4, "open: requires at least 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "open: first argument must be a string");
  int mode = (n >= 2) ? (int)val_to_int(&args[1]) : NEXS_OREAD;
  int fd = nexs_open((char *)args[0].data, mode);
  if (fd < 0) return val_err(4, "open: failed");
  return val_int(fd);
}

static Value bi_create(Value *args, int n) {
  if (n < 1) return val_err(4, "create: requires at least 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "create: first argument must be a string");
  int mode = (n >= 2) ? (int)val_to_int(&args[1]) : NEXS_OWRITE;
  int perm = (n >= 3) ? (int)val_to_int(&args[2]) : 0644;
  int fd = nexs_create((char *)args[0].data, mode, perm);
  if (fd < 0) return val_err(4, "create: failed");
  return val_int(fd);
}

static Value bi_close(Value *args, int n) {
  if (n < 1) return val_err(4, "close: requires 1 argument");
  return val_int(nexs_close((int)val_to_int(&args[0])));
}

static Value bi_read(Value *args, int n) {
  if (n < 2) return val_err(4, "read: requires fd and nbytes");
  int fd = (int)val_to_int(&args[0]);
  int nbytes = (int)val_to_int(&args[1]);
  if (nbytes <= 0 || nbytes > MAX_STR_LEN - 1) nbytes = MAX_STR_LEN - 1;
  char buf[MAX_STR_LEN];
  int64_t offset = (n >= 3) ? val_to_int(&args[2]) : -1;
  int rd = nexs_pread(fd, buf, nbytes, offset);
  if (rd < 0) return val_err(4, "read: failed");
  buf[rd] = '\0';
  return val_str(buf);
}

static Value bi_write(Value *args, int n) {
  if (n < 2) return val_err(4, "write: requires fd and string");
  int fd = (int)val_to_int(&args[0]);
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "write: second argument must be a string");
  const char *data = (const char *)args[1].data;
  int len = (int)strlen(data);
  int64_t offset = (n >= 3) ? val_to_int(&args[2]) : -1;
  return val_int(nexs_pwrite(fd, data, len, offset));
}

static Value bi_seek(Value *args, int n) {
  if (n < 2) return val_err(4, "seek: requires fd and offset");
  int fd = (int)val_to_int(&args[0]);
  int64_t offset = val_to_int(&args[1]);
  int whence = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  return val_int(nexs_seek(fd, offset, whence));
}

static Value bi_dup(Value *args, int n) {
  if (n < 1) return val_err(4, "dup: requires at least 1 argument");
  int oldfd = (int)val_to_int(&args[0]);
  int newfd = (n >= 2) ? (int)val_to_int(&args[1]) : -1;
  return val_int(nexs_dup(oldfd, newfd));
}

static Value bi_fd2path(Value *args, int n) {
  if (n < 1) return val_err(4, "fd2path: requires 1 argument");
  int fd = (int)val_to_int(&args[0]);
  char buf[REG_PATH_MAX];
  if (nexs_fd2path(fd, buf, sizeof(buf)) < 0)
    return val_err(4, "fd2path: failed");
  return val_str(buf);
}

static Value bi_remove(Value *args, int n) {
  if (n < 1) return val_err(4, "remove: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "remove: argument must be a string");
  return val_int(nexs_remove((char *)args[0].data));
}

static Value bi_pipe(Value *args, int n) {
  (void)args; (void)n;
  int fds[2];
  if (nexs_pipe(fds) < 0) return val_err(4, "pipe: failed");
  DynArray *arr = arr_get_or_create("__pipe__");
  arr_set(arr, 0, val_int(fds[0]));
  arr_set(arr, 1, val_int(fds[1]));
  Value v;
  v.type = TYPE_ARR; v.data = arr; v.ival = 0;
  v.fval = 0; v.err_code = 0; v.err_msg = NULL;
  return v;
}

static Value bi_fstat(Value *args, int n) {
  if (n < 1) return val_err(4, "stat: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "stat: argument must be a string");
  return nexs_stat((char *)args[0].data);
}

static Value bi_chdir(Value *args, int n) {
  if (n < 1) return val_err(4, "chdir: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "chdir: argument must be a string");
  return val_int(nexs_chdir((char *)args[0].data));
}

static Value bi_mount(Value *args, int n) {
  if (n < 2) return val_err(4, "mount: requires src and dst");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "mount: src must be a string");
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "mount: dst must be a string");
  int before = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  return val_int(reg_mount((char *)args[0].data, (char *)args[1].data, before));
}

static Value bi_bind(Value *args, int n) {
  if (n < 2) return val_err(4, "bind: requires src and dst");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "bind: src must be a string");
  if (args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "bind: dst must be a string");
  int flag = (n >= 3) ? (int)val_to_int(&args[2]) : 0;
  return val_int(reg_bind((char *)args[0].data, (char *)args[1].data, flag));
}

static Value bi_unmount(Value *args, int n) {
  if (n < 1) return val_err(4, "unmount: requires at least dst");
  const char *src = NULL, *dst = NULL;
  if (n >= 2) {
    if (args[0].type == TYPE_STR && args[0].data) src = (char *)args[0].data;
    if (args[1].type == TYPE_STR && args[1].data) dst = (char *)args[1].data;
  } else {
    if (args[0].type == TYPE_STR && args[0].data) dst = (char *)args[0].data;
  }
  if (!dst) return val_err(4, "unmount: invalid dst");
  return val_int(reg_unmount(src, dst));
}

/* Arrow UTF-8 → (U+2192) */
#define SIG(s) s " \xe2\x86\x92 "

void sysio_register_builtins(void) {
  fn_register_builtin_sig("open",    bi_open,
    SIG("open(path str, mode int)") "fd int");
  fn_register_builtin_sig("create",  bi_create,
    SIG("create(path str, mode int, perm int)") "fd int");
  fn_register_builtin_sig("close",   bi_close,
    SIG("close(fd int)") "nil");
  fn_register_builtin_sig("read",    bi_read,
    SIG("read(fd int, n int)") "str");
  fn_register_builtin_sig("write",   bi_write,
    SIG("write(fd int, data str)") "int");
  fn_register_builtin_sig("seek",    bi_seek,
    SIG("seek(fd int, offset int, whence int)") "int");
  fn_register_builtin_sig("dup",     bi_dup,
    SIG("dup(oldfd int, newfd int)") "fd int");
  fn_register_builtin_sig("fd2path", bi_fd2path,
    SIG("fd2path(fd int)") "str");
  fn_register_builtin_sig("remove",  bi_remove,
    SIG("remove(path str)") "int");
  fn_register_builtin_sig("pipe",    bi_pipe,
    SIG("pipe()") "arr [rfd wfd]");
  fn_register_builtin_sig("fstat",   bi_fstat,
    SIG("fstat(path str)") "str");
  fn_register_builtin_sig("chdir",   bi_chdir,
    SIG("chdir(path str)") "int");
  fn_register_builtin_sig("mount",   bi_mount,
    SIG("mount(src str, dst str, flags int)") "int");
  fn_register_builtin_sig("bind",    bi_bind,
    SIG("bind(src str, dst str, flags int)") "int");
  fn_register_builtin_sig("unmount", bi_unmount,
    SIG("unmount(src str, dst str)") "int");

  /* Store actual fn_table indices in /sys/<name> for val_print and eval resolution */
  {
    static const struct { const char *name; BuiltinFn fn; } t[] = {
      {"open",bi_open},{"create",bi_create},{"close",bi_close},
      {"read",bi_read},{"write",bi_write},{"seek",bi_seek},
      {"dup",bi_dup},{"fd2path",bi_fd2path},{"remove",bi_remove},
      {"pipe",bi_pipe},{"fstat",bi_fstat},{"chdir",bi_chdir},
      {"mount",bi_mount},{"bind",bi_bind},{"unmount",bi_unmount},
    };
    char path[REG_PATH_MAX];
    for (int _i = 0; _i < (int)(sizeof(t)/sizeof(t[0])); _i++) {
      NexsFnDef *def = fn_lookup(t[_i].name);
      if (def) {
        int idx = (int)(def - g_fn_table);
        snprintf(path, sizeof(path), "/sys/%s", t[_i].name);
        reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
      }
    }
  }
  reg_mkpath("/sys/fd", RK_READ);
}
#undef SIG
