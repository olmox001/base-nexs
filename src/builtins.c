/*
 * builtins.c — Standard Built-in Functions
 * ==========================================
 * Registra le funzioni built-in base in /sys/:
 *   str, int, float, len, type, buddy_stats
 */

#include "basereg.h"

#include <string.h>

/* =========================================================
   BUILTIN REGISTRATION HELPER
   ========================================================= */

void reg_register_builtin(const char *name, BuiltinFn fn) {
  if (!name || !fn)
    return;
  char path[REG_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", REG_SYS, name);
  Value v;
  v.type = TYPE_FN;
  v.data = (void *)fn;
  v.ival = 1;
  v.fval = 0.0;
  v.err_code = 0;
  v.err_msg = NULL;
  reg_set(path, v, RK_READ | RK_EXEC);
}

/* =========================================================
   STANDARD BUILTINS
   ========================================================= */

static Value builtin_str(Value *args, int n) {
  if (n < 1)
    return val_err(4, "str: richiede 1 argomento");
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
  default:
    snprintf(buf, sizeof(buf), "<%s>", val_type_name(args[0].type));
  }
  return val_str(buf);
}

static Value builtin_int_(Value *args, int n) {
  if (n < 1)
    return val_err(4, "int: richiede 1 argomento");
  return val_int(val_to_int(&args[0]));
}

static Value builtin_float_(Value *args, int n) {
  if (n < 1)
    return val_err(4, "float: richiede 1 argomento");
  return val_float(val_to_float(&args[0]));
}

static Value builtin_len(Value *args, int n) {
  if (n < 1)
    return val_err(4, "len: richiede 1 argomento");
  if (args[0].type == TYPE_STR && args[0].data)
    return val_int((int64_t)strlen((char *)args[0].data));
  if (args[0].type == TYPE_ARR && args[0].data)
    return val_int((int64_t)((DynArray *)args[0].data)->size);
  return val_err(4, "len: tipo non supportato");
}

static Value builtin_type(Value *args, int n) {
  if (n < 1)
    return val_err(4, "type: richiede 1 argomento");
  return val_str(val_type_name(args[0].type));
}

static Value builtin_buddy_stats(Value *args, int n) {
  (void)args;
  (void)n;
  buddy_dump_stats(stdout);
  return val_nil();
}

/* errstr() — ritorna l'ultimo errore come stringa */
static Value builtin_errstr(Value *args, int n) {
  (void)args;
  (void)n;
  char buf[256];
  nexs_errstr(buf, sizeof(buf));
  return val_str(buf);
}

/* =========================================================
   REGISTRATION
   ========================================================= */

void builtins_register_all(void) {
  reg_register_builtin("str", builtin_str);
  reg_register_builtin("int", builtin_int_);
  reg_register_builtin("float", builtin_float_);
  reg_register_builtin("len", builtin_len);
  reg_register_builtin("type", builtin_type);
  reg_register_builtin("buddy_stats", builtin_buddy_stats);
  reg_register_builtin("errstr", builtin_errstr);
}
