/* sys/sysio.c — Language bindings, terminal input and keymap only.
 * Actual file descriptions are owned by sys/fd_owner.c, not a second table.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "include/nexs_sys.h"
#include "nexs_hal.h"
#include "../registry/include/nexs_registry.h"
#include "../lang/include/nexs_fn.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include "include/nexs_keymap.h"
#ifndef NEXS_BAREMETAL
#include <sys/ioctl.h>
#endif

static int s_key_lookahead = -1;

static int nexs_read_byte(void) {
  if (s_key_lookahead != -1) {
    int c = s_key_lookahead;
    s_key_lookahead = -1;
    return c;
  }
#ifdef NEXS_BAREMETAL
  return nexs_hal_getc();
#else
  char c;
  if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
  return (int)(unsigned char)c;
#endif
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
  /* Each pipe owns a distinct anonymous pair. There must never be a
   * global "__pipe__" array reused across unrelated descriptors. */
  DynArray *arr = arr_create_anon();
  if (arr == NULL || arr_set(arr, 0, val_int(fds[0])) != 0 ||
      arr_set(arr, 1, val_int(fds[1])) != 0) {
    if (arr != NULL)
      arr_unref(arr);
    nexs_close(fds[0]);
    nexs_close(fds[1]);
    return val_err(2, "pipe: descriptor array allocation failed");
  }
  Value v = val_nil();
  v.type = TYPE_ARR;
  v.data = arr;
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

/* =========================================================
   TERMINAL SYSCALLS (rawon / rawoff / readbyte / chr)
   ========================================================= */

static struct termios s_orig_term;
static int            s_raw_active = 0;

static int s_fg_pid = -1;

static int nexs_is_fg(void) {
#ifdef NEXS_BAREMETAL
  return 1;
#else
  if (s_fg_pid < 0) return 1;
  static pid_t s_own_pid;
  if (!s_own_pid) s_own_pid = getpid();
  return ((int)s_own_pid == s_fg_pid);
#endif
}

static Value bi_set_fg_pid(Value *args, int n) {
  if (n < 1) return val_nil();
  s_fg_pid = (int)val_to_int(&args[0]);
  return val_nil();
}

static Value bi_rawon(Value *args, int n) {
  (void)args; (void)n;
#ifdef NEXS_BAREMETAL
  return val_nil();
#else
  if (s_raw_active) return val_int(0);
  if (!isatty(STDIN_FILENO)) return val_int(-1);
  if (tcgetattr(STDIN_FILENO, &s_orig_term) != 0) return val_int(-1);
  struct termios raw = s_orig_term;
  raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON | BRKINT | ISTRIP | INPCK);
  raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cflag |= CS8;
  raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
  /* A mode switch must never discard keystrokes already queued by the
   * terminal: TCSAFLUSH lost commands typed just before the prompt. */
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return val_int(-1);
  s_raw_active = 1;
  return val_int(0);
#endif
}

static Value bi_rawoff(Value *args, int n) {
  (void)args; (void)n;
#ifdef NEXS_BAREMETAL
  return val_nil();
#else
  if (!s_raw_active) return val_int(0);
  /* Preserve typeahead across successive input() calls. */
  tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_term);
  s_raw_active = 0;
  return val_int(0);
#endif
}

static char s_key_layout[16] = "us";

static Value bi_set_layout(Value *args, int n) {
    if (n < 1 || args[0].type != TYPE_STR || !args[0].data)
        return val_err(4, "set_layout: requires string layout name");
    strncpy(s_key_layout, (char *)args[0].data, sizeof(s_key_layout) - 1);
    s_key_layout[sizeof(s_key_layout) - 1] = '\0';
    return val_int(0);
}

static Value bi_readkey(Value *args, int n) {
    (void)args; (void)n;
    int c = nexs_read_byte();
    if (c == -1) return val_str("");

    if (c == 27) { /* ESC */
        int c2, c3;
        /* Peek con poll: se non arriva niente entro 50ms → bare ESC */
#ifndef NEXS_BAREMETAL
        struct pollfd _pf = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&_pf, 1, 50) <= 0) return val_str("ESC");
