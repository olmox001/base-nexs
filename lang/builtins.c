/*
 * lang/builtins.c — Standard Built-in Functions
 * ================================================
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

/*
 * Registers built-ins via fn_register_builtin() AND reg_register_builtin()
 * (both paths kept for compatibility with the registry-based dispatcher).
 *
 * Built-ins:
 *   str, int, float, len, type, buddy_stats, errstr
 *   sendmessage, receivemessage, msgpending
 *   mkptr, deref
 *   substr, contains, split, trim, upper, lower
 *   abs, min, max
 *   keys (list registry children as array)
 *   print_reg (print registry path to stdout)
 */

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_value.h"
#include "../registry/include/nexs_registry.h"
#include "include/nexs_eval.h"
#include "include/nexs_fn.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef NEXS_BAREMETAL
#include <dirent.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define nexs_sleep_ms(ms) usleep((ms) * 1000)
#else
#include "../hal/include/nexs_timer.h"
#define nexs_sleep_ms(ms) hal_timer_sleep_ms(ms)
#endif
#include <string.h>

/* =========================================================
   HELPER — register into both fn_table and registry /sys/
   ========================================================= */

static void register_builtin_sig(const char *name, BuiltinFn fn,
                                 const char *sig) {
  int idx = fn_register_builtin_sig(name, fn, sig);
  if (idx >= 0) {
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "/sys/%s", name);
    reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
  }
}

/* =========================================================
   STANDARD BUILT-INS
   ========================================================= */

static Value builtin_str(Value *args, int n) {
  if (n < 1)
    return val_err(4, "str: requires 1 argument");
  char buf[MAX_STR_LEN];
  switch (args[0].type) {
  case TYPE_INT:
    snprintf(buf, sizeof(buf), "%lld", (long long)args[0].ival);
    break;
  case TYPE_FLOAT:
    snprintf(buf, sizeof(buf), "%g", args[0].fval);
    break;
  case TYPE_BOOL:
    snprintf(buf, sizeof(buf), "%s", args[0].ival ? "true" : "false");
    break;
  case TYPE_STR:
    return val_clone(&args[0]);
  case TYPE_NIL:
    return val_str("nil");
  case TYPE_ERR:
    return val_str(args[0].err_msg ? args[0].err_msg : "unknown error");
  default:
    snprintf(buf, sizeof(buf), "<%s>", val_type_name(args[0].type));
  }
  return val_str(buf);
}

static Value builtin_int_(Value *args, int n) {
  if (n < 1)
    return val_err(4, "int: requires 1 argument");
  return val_int(val_to_int(&args[0]));
}

static Value builtin_float_(Value *args, int n) {
  if (n < 1)
    return val_err(4, "float: requires 1 argument");
  return val_float(val_to_float(&args[0]));
}

static Value builtin_len(Value *args, int n) {
  if (n < 1)
    return val_err(4, "len: requires 1 argument");
  if (args[0].type == TYPE_STR && args[0].data)
    return val_int((int64_t)strlen((char *)args[0].data));
  if (args[0].type == TYPE_ARR && args[0].data)
    return val_int((int64_t)((DynArray *)args[0].data)->size);
  if (args[0].type == TYPE_NIL)
    return val_int(0);
  return val_err(4, "len: unsupported type");
}

static Value builtin_type(Value *args, int n) {
  if (n < 1)
    return val_err(4, "type: requires 1 argument");
  return val_str(val_type_name(args[0].type));
}

static Value builtin_buddy_stats(Value *args, int n) {
  (void)args;
  (void)n;
  buddy_dump_stats(stdout);
  return val_nil();
}

static Value builtin_errstr(Value *args, int n) {
  (void)args;
  (void)n;
  extern void nexs_errstr(char *buf, int nbuf);
  char buf[256];
  buf[0] = '\0'; /* Importante: inizializza a stringa vuota se vogliamo solo
                    leggere */
  nexs_errstr(buf, sizeof(buf));
  return val_str(buf);
}

/* =========================================================
   STRING BUILT-INS
   ========================================================= */

/*
 * substr(s, start, len) → str
 * Zero-based, bounds-clamped. Negative start wraps from end.
 */
