/*
 * lang/builtins.c — Standard Built-in Functions
 * ================================================
 * Registers built-ins via fn_register_builtin() AND reg_register_builtin()
 * (both paths kept for compatibility with the registry-based dispatcher).
 *
 * New builtins: sendmsg, recvmsg, msgpending, ptr, deref.
 */

#include "include/nexs_fn.h"
#include "include/nexs_eval.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
   HELPER — register into both fn_table and registry /sys/
   ========================================================= */

static void register_builtin(const char *name, BuiltinFn fn) {
  /* Register in fn_table for fast direct lookup */
  fn_register_builtin(name, fn);
  /* Also register in registry /sys/<name> for legacy resolution */
  reg_register_builtin(name, (void *)fn);
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

/* errstr() */
static Value builtin_errstr(Value *args, int n) {
  (void)args; (void)n;
  /* Forward declaration — sysio provides nexs_errstr */
  extern void nexs_errstr(char *buf, int nbuf);
  char buf[256];
  nexs_errstr(buf, sizeof(buf));
  return val_str(buf);
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

void builtins_register_all(void) {
  register_builtin("str",          builtin_str);
  register_builtin("int",          builtin_int_);
  register_builtin("float",        builtin_float_);
  register_builtin("len",          builtin_len);
  register_builtin("type",         builtin_type);
  register_builtin("buddy_stats",  builtin_buddy_stats);
  register_builtin("errstr",       builtin_errstr);

  /* IPC */
  register_builtin("sendmsg",      nexs_builtin_sendmsg);
  register_builtin("recvmsg",      nexs_builtin_recvmsg);
  register_builtin("msgpending",   nexs_builtin_msgpending);

  /* Pointers */
  register_builtin("mkptr",        nexs_builtin_ptr);
  register_builtin("deref",        nexs_builtin_deref);
}