#endif
        c2 = nexs_read_byte();
        if (c2 == -1) return val_str("ESC");

        /* CSI sequences: ESC [ ... */
        if (c2 == '[') {
            c3 = nexs_read_byte();
            if (c3 == -1) return val_str("ESC");
            
            if (c3 == 'A') return val_str("KEY_UP");
            if (c3 == 'B') return val_str("KEY_DOWN");
            if (c3 == 'C') return val_str("KEY_RIGHT");
            if (c3 == 'D') return val_str("KEY_LEFT");

            if (c3 == 'H') return val_str("KEY_HOME");
            if (c3 == 'F') return val_str("KEY_END");
            
            /* ESC [ N ~ sequences */
            if (c3 >= '1' && c3 <= '6') {
                int c4 = nexs_read_byte(); /* consume '~' */
                if (c3 == '1' || c3 == '7') return val_str("KEY_HOME");
                if (c3 == '4' || c3 == '8') return val_str("KEY_END");
                if (c3 == '3') return val_str("KEY_DELETE");
                if (c3 == '5') return val_str("KEY_PGUP");
                if (c3 == '6') return val_str("KEY_PGDN");
                if (c4 != '~' && c4 != -1) { /* handle ;5A etc - simplified */ }
            }
        }
        /* SS3 sequences: ESC O ... (xterm, macOS Terminal) */
        if (c2 == 'O') {
            c3 = nexs_read_byte();
            if (c3 == -1) return val_str("ESC");
            if (c3 == 'A') return val_str("KEY_UP");
            if (c3 == 'B') return val_str("KEY_DOWN");
            if (c3 == 'C') return val_str("KEY_RIGHT");
            if (c3 == 'D') return val_str("KEY_LEFT");
            if (c3 == 'H') return val_str("KEY_HOME");
            if (c3 == 'F') return val_str("KEY_END");
        }
        return val_str("ESC");
    }

    if (c == 127 || c == 8)  return val_str("BACKSPACE");
    if (c == 10  || c == 13) return val_str("ENTER");
    if (c == 24)             return val_str("CTRL_X");
    if (c == 1)              return val_str("CTRL_A");
    if (c == 5)              return val_str("CTRL_E");
    if (c == 11)             return val_str("CTRL_K");
    if (c == 21)             return val_str("CTRL_U");
    if (c == 23)             return val_str("CTRL_W");
    if (c == 9)              return val_str("TAB");
    if (c < 32)              return val_str(""); /* altri ctrl — ignora */

    const char *translated = translate_key_layout(s_key_layout, (char)c);
    return val_str((char *)translated);
}

/* readkey_nb(ms) → str: readkey non-bloccante con timeout in ms */
static Value bi_readkey_nb(Value *args, int n) {
    int ms = (n >= 1) ? (int)val_to_int(&args[0]) : 50;
#ifndef NEXS_BAREMETAL
    struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, ms) <= 0) return val_str("");
#else
    /* Baremetal: check if a byte is available without blocking */
    if (s_key_lookahead == -1) {
        s_key_lookahead = nexs_hal_getc();
    }
    if (s_key_lookahead == -1) return val_str("");
    (void)ms;
#endif
    return bi_readkey(NULL, 0);
}

static Value bi_term_at(Value *args, int n) {
    if (!nexs_is_fg()) return val_nil();
    if (n < 3) return val_err(4, "term_at: requires y, x, str");
    int y = (int)val_to_int(&args[0]);
    int x = (int)val_to_int(&args[1]);
    const char *s = (args[2].type == TYPE_STR) ? (const char *)args[2].data : "";
    char buf[64];
    snprintf(buf, sizeof(buf), "\033[%d;%dH", y, x);
    nexs_hal_print(buf);
    nexs_hal_print(s);
    return val_nil();
}

static Value bi_term_cls(Value *args, int n) {
    (void)args; (void)n;
    if (!nexs_is_fg()) return val_nil();
    nexs_hal_print("\033[2J\033[H");
    return val_nil();
}

static Value bi_term_cursor_move(Value *args, int n) {
    if (n < 2) return val_err(4, "term_cursor_move: requires y, x");
    int y = (int)val_to_int(&args[0]);
    int x = (int)val_to_int(&args[1]);
    char buf[32];
    snprintf(buf, sizeof(buf), "\033[%d;%dH", y, x);
    nexs_hal_print(buf);
    return val_nil();
}

static Value bi_term_erase_eol(Value *args, int n) {
    (void)args; (void)n;
    nexs_hal_print("\033[K");
    return val_nil();
}

static Value bi_term_cursor_show(Value *args, int n) {
    if (n < 1) return val_err(4, "term_cursor_show: requires bool");
    int show = (int)val_to_int(&args[0]);
    nexs_hal_print(show ? "\033[?25h" : "\033[?25l");
    return val_nil();
}

static Value bi_term_flush(Value *args, int n) {
    (void)args; (void)n;
    if (!nexs_is_fg()) return val_nil();
#ifndef NEXS_BAREMETAL
    fflush(stdout);
#endif
    return val_nil();
}