static Value builtin_substr(Value *args, int n) {
  if (n < 1)
    return val_err(4, "substr: requires at least 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "substr: first argument must be a string");
  const char *s = (const char *)args[0].data;
  int64_t slen = (int64_t)strlen(s);
  int64_t start = (n >= 2) ? val_to_int(&args[1]) : 0;
  int64_t count = (n >= 3) ? val_to_int(&args[2]) : slen;

  /* Wrap negative start from end */
  if (start < 0)
    start = slen + start;
  if (start < 0)
    start = 0;
  if (start >= slen)
    return val_str("");
  if (count < 0)
    count = 0;
  if (start + count > slen)
    count = slen - start;

  char buf[MAX_STR_LEN];
  if (count >= MAX_STR_LEN)
    count = MAX_STR_LEN - 1;
  strncpy(buf, s + start, (size_t)count);
  buf[count] = '\0';
  return val_str(buf);
}

/*
 * contains(s, sub) → bool
 */
static Value builtin_contains(Value *args, int n) {
  if (n < 2)
    return val_err(4, "contains: requires 2 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_err(4, "contains: both arguments must be strings");
  const char *s = args[0].data ? (const char *)args[0].data : "";
  const char *sub = args[1].data ? (const char *)args[1].data : "";
  return val_bool(strstr(s, sub) != NULL);
}

static Value builtin_startswith(Value *args, int n) {
  if (n < 2)
    return val_err(4, "startswith: requires 2 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_bool(0);
  const char *s = args[0].data ? (const char *)args[0].data : "";
  const char *pre = args[1].data ? (const char *)args[1].data : "";
  if (!*pre)
    return val_bool(1);
  return val_bool(strncmp(s, pre, strlen(pre)) == 0);
}

static Value builtin_endswith(Value *args, int n) {
  if (n < 2)
    return val_err(4, "endswith: requires 2 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_bool(0);
  const char *s = args[0].data ? (const char *)args[0].data : "";
  const char *suf = args[1].data ? (const char *)args[1].data : "";
  if (!*suf)
    return val_bool(1);
  size_t slen = strlen(s);
  size_t suflen = strlen(suf);
  if (suflen > slen)
    return val_bool(0);
  return val_bool(strcmp(s + slen - suflen, suf) == 0);
}

/*
 * trim(s) → str  (strip leading and trailing whitespace)
 */
static Value builtin_trim(Value *args, int n) {
  if (n < 1)
    return val_err(4, "trim: requires 1 argument");
  if (args[0].type != TYPE_STR)
    return val_err(4, "trim: argument must be a string");
  const char *s = args[0].data ? (const char *)args[0].data : "";
  while (isspace((unsigned char)*s))
    s++;
  char buf[MAX_STR_LEN];
  strncpy(buf, s, MAX_STR_LEN - 1);
  buf[MAX_STR_LEN - 1] = '\0';
  size_t len = strlen(buf);
  while (len > 0 && isspace((unsigned char)buf[len - 1]))
    buf[--len] = '\0';
  return val_str(buf);
}

/*
 * upper(s) / lower(s) → str
 */
static Value builtin_upper(Value *args, int n) {
  if (n < 1)
    return val_err(4, "upper: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_str("");
  char buf[MAX_STR_LEN];
  const char *s = (const char *)args[0].data;
  size_t i;
  for (i = 0; s[i] && i < MAX_STR_LEN - 1; i++)
    buf[i] = (char)toupper((unsigned char)s[i]);
  buf[i] = '\0';
  return val_str(buf);
}

static Value builtin_lower(Value *args, int n) {
  if (n < 1)
    return val_err(4, "lower: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_str("");
  char buf[MAX_STR_LEN];
  const char *s = (const char *)args[0].data;
  size_t i;
  for (i = 0; s[i] && i < MAX_STR_LEN - 1; i++)
    buf[i] = (char)tolower((unsigned char)s[i]);
  buf[i] = '\0';
  return val_str(buf);
}

/*
 * split(s, sep) → arr
 * Simple split on a delimiter string. Returns at most MAX_ARRAYS elements.
 */
static Value builtin_split(Value *args, int n) {
  if (n < 2)
    return val_err(4, "split: requires string and separator");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_err(4, "split: both arguments must be strings");
  const char *s = args[0].data ? (const char *)args[0].data : "";
  const char *sep = args[1].data ? (const char *)args[1].data : "";
  size_t seplen = strlen(sep);

  /* Build a temporary array */
  DynArray *arr = arr_create_anon();

  if (seplen == 0) {
    /* Split into individual characters */
    for (size_t i = 0; s[i]; i++) {
      char ch[2] = {s[i], '\0'};
      arr_set(arr, i, val_str(ch));
    }
  } else {
    size_t idx = 0;
    const char *p = s;
    const char *found;
    while ((found = strstr(p, sep)) != NULL) {
      size_t chunk = (size_t)(found - p);
      char buf[MAX_STR_LEN];
      if (chunk >= MAX_STR_LEN)
        chunk = MAX_STR_LEN - 1;
      strncpy(buf, p, chunk);
      buf[chunk] = '\0';
      arr_set(arr, idx++, val_str(buf));
      p = found + seplen;
    }
    /* Last segment */
    arr_set(arr, idx, val_str(p));
  }

  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

/*
 * replace(s, old, new) → str
 * Replace first occurrence of old with new.
 */
static Value builtin_replace(Value *args, int n) {
  if (n < 3)
    return val_err(4, "replace: requires 3 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR ||
      args[2].type != TYPE_STR)
    return val_err(4, "replace: all arguments must be strings");
  const char *s = args[0].data ? (const char *)args[0].data : "";
  const char *old = args[1].data ? (const char *)args[1].data : "";
  const char *rep = args[2].data ? (const char *)args[2].data : "";
  size_t oldlen = strlen(old);

  if (oldlen == 0)
    return val_clone(&args[0]);
  const char *found = strstr(s, old);
  if (!found)
    return val_clone(&args[0]);

  char buf[MAX_STR_LEN * 2];
  size_t prefix = (size_t)(found - s);
  size_t replen = strlen(rep);
  size_t suffix_off = prefix + oldlen;
  size_t total = prefix + replen + strlen(s + suffix_off);
  if (total >= sizeof(buf))
    total = sizeof(buf) - 1;
  snprintf(buf, sizeof(buf), "%.*s%.*s%s", (int)prefix, s, (int)replen, rep,
           s + suffix_off);
  buf[total] = '\0';
  return val_str(buf);
}

/* =========================================================
   NUMERIC BUILT-INS
   ========================================================= */

static Value builtin_abs(Value *args, int n) {
  if (n < 1)
    return val_err(4, "abs: requires 1 argument");
  if (args[0].type == TYPE_FLOAT)
    return val_float(args[0].fval < 0.0 ? -args[0].fval : args[0].fval);
  int64_t v = val_to_int(&args[0]);
  return val_int(v < 0 ? -v : v);
}

static Value builtin_min(Value *args, int n) {
  if (n < 2)
    return val_err(4, "min: requires 2 arguments");
  if (args[0].type == TYPE_FLOAT || args[1].type == TYPE_FLOAT) {
    double a = val_to_float(&args[0]), b = val_to_float(&args[1]);
    return val_float(a < b ? a : b);
  }
  int64_t a = val_to_int(&args[0]), b = val_to_int(&args[1]);
  return val_int(a < b ? a : b);
}

static Value builtin_max(Value *args, int n) {
  if (n < 2)
    return val_err(4, "max: requires 2 arguments");
  if (args[0].type == TYPE_FLOAT || args[1].type == TYPE_FLOAT) {
    double a = val_to_float(&args[0]), b = val_to_float(&args[1]);
    return val_float(a > b ? a : b);
  }
  int64_t a = val_to_int(&args[0]), b = val_to_int(&args[1]);
  return val_int(a > b ? a : b);
}

/* =========================================================
   REGISTRY BUILT-INS
   ========================================================= */

/*
 * keys(path) → arr
 * Returns an array of the direct child key names under the registry path.
 * Plan 9 style: "ls as a value". Lets scripts iterate the registry.
 */
/* lines_of(str) → arr: split su \n, ritorna array nativo */
static Value builtin_lines_of(Value *args, int n) {
  if (n < 1 || args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "lines_of: requires string");

  const char *src = (const char *)args[0].data;
  DynArray *arr = arr_create_anon();

  char *copy = buddy_strdup(src);
  char *p = copy;
  char *line = p;
  size_t idx = 0;

  while (*p) {
    if (*p == '\n') {
      *p = '\0';
      arr_set(arr, idx++, val_str(line));
      line = p + 1;
    }
    p++;
  }
  /* Last part */
  if (*line || p > line) {
    arr_set(arr, idx++, val_str(line));
  }

  xfree(copy);

  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

static Value builtin_keys(Value *args, int n) {
  if (n < 1)
    return val_err(4, "keys: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "keys: argument must be a string path");

  RegKey *k = reg_lookup((char *)args[0].data);
  if (!k)
    return val_err(4, "keys: path not found");

  DynArray *arr = arr_create_anon();

  size_t idx = 0;
  for (RegKey *c = k->children; c; c = c->next) {
    Value tmp = val_str(c->name);
    arr_set(arr, idx++, tmp);
    val_free(&tmp);
  }

  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

static Value builtin_print_reg(Value *args, int n) {
  if (n < 1)
    return val_err(4, "print_reg: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "print_reg: argument must be a string path");
  reg_ls_recursive((char *)args[0].data, NULL, 0);
  return val_nil();
}

/*
 * reg_get(path) → value   (function-call form of `reg /path`)
 * Useful when path is a dynamic string: reg_get("/sys/pm/proc/" + str(pid))
 */
static Value builtin_reg_get(Value *args, int n) {
  if (n < 1)
    return val_err(4, "reg_get: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_get: argument must be a string path");
  return reg_get((char *)args[0].data);
}

/*
 * reg_set_fn(path, value) → nil   (function-call form of `reg /path = val`)
 * Enables dynamic registry writes with computed paths.
 */
static Value builtin_reg_set_fn(Value *args, int n) {
  if (n < 2)
    return val_err(4, "reg_set: requires path and value");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_set: first argument must be a string path");
  reg_set((char *)args[0].data, args[1], RK_ALL);
  return val_nil();
}

/*
 * reg_del(path) → nil   (function-call form of reg_delete)
 */
static Value builtin_reg_del(Value *args, int n) {
  if (n < 1)
    return val_err(4, "reg_del: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_del: argument must be a string path");
  reg_delete((char *)args[0].data);
  return val_nil();
}

/* =========================================================
   ARRAY/STRING BUILT-INS (arr_join, arr_ins, arr_del)
   ========================================================= */

static Value builtin_arr_create_anon(Value *args, int n) {
  (void)args;
  (void)n;
  DynArray *arr = arr_create_anon();
  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

/* arr_join(arr sep) → str: concatena elementi arr separati da sep (es. "\n") */
static Value builtin_arr_join(Value *args, int n) {
  if (n < 1 || args[0].type != TYPE_ARR || !args[0].data)
    return val_err(4, "arr_join: requires arr");
  DynArray *arr = (DynArray *)args[0].data;
  const char *sep = (n >= 2 && args[1].type == TYPE_STR && args[1].data)
                        ? (const char *)args[1].data
                        : "\n";
  size_t seplen = strlen(sep);
  int count = (int)arr->size;
  /* Estimate total size */
  char result[MAX_STR_LEN * 2];
  int pos = 0;
  for (int i = 0; i < count && pos < (int)sizeof(result) - 2; i++) {
    Value *v = &arr->items[i];
    const char *s = (v->type == TYPE_STR && v->data) ? (char *)v->data : "";
    int slen = (int)strlen(s);
    if (pos + slen + seplen >= sizeof(result) - 1)
      slen = (int)(sizeof(result) - pos - seplen - 1);
    memcpy(result + pos, s, (size_t)slen);
    pos += slen;
    if (i < count - 1) {
      memcpy(result + pos, sep, seplen);
      pos += seplen;
    }
  }
  result[pos] = '\0';
  return val_str(result);
}

/* arr_ins(arr idx val) → nil: inserisce val a idx, shift right */
static Value builtin_arr_ins(Value *args, int n) {
  if (n < 3 || args[0].type != TYPE_ARR || !args[0].data)
    return val_err(4, "arr_ins: arr, idx, val");
  DynArray *arr = (DynArray *)args[0].data;
  size_t idx = (size_t)val_to_int(&args[1]);
  Value newval = val_clone(&args[2]);
  /* Grow array by 1 */
  arr_ensure_cap(arr, arr->size);
  /* Shift right from idx */
  for (size_t j = arr->size; j > idx; j--)
    arr->items[j] = arr->items[j - 1];
  arr->items[idx] = newval;
  arr->size++;
  return val_nil();
}

/* arr_del(arr idx) → nil: rimuove elemento a idx, shift left */
static Value builtin_arr_del(Value *args, int n) {
  if (n < 2 || args[0].type != TYPE_ARR || !args[0].data)
    return val_err(4, "arr_del: arr, idx");
  DynArray *arr = (DynArray *)args[0].data;
  size_t idx = (size_t)val_to_int(&args[1]);
  if (idx >= arr->size)
    return val_nil();
  val_free(&arr->items[idx]);
  /* Shift left */
  for (size_t j = idx; j < arr->size - 1; j++)
    arr->items[j] = arr->items[j + 1];
  arr->size--;
  return val_nil();
}

/* =========================================================
   IPC BUILT-INS
   ========================================================= */

static Value nexs_builtin_sendmsg(Value *args, int n) {
  if (n < 2)
    return val_err(4, "sendmsg: requires path and value");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "sendmsg: first argument must be a string path");
  int rc = reg_ipc_send((char *)args[0].data, args[1]);
  return val_int(rc);
}

static Value nexs_builtin_recvmsg(Value *args, int n) {
  if (n < 1)
    return val_err(4, "recvmsg: requires path");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "recvmsg: argument must be a string path");
  Value msg = val_nil();
  int rc = reg_ipc_recv((char *)args[0].data, &msg);
  if (rc < 0) {
    val_free(&msg);
    return val_nil();
  }
  return msg;
}

static Value nexs_builtin_msgpending(Value *args, int n) {
  if (n < 1)
    return val_err(4, "msgpending: requires path");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "msgpending: argument must be a string path");
  int count = reg_ipc_pending((char *)args[0].data);
  return val_int(count);
}

/* =========================================================
   POINTER BUILT-INS
   ========================================================= */

static Value nexs_builtin_ptr(Value *args, int n) {
  if (n < 1)
    return val_err(4, "ptr: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "ptr: argument must be a string path");
  return val_ptr((char *)args[0].data);
}

static Value nexs_builtin_deref(Value *args, int n) {
  if (n < 1)
    return val_err(4, "deref: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "deref: argument must be a string path");
  return reg_get_deref((char *)args[0].data);
}

static Value builtin_eval_builtin(Value *args, int n) {
  if (n < 1)
    return val_err(4, "eval: requires a string argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "eval: argument must be a string");

  const char *src = (const char *)args[0].data;

  /* Create a completely fresh context for reentrant eval.
   * DO NOT inherit FILE* pointers from the global context — they may
   * be corrupt if the global ctx pointer has been invalidated.
   * eval_ctx_init sets safe defaults (stdout/stderr on host, NULL on
   * baremetal). */
  EvalCtx inner_ctx;
  eval_ctx_init(&inner_ctx);

  EvalCtx *old_ctx = nexs_g_eval_ctx;
  EvalResult r = eval_str(&inner_ctx, src);
  nexs_g_eval_ctx = old_ctx;

  if (r.sig == CTRL_ERR) {
    /* Preserve the original error message from the inner eval */
    if (r.ret_val.type == TYPE_ERR && r.ret_val.err_msg) {
      return r.ret_val; /* pass through the original error */
    }
    val_free(&r.ret_val);
    return val_err(4, "eval: execution failed");
  }
  return r.ret_val;
}

static Value builtin_sleep(Value *args, int n) {
  if (n < 1)
    return val_err(4, "sleep: requires ms");
  int ms = (int)val_to_int(&args[0]);
  nexs_sleep_ms(ms);
  return val_nil();
}

/* =========================================================
   TEXT BUFFER BUILT-INS
   ========================================================= */

static Value builtin_tb_create(Value *args, int n) {
  (void)args;
  (void)n;
  DynArray *arr = arr_create_anon();
  arr_set(arr, 0, val_str(""));
  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

static Value builtin_tb_load(Value *args, int n) {
  if (n < 2 || args[0].type != TYPE_ARR || args[1].type != TYPE_STR)
    return val_err(4, "tb_load: buffer array and content string required");

  DynArray *buf = (DynArray *)args[0].data;
  const char *content = (char *)args[1].data;

  for (size_t i = 0; i < buf->size; i++)
    val_free(&buf->items[i]);
  buf->size = 0;

  char *copy = buddy_strdup(content);
  char *p = copy;
  char *line = p;
  size_t idx = 0;
  while (*p) {
    if (*p == '\n') {
      *p = '\0';
      Value tmp = val_str(line);
      arr_set(buf, idx++, tmp);
      val_free(&tmp);
      line = p + 1;
    }
    p++;
  }
  if (*line || p > line) {
    Value tmp = val_str(line);
    arr_set(buf, idx++, tmp);
    val_free(&tmp);
  }
  if (idx == 0) {
    Value tmp = val_str("");
    arr_set(buf, 0, tmp);
    val_free(&tmp);
  }

  xfree(copy);
  return val_int((int64_t)idx);
}

static Value builtin_tb_to_str(Value *args, int n) {
  if (n < 1 || args[0].type != TYPE_ARR)
    return val_err(4, "tb_to_str: buffer required");
  DynArray *buf = (DynArray *)args[0].data;

  /* First pass: compute total size needed */
  size_t total = 1; /* NUL terminator */
  for (size_t i = 0; i < buf->size; i++) {
    if (buf->items[i].type == TYPE_STR && buf->items[i].data)
      total += strlen((char *)buf->items[i].data) + 1; /* +1 for '\n' */
  }

  char *res = xmalloc(total);
  res[0] = '\0';
  size_t cur_len = 0;

  for (size_t i = 0; i < buf->size; i++) {
    if (buf->items[i].type == TYPE_STR && buf->items[i].data) {
      const char *line = (char *)buf->items[i].data;
      size_t llen = strlen(line);
      memcpy(res + cur_len, line, llen);
      cur_len += llen;
      if (i < buf->size - 1)
        res[cur_len++] = '\n';
    }
  }
  res[cur_len] = '\0';

  Value v = val_str(res);
  xfree(res);
  return v;
}

static Value builtin_tb_insert_char(Value *args, int n) {
  if (n < 4 || args[0].type != TYPE_ARR)
    return val_err(4, "tb_insert_char args");
  DynArray *buf = (DynArray *)args[0].data;
  int y = (int)val_to_int(&args[1]);
  int x = (int)val_to_int(&args[2]);
  const char *ch = (args[3].type == TYPE_STR) ? (char *)args[3].data : "";

  Value line_val = arr_get_at(buf, (size_t)y);
  if (line_val.type != TYPE_STR) {
    val_free(&line_val);
    return val_nil();
  }
  const char *old = (char *)line_val.data;
  size_t olen = strlen(old);

  if (x < 0)
    x = 0;
  if (x > (int)olen)
    x = (int)olen;

  char *new_str = xmalloc(olen + strlen(ch) + 1);
  strncpy(new_str, old, (size_t)x);
  strcpy(new_str + x, ch);
  strcpy(new_str + x + strlen(ch), old + x);

  arr_set(buf, (size_t)y, val_str(new_str));
  xfree(new_str);
  val_free(&line_val);
  return val_nil();
}

static Value builtin_tb_delete_char(Value *args, int n) {
  if (n < 3 || args[0].type != TYPE_ARR)
    return val_err(4, "tb_delete_char args");
  DynArray *buf = (DynArray *)args[0].data;
  int y = (int)val_to_int(&args[1]);
  int x = (int)val_to_int(&args[2]);

  if (x <= 0)
    return val_int(0);

  Value line_val = arr_get_at(buf, (size_t)y);
  if (line_val.type != TYPE_STR) {
    val_free(&line_val);
    return val_int(0);
  }
  const char *old = (char *)line_val.data;
  size_t olen = strlen(old);

  if (x > (int)olen)
    x = (int)olen;

  char *new_str = xmalloc(olen + 1);
  strncpy(new_str, old, (size_t)x - 1);
  strcpy(new_str + x - 1, old + x);

  arr_set(buf, (size_t)y, val_str(new_str));
  xfree(new_str);
  val_free(&line_val);
  return val_int(1);
}

/* =========================================================
   REGISTRATION
   ========================================================= */

/* Arrow UTF-8 → (U+2192) */
#define SIG(s) s " \xe2\x86\x92 "

static Value builtin_sys_embedded_read(Value *args, int n) {
  if (n < 1 || args[0].type != TYPE_STR)
    return val_err(4, "sys_embedded_read: path string required");
  const char *path = (char *)args[0].data;
  extern const char *nexs_embedded_lookup(const char *path);
  const char *src = nexs_embedded_lookup(path);
  if (!src)
    return val_err(4, "File not found in embedded table");
  return val_str(src);
}

typedef struct {
  const char *path;
  const char *src;
} NexsEmbedDep;
/* The real table is provided by codegen.c during standalone build */
extern const NexsEmbedDep nexs_embed_deps_table[];

static Value builtin_sys_bundled(Value *args, int n) {
  (void)args;
  (void)n;
  DynArray *arr = arr_create_anon();
  size_t idx = 0;
#ifndef NEXS_BAREMETAL
  DIR *d = opendir("modules");
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (ent->d_name[0] == '.')
        continue;
      arr_set(arr, idx++, val_str(ent->d_name));
    }
    closedir(d);
  }
#else
  /* The table is generated by codegen.c in standalone builds */
  /* nexs_fprintf(NULL, "[TRACE] sys_bundled: table_addr=0x%llx starting loop\n", (unsigned long long)nexs_embed_deps_table); */
  for (int i = 0; nexs_embed_deps_table[i].path; i++) {
    const char *p = nexs_embed_deps_table[i].path;
    /* const char *s = nexs_embed_deps_table[i].src;
    nexs_fprintf(NULL, "[TRACE] sys_bundled: adding %s (path_ptr=0x%llx, src_ptr=0x%llx)\n", 
                 p, (unsigned long long)p, (unsigned long long)s); */
    if (!p) break;
    Value tmp = val_str(p);
    arr_set(arr, idx++, tmp);
    val_free(&tmp);
  }
#endif
  Value v;
  v.type = TYPE_ARR;
  v.data = arr;
  v.ival = 0;
  v.fval = 0;
  v.err_code = 0;
  v.err_msg = NULL;
  return v;
}

static Value builtin_sys_debug(Value *args, int n) {
  if (n > 0)
    g_nexs_debug = val_is_truthy(&args[0]);
  return val_bool(g_nexs_debug);
}

void builtins_register_all(void) {
  /* Core type conversion */
  register_builtin_sig("str", builtin_str, SIG("str(value)") "str");
  register_builtin_sig("int", builtin_int_, SIG("int(value)") "int");
  register_builtin_sig("float", builtin_float_, SIG("float(value)") "float");
  register_builtin_sig("len", builtin_len, SIG("len(str|arr)") "int");
  register_builtin_sig("type", builtin_type, SIG("type(value)") "str");
  register_builtin_sig("buddy_stats", builtin_buddy_stats,
                       SIG("buddy_stats()") "nil");
  register_builtin_sig("errstr", builtin_errstr, SIG("errstr()") "str");
  register_builtin_sig("eval", builtin_eval_builtin,
                       SIG("eval(src str)") "value");

  /* String manipulation */
  register_builtin_sig("substr", builtin_substr,
                       SIG("substr(s str, start int, len int)") "str");
  register_builtin_sig("contains", builtin_contains,
                       SIG("contains(s str, sub str)") "bool");
  register_builtin_sig("startswith", builtin_startswith,
                       SIG("startswith(s str, pre str)") "bool");
  register_builtin_sig("endswith", builtin_endswith,
                       SIG("endswith(s str, suf str)") "bool");
  register_builtin_sig("trim", builtin_trim, SIG("trim(s str)") "str");
  register_builtin_sig("upper", builtin_upper, SIG("upper(s str)") "str");
  register_builtin_sig("lower", builtin_lower, SIG("lower(s str)") "str");
  register_builtin_sig("split", builtin_split,
                       SIG("split(s str, sep str)") "arr");
  register_builtin_sig("replace", builtin_replace,
                       SIG("replace(s str, old str, new str)") "str");

  /* Numeric */
  register_builtin_sig("abs", builtin_abs, SIG("abs(n)") "num");
  register_builtin_sig("min", builtin_min, SIG("min(a, b)") "num");
  register_builtin_sig("max", builtin_max, SIG("max(a, b)") "num");

  /* Registry function-call forms (enable dynamic path computation) */
  register_builtin_sig("reg_get", builtin_reg_get,
                       SIG("reg_get(path str)") "value");
  register_builtin_sig("reg_set", builtin_reg_set_fn,
                       SIG("reg_set(path str, value)") "nil");
  register_builtin_sig("reg_del", builtin_reg_del,
                       SIG("reg_del(path str)") "nil");
  register_builtin_sig("arr_join", builtin_arr_join,
                       SIG("arr_join(arr, sep str)") "str");
  register_builtin_sig("arr_ins", builtin_arr_ins,
                       SIG("arr_ins(arr, idx int, val)") "nil");
  register_builtin_sig("arr_del", builtin_arr_del,
                       SIG("arr_del(arr, idx int)") "nil");
  register_builtin_sig("arr_create_anon", builtin_arr_create_anon,
                       SIG("arr_create_anon()") "arr");
  register_builtin_sig("keys", builtin_keys, SIG("keys(path str)") "arr");
  register_builtin_sig("print_reg", builtin_print_reg,
                       SIG("print_reg(path str)") "nil");
  register_builtin_sig("lines_of", builtin_lines_of,
                       SIG("lines_of(s str)") "arr");

  /* IPC — fn-call forms; keyword forms use AST_SEND_MSG / AST_RECV_MSG */
  register_builtin_sig("sendmessage", nexs_builtin_sendmsg,
                       SIG("sendmessage(path str, value)") "int");
  register_builtin_sig("receivemessage", nexs_builtin_recvmsg,
                       SIG("receivemessage(path str)") "value");
  register_builtin_sig("msgpending", nexs_builtin_msgpending,
                       SIG("msgpending(path str)") "int");

  /* Pointers */
  register_builtin_sig("mkptr", nexs_builtin_ptr, SIG("mkptr(path str)") "ptr");
  register_builtin_sig("deref", nexs_builtin_deref,
                       SIG("deref(path str)") "value");

  register_builtin_sig("sleep", builtin_sleep, SIG("sleep(ms int)") "nil");

  register_builtin_sig("sys_debug", builtin_sys_debug,
                       SIG("sys_debug(on bool)") "bool");
  register_builtin_sig("sys_bundled", builtin_sys_bundled,
                       SIG("sys_bundled()") "arr");
  register_builtin_sig("sys_embedded_read", builtin_sys_embedded_read,
                       SIG("sys_embedded_read(path str)") "str");

  /* Text Buffer library */
  register_builtin_sig("tb_create", builtin_tb_create,
                       SIG("tb_create()") "arr");
  register_builtin_sig("tb_load", builtin_tb_load,
                       SIG("tb_load(buf arr, content str)") "int");
  register_builtin_sig("tb_to_str", builtin_tb_to_str,
                       SIG("tb_to_str(buf arr)") "str");
  register_builtin_sig(
      "tb_insert_char", builtin_tb_insert_char,
      SIG("tb_insert_char(buf arr, y int, x int, ch str)") "nil");
  register_builtin_sig("tb_delete_char", builtin_tb_delete_char,
                       SIG("tb_delete_char(buf arr, y int, x int)") "int");
}

#undef SIG
