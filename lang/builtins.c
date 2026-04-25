/*
 * lang/builtins.c — Standard Built-in Functions
 * ================================================
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

#include "include/nexs_fn.h"
#include "include/nexs_eval.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <ctype.h>
#include <stdio.h>
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
   EVAL BUILT-IN
   ========================================================= */

static Value builtin_eval(Value *args, int n) {
  if (n < 1) return val_err(4, "eval: requires a string argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "eval: argument must be a string");

  EvalCtx ctx;
  eval_ctx_init(&ctx);
  ctx.out = nexs_g_eval_ctx ? nexs_g_eval_ctx->out : stdout;
  ctx.err = nexs_g_eval_ctx ? nexs_g_eval_ctx->err : stderr;

  EvalResult r = eval_str(&ctx, (char *)args[0].data);
  Value ret = val_clone(&r.ret_val);
  val_free(&r.ret_val);
  return ret;
}

/* =========================================================
   STANDARD BUILT-INS
   ========================================================= */

static Value builtin_str(Value *args, int n) {
  if (n < 1) return val_err(4, "str: requires 1 argument");
  char buf[MAX_STR_LEN];
  switch (args[0].type) {
  case TYPE_INT:   snprintf(buf, sizeof(buf), "%lld", (long long)args[0].ival); break;
  case TYPE_FLOAT: snprintf(buf, sizeof(buf), "%g",   args[0].fval); break;
  case TYPE_BOOL:  snprintf(buf, sizeof(buf), "%s",   args[0].ival ? "true" : "false"); break;
  case TYPE_STR:   return val_clone(&args[0]);
  case TYPE_NIL:   return val_str("nil");
  default:         snprintf(buf, sizeof(buf), "<%s>", val_type_name(args[0].type));
  }
  return val_str(buf);
}

static Value builtin_int_(Value *args, int n) {
  if (n < 1) return val_err(4, "int: requires 1 argument");
  return val_int(val_to_int(&args[0]));
}

static Value builtin_float_(Value *args, int n) {
  if (n < 1) return val_err(4, "float: requires 1 argument");
  return val_float(val_to_float(&args[0]));
}

static Value builtin_len(Value *args, int n) {
  if (n < 1) return val_err(4, "len: requires 1 argument");
  if (args[0].type == TYPE_STR && args[0].data)
    return val_int((int64_t)strlen((char *)args[0].data));
  if (args[0].type == TYPE_ARR && args[0].data)
    return val_int((int64_t)((DynArray *)args[0].data)->size);
  if (args[0].type == TYPE_NIL) return val_int(0);
  return val_err(4, "len: unsupported type");
}

static Value builtin_type(Value *args, int n) {
  if (n < 1) return val_err(4, "type: requires 1 argument");
  return val_str(val_type_name(args[0].type));
}

static Value builtin_buddy_stats(Value *args, int n) {
  (void)args; (void)n;
  buddy_dump_stats(stdout);
  return val_nil();
}

static Value builtin_errstr(Value *args, int n) {
  (void)args; (void)n;
  extern void nexs_errstr(char *buf, int nbuf);
  char buf[256];
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
  if (n < 1) return val_err(4, "substr: requires at least 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "substr: first argument must be a string");
  const char *s = (const char *)args[0].data;
  int64_t slen  = (int64_t)strlen(s);
  int64_t start = (n >= 2) ? val_to_int(&args[1]) : 0;
  int64_t count = (n >= 3) ? val_to_int(&args[2]) : slen;

  /* Wrap negative start from end */
  if (start < 0) start = slen + start;
  if (start < 0) start = 0;
  if (start >= slen) return val_str("");
  if (count < 0) count = 0;
  if (start + count > slen) count = slen - start;

  char buf[MAX_STR_LEN];
  if (count >= MAX_STR_LEN) count = MAX_STR_LEN - 1;
  strncpy(buf, s + start, (size_t)count);
  buf[count] = '\0';
  return val_str(buf);
}

/*
 * contains(s, sub) → bool
 */
static Value builtin_contains(Value *args, int n) {
  if (n < 2) return val_err(4, "contains: requires 2 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_err(4, "contains: both arguments must be strings");
  const char *s   = args[0].data ? (const char *)args[0].data : "";
  const char *sub = args[1].data ? (const char *)args[1].data : "";
  return val_bool(strstr(s, sub) != NULL);
}

/*
 * trim(s) → str  (strip leading and trailing whitespace)
 */
static Value builtin_trim(Value *args, int n) {
  if (n < 1) return val_err(4, "trim: requires 1 argument");
  if (args[0].type != TYPE_STR)
    return val_err(4, "trim: argument must be a string");
  const char *s = args[0].data ? (const char *)args[0].data : "";
  while (isspace((unsigned char)*s)) s++;
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
  if (n < 1) return val_err(4, "upper: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data) return val_str("");
  char buf[MAX_STR_LEN];
  const char *s = (const char *)args[0].data;
  size_t i;
  for (i = 0; s[i] && i < MAX_STR_LEN - 1; i++)
    buf[i] = (char)toupper((unsigned char)s[i]);
  buf[i] = '\0';
  return val_str(buf);
}

static Value builtin_lower(Value *args, int n) {
  if (n < 1) return val_err(4, "lower: requires 1 argument");
  if (args[0].type != TYPE_STR || !args[0].data) return val_str("");
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
  if (n < 2) return val_err(4, "split: requires string and separator");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR)
    return val_err(4, "split: both arguments must be strings");
  const char *s   = args[0].data ? (const char *)args[0].data : "";
  const char *sep = args[1].data ? (const char *)args[1].data : "";
  size_t seplen   = strlen(sep);

  /* Build a temporary array */
  static int split_counter = 0;
  char arr_name[NAME_LEN];
  snprintf(arr_name, sizeof(arr_name), "__split_%d__", split_counter++);
  DynArray *arr = arr_get_or_create(arr_name);

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
      if (chunk >= MAX_STR_LEN) chunk = MAX_STR_LEN - 1;
      strncpy(buf, p, chunk);
      buf[chunk] = '\0';
      arr_set(arr, idx++, val_str(buf));
      p = found + seplen;
    }
    /* Last segment */
    arr_set(arr, idx, val_str(p));
  }

  Value v;
  v.type = TYPE_ARR; v.data = arr; v.ival = 0;
  v.fval = 0; v.err_code = 0; v.err_msg = NULL;
  return v;
}

/*
 * replace(s, old, new) → str
 * Replace first occurrence of old with new.
 */
static Value builtin_replace(Value *args, int n) {
  if (n < 3) return val_err(4, "replace: requires 3 arguments");
  if (args[0].type != TYPE_STR || args[1].type != TYPE_STR || args[2].type != TYPE_STR)
    return val_err(4, "replace: all arguments must be strings");
  const char *s   = args[0].data ? (const char *)args[0].data : "";
  const char *old = args[1].data ? (const char *)args[1].data : "";
  const char *rep = args[2].data ? (const char *)args[2].data : "";
  size_t oldlen   = strlen(old);

  if (oldlen == 0) return val_clone(&args[0]);
  const char *found = strstr(s, old);
  if (!found) return val_clone(&args[0]);

  char buf[MAX_STR_LEN * 2];
  size_t prefix = (size_t)(found - s);
  size_t replen = strlen(rep);
  size_t suffix_off = prefix + oldlen;
  size_t total = prefix + replen + strlen(s + suffix_off);
  if (total >= sizeof(buf)) total = sizeof(buf) - 1;
  snprintf(buf, sizeof(buf), "%.*s%.*s%s",
           (int)prefix, s,
           (int)replen, rep,
           s + suffix_off);
  buf[total] = '\0';
  return val_str(buf);
}

/* =========================================================
   NUMERIC BUILT-INS
   ========================================================= */

static Value builtin_abs(Value *args, int n) {
  if (n < 1) return val_err(4, "abs: requires 1 argument");
  if (args[0].type == TYPE_FLOAT)
    return val_float(args[0].fval < 0.0 ? -args[0].fval : args[0].fval);
  int64_t v = val_to_int(&args[0]);
  return val_int(v < 0 ? -v : v);
}

static Value builtin_min(Value *args, int n) {
  if (n < 2) return val_err(4, "min: requires 2 arguments");
  if (args[0].type == TYPE_FLOAT || args[1].type == TYPE_FLOAT) {
    double a = val_to_float(&args[0]), b = val_to_float(&args[1]);
    return val_float(a < b ? a : b);
  }
  int64_t a = val_to_int(&args[0]), b = val_to_int(&args[1]);
  return val_int(a < b ? a : b);
}

static Value builtin_max(Value *args, int n) {
  if (n < 2) return val_err(4, "max: requires 2 arguments");
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
static Value builtin_keys(Value *args, int n) {
  if (n < 1) return val_err(4, "keys: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "keys: argument must be a string path");

  RegKey *k = reg_lookup((char *)args[0].data);
  if (!k) return val_err(4, "keys: path not found");

  static int keys_counter = 0;
  char arr_name[NAME_LEN];
  snprintf(arr_name, sizeof(arr_name), "__keys_%d__", keys_counter++);
  DynArray *arr = arr_get_or_create(arr_name);

  size_t idx = 0;
  for (RegKey *c = k->children; c; c = c->next)
    arr_set(arr, idx++, val_str(c->name));

  Value v;
  v.type = TYPE_ARR; v.data = arr; v.ival = 0;
  v.fval = 0; v.err_code = 0; v.err_msg = NULL;
  return v;
}

/*
 * reg_get(path) → value   (function-call form of `reg /path`)
 * Useful when path is a dynamic string: reg_get("/sys/pm/proc/" + str(pid))
 */
static Value builtin_reg_get(Value *args, int n) {
  if (n < 1) return val_err(4, "reg_get: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_get: argument must be a string path");
  return reg_get((char *)args[0].data);
}

/*
 * reg_set_fn(path, value) → nil   (function-call form of `reg /path = val`)
 * Enables dynamic registry writes with computed paths.
 */
static Value builtin_reg_set_fn(Value *args, int n) {
  if (n < 2) return val_err(4, "reg_set: requires path and value");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_set: first argument must be a string path");
  reg_set((char *)args[0].data, args[1], RK_ALL);
  return val_nil();
}

/*
 * reg_del(path) → nil   (function-call form of reg_delete)
 */
static Value builtin_reg_del(Value *args, int n) {
  if (n < 1) return val_err(4, "reg_del: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "reg_del: argument must be a string path");
  reg_delete((char *)args[0].data);
  return val_nil();
}

/* =========================================================
   IPC BUILT-INS
   ========================================================= */

static Value nexs_builtin_sendmsg(Value *args, int n) {
  if (n < 2) return val_err(4, "sendmsg: requires path and value");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "sendmsg: first argument must be a string path");
  int rc = reg_ipc_send((char *)args[0].data, args[1]);
  return val_int(rc);
}

static Value nexs_builtin_recvmsg(Value *args, int n) {
  if (n < 1) return val_err(4, "recvmsg: requires path");
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
  if (n < 1) return val_err(4, "msgpending: requires path");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "msgpending: argument must be a string path");
  int count = reg_ipc_pending((char *)args[0].data);
  return val_int(count);
}

/* =========================================================
   POINTER BUILT-INS
   ========================================================= */

static Value nexs_builtin_ptr(Value *args, int n) {
  if (n < 1) return val_err(4, "ptr: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "ptr: argument must be a string path");
  return val_ptr((char *)args[0].data);
}

static Value nexs_builtin_deref(Value *args, int n) {
  if (n < 1) return val_err(4, "deref: requires a path string");
  if (args[0].type != TYPE_STR || !args[0].data)
    return val_err(4, "deref: argument must be a string path");
  return reg_get_deref((char *)args[0].data);
}

/* =========================================================
   REGISTRATION
   ========================================================= */

/* Arrow UTF-8 → (U+2192) */
#define SIG(s) s " \xe2\x86\x92 "

void builtins_register_all(void) {
  /* Core type conversion / execution */
  register_builtin_sig("eval",        builtin_eval,
    SIG("eval(src str)") "value");
  register_builtin_sig("str",         builtin_str,
    SIG("str(value)") "str");
  register_builtin_sig("int",         builtin_int_,
    SIG("int(value)") "int");
  register_builtin_sig("float",       builtin_float_,
    SIG("float(value)") "float");
  register_builtin_sig("len",         builtin_len,
    SIG("len(str|arr)") "int");
  register_builtin_sig("type",        builtin_type,
    SIG("type(value)") "str");
  register_builtin_sig("buddy_stats", builtin_buddy_stats,
    SIG("buddy_stats()") "nil");
  register_builtin_sig("errstr",      builtin_errstr,
    SIG("errstr()") "str");

  /* String manipulation */
  register_builtin_sig("substr",   builtin_substr,
    SIG("substr(s str, start int, len int)") "str");
  register_builtin_sig("contains", builtin_contains,
    SIG("contains(s str, sub str)") "bool");
  register_builtin_sig("trim",     builtin_trim,
    SIG("trim(s str)") "str");
  register_builtin_sig("upper",    builtin_upper,
    SIG("upper(s str)") "str");
  register_builtin_sig("lower",    builtin_lower,
    SIG("lower(s str)") "str");
  register_builtin_sig("split",    builtin_split,
    SIG("split(s str, sep str)") "arr");
  register_builtin_sig("replace",  builtin_replace,
    SIG("replace(s str, old str, new str)") "str");

  /* Numeric */
  register_builtin_sig("abs",  builtin_abs,
    SIG("abs(n)") "num");
  register_builtin_sig("min",  builtin_min,
    SIG("min(a, b)") "num");
  register_builtin_sig("max",  builtin_max,
    SIG("max(a, b)") "num");

  /* Registry function-call forms (enable dynamic path computation) */
  register_builtin_sig("reg_get",  builtin_reg_get,
    SIG("reg_get(path str)") "value");
  register_builtin_sig("reg_set",  builtin_reg_set_fn,
    SIG("reg_set(path str, value)") "nil");
  register_builtin_sig("reg_del",  builtin_reg_del,
    SIG("reg_del(path str)") "nil");
  register_builtin_sig("keys",     builtin_keys,
    SIG("keys(path str)") "arr");

  /* IPC — fn-call forms; keyword forms use AST_SEND_MSG / AST_RECV_MSG */
  register_builtin_sig("sendmessage",    nexs_builtin_sendmsg,
    SIG("sendmessage(path str, value)") "int");
  register_builtin_sig("receivemessage", nexs_builtin_recvmsg,
    SIG("receivemessage(path str)") "value");
  register_builtin_sig("msgpending",     nexs_builtin_msgpending,
    SIG("msgpending(path str)") "int");

  /* Pointers */
  register_builtin_sig("mkptr",  nexs_builtin_ptr,
    SIG("mkptr(path str)") "ptr");
  register_builtin_sig("deref",  nexs_builtin_deref,
    SIG("deref(path str)") "value");
}

#undef SIG