#define NEXS_TERM_SIZE_FALLBACK "80x24"

static Value bi_term_size(Value *args, int n) {
  (void)args; (void)n;
#ifdef NEXS_BAREMETAL
  return val_str(NEXS_TERM_SIZE_FALLBACK);
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%dx%d", (int)ws.ws_col, (int)ws.ws_row);
    return val_str(buf);
  }
  return val_str(NEXS_TERM_SIZE_FALLBACK);
#endif
}

static Value bi_readbyte(Value *args, int n) {
  (void)args; (void)n;
  char c;
  ssize_t r = read(STDIN_FILENO, &c, 1);
  if (r <= 0) return val_int(-1);
  return val_int((int64_t)(unsigned char)c);
}

static Value bi_chr(Value *args, int n) {
  if (n < 1) return val_str("");
  int64_t cp = val_to_int(&args[0]);
  char buf[5] = {0};
  if (cp < 0x80) {
    buf[0] = (char)cp;
  } else if (cp < 0x800) {
    buf[0] = (char)(0xC0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
  } else {
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
  }
  return val_str(buf);
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
  fn_register_builtin_sig("mount",    bi_mount,
    SIG("mount(src str, dst str, flags int)") "int");
  fn_register_builtin_sig("bind",     bi_bind,
    SIG("bind(src str, dst str, flags int)") "int");
  fn_register_builtin_sig("unmount",  bi_unmount,
    SIG("unmount(src str, dst str)") "int");
  fn_register_builtin_sig("rawon",    bi_rawon,
    SIG("rawon()") "int");
  fn_register_builtin_sig("rawoff",   bi_rawoff,
    SIG("rawoff()") "int");
  fn_register_builtin_sig("readbyte", bi_readbyte,
    SIG("readbyte()") "int");
  fn_register_builtin_sig("chr",      bi_chr,
    SIG("chr(codepoint int)") "str");
  fn_register_builtin_sig("set_layout", bi_set_layout,
    SIG("set_layout(name str)") "int");
  fn_register_builtin_sig("readkey",    bi_readkey,
    SIG("readkey()") "str");
  fn_register_builtin_sig("readkey_nb", bi_readkey_nb,
    SIG("readkey_nb(ms int)") "str");
  fn_register_builtin_sig("term_at",   bi_term_at,
    SIG("term_at(y int, x int, s str)") "nil");
  fn_register_builtin_sig("term_cls",  bi_term_cls,
    SIG("term_cls()") "nil");
  fn_register_builtin_sig("term_cursor_move", bi_term_cursor_move,
    SIG("term_cursor_move(y int, x int)") "nil");
  fn_register_builtin_sig("term_erase_eol",   bi_term_erase_eol,
    SIG("term_erase_eol()") "nil");
  fn_register_builtin_sig("term_cursor_show", bi_term_cursor_show,
    SIG("term_cursor_show(bool)") "nil");
  fn_register_builtin_sig("term_flush", bi_term_flush,
    SIG("term_flush()") "nil");
  fn_register_builtin_sig("term_size", bi_term_size,
    SIG("term_size()") "str");

  /* Single terminal driver, two explicit registry interfaces:
   * public stdlib wrappers maintain per-session shadow buffers;
   * underscore names are the raw HAL provider contract called by them.
   * They are aliases to EXISTING callbacks, not a second implementation. */
  fn_register_builtin_sig("_readkey", bi_readkey,
    SIG("_readkey()") "str");
  fn_register_builtin_sig("_term_at", bi_term_at,
    SIG("_term_at(y int, x int, s str)") "nil");
  fn_register_builtin_sig("_term_cls", bi_term_cls,
    SIG("_term_cls()") "nil");
  fn_register_builtin_sig("_term_flush", bi_term_flush,
    SIG("_term_flush()") "nil");
  fn_register_builtin_sig("_term_cursor_move", bi_term_cursor_move,
    SIG("_term_cursor_move(y int, x int)") "nil");
  fn_register_builtin_sig("_term_cursor_show", bi_term_cursor_show,
    SIG("_term_cursor_show(bool)") "nil");
  fn_register_builtin_sig("_term_size", bi_term_size,
    SIG("_term_size()") "str");

  fn_register_builtin_sig("set_fg_pid", bi_set_fg_pid,
    SIG("set_fg_pid(pid int)") "nil");

  /* Every built-in above was already published to /sys/ by
   * fn_register_builtin_sig: no second table or registration pass. */
  reg_mkpath("/sys/fd", RK_READ);
}
#undef SIG
