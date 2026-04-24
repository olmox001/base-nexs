/*
 * value.c — Value System Implementation
 * =======================================
 * Tipi: NIL INT FLOAT STR ARR FN ERR BOOL REF
 * Costruttori, predicati, aritmetica, stampa, clone, free.
 *
 * BUG FIXES applicati:
 *   - val_print TYPE_ARR: usa v->data come DynArray* (non lookup per nome)
 *   - val_equal TYPE_STR: controlla NULL prima di strcmp
 *   - val_to_int/val_to_float TYPE_STR: controlla data NULL
 *   - val_add: supporta concatenazione stringhe
 */

#include "basereg.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================
   COSTRUTTORI
   ========================================================= */

const char *val_type_name(ValueType t) {
  switch (t) {
  case TYPE_NIL:   return "nil";
  case TYPE_INT:   return "int";
  case TYPE_FLOAT: return "float";
  case TYPE_STR:   return "str";
  case TYPE_ARR:   return "arr";
  case TYPE_FN:    return "fn";
  case TYPE_ERR:   return "err";
  case TYPE_BOOL:  return "bool";
  case TYPE_REF:   return "ref";
  default:         return "?";
  }
}

Value val_nil(void) { return (Value){TYPE_NIL, NULL, 0, 0.0, 0, NULL}; }
Value val_bool(int b) {
  return (Value){TYPE_BOOL, NULL, b ? 1 : 0, 0.0, 0, NULL};
}
Value val_int(int64_t n) { return (Value){TYPE_INT, NULL, n, 0.0, 0, NULL}; }
Value val_float(double f) { return (Value){TYPE_FLOAT, NULL, 0, f, 0, NULL}; }

Value val_str(const char *s) {
  Value v = {TYPE_STR, NULL, 0, 0.0, 0, NULL};
  if (s)
    v.data = buddy_strdup(s);
  return v;
}

Value val_err(int code, const char *msg) {
  Value v = {TYPE_ERR, NULL, 0, 0.0, code, NULL};
  if (msg)
    v.err_msg = buddy_strdup(msg);
  return v;
}

Value val_ref(const char *path) {
  Value v = {TYPE_REF, NULL, 0, 0.0, 0, NULL};
  if (path)
    v.data = buddy_strdup(path);
  return v;
}

/* =========================================================
   PREDICATI
   ========================================================= */

int val_is_error(const Value *v) {
  return v && v->type == TYPE_ERR;
}

int val_is_truthy(const Value *v) {
  if (!v)
    return 0;
  switch (v->type) {
  case TYPE_NIL:   return 0;
  case TYPE_BOOL:  return v->ival != 0;
  case TYPE_INT:   return v->ival != 0;
  case TYPE_FLOAT: return v->fval != 0.0;
  case TYPE_STR:   return v->data && ((char *)v->data)[0] != '\0';
  case TYPE_ERR:   return 0;
  default:         return 1;
  }
}

int val_equal(const Value *a, const Value *b) {
  if (!a || !b)
    return 0;
  if (a->type != b->type)
    return 0;
  switch (a->type) {
  case TYPE_NIL:  return 1;
  case TYPE_BOOL: return a->ival == b->ival;
  case TYPE_INT:  return a->ival == b->ival;
  case TYPE_FLOAT: return a->fval == b->fval;
  case TYPE_STR:
    /* FIX: NULL guard before strcmp */
    if (!a->data && !b->data) return 1;
    if (!a->data || !b->data) return 0;
    return strcmp((char *)a->data, (char *)b->data) == 0;
  default: return 0;
  }
}

/* =========================================================
   CONVERSIONI
   ========================================================= */

int64_t val_to_int(const Value *v) {
  if (!v)
    return 0;
  switch (v->type) {
  case TYPE_INT:   return v->ival;
  case TYPE_FLOAT: return (int64_t)v->fval;
  case TYPE_BOOL:  return v->ival;
  case TYPE_STR:
    /* FIX: NULL guard */
    return v->data ? atoll((char *)v->data) : 0;
  default: return 0;
  }
}

double val_to_float(const Value *v) {
  if (!v)
    return 0.0;
  switch (v->type) {
  case TYPE_INT:   return (double)v->ival;
  case TYPE_FLOAT: return v->fval;
  case TYPE_BOOL:  return (double)v->ival;
  case TYPE_STR:
    /* FIX: NULL guard */
    return v->data ? atof((char *)v->data) : 0.0;
  default: return 0.0;
  }
}

/* =========================================================
   STAMPA
   ========================================================= */

void val_print(const Value *v, FILE *out) {
  if (!v || !out)
    return;
  switch (v->type) {
  case TYPE_NIL:
    fprintf(out, "nil");
    break;
  case TYPE_BOOL:
    fprintf(out, "%s", v->ival ? "true" : "false");
    break;
  case TYPE_INT:
    fprintf(out, "%lld", (long long)v->ival);
    break;
  case TYPE_FLOAT:
    fprintf(out, "%g", v->fval);
    break;
  case TYPE_STR:
    fprintf(out, "%s", v->data ? (char *)v->data : "");
    break;
  case TYPE_ARR: {
    /*
     * FIX: v->data è direttamente un DynArray*, non un nome.
     * Il vecchio codice faceva arr_get((char*)v->data) che crashava.
     */
    DynArray *arr = (DynArray *)v->data;
    if (!arr) {
      fprintf(out, "[]");
    } else {
      fprintf(out, "[");
      for (size_t i = 0; i < arr->size; i++) {
        val_print(&arr->items[i], out);
        if (i < arr->size - 1)
          fprintf(out, ", ");
      }
      fprintf(out, "]");
    }
    break;
  }
  case TYPE_FN:
    if (v->data) {
      if (v->ival == 0) {
        ASTNode *fn_node = (ASTNode *)v->data;
        fprintf(out, "<fn %s(", fn_node->name[0] ? fn_node->name : "anonymous");
        for (int i = 0; i < fn_node->n_params; i++) {
          fprintf(out, "%s%s", i > 0 ? ", " : "", fn_node->params[i]);
        }
        fprintf(out, ")>");
      } else {
        fprintf(out, "<builtin fn>");
      }
    } else {
      fprintf(out, "<fn>");
    }
    break;
  case TYPE_REF:
    fprintf(out, "<ref:%s>", v->data ? (char *)v->data : "?");
    break;
  case TYPE_ERR:
    fprintf(out, "ERR(%d: %s)", v->err_code, v->err_msg ? v->err_msg : "");
    break;
  }
}

/* =========================================================
   GESTIONE MEMORIA
   ========================================================= */

void val_free(Value *v) {
  if (!v)
    return;
  if (v->type == TYPE_STR && v->data) {
    xfree(v->data);
    v->data = NULL;
  }
  if (v->type == TYPE_REF && v->data) {
    xfree(v->data);
    v->data = NULL;
  }
  if (v->type == TYPE_ERR && v->err_msg) {
    xfree(v->err_msg);
    v->err_msg = NULL;
  }
  /* TYPE_ARR e TYPE_FN: la vita è gestita dal registry/DynArray */
}

Value val_clone(const Value *v) {
  if (!v)
    return val_nil();
  Value c = *v;
  if (v->type == TYPE_STR && v->data)
    c.data = buddy_strdup(v->data);
  if (v->type == TYPE_REF && v->data)
    c.data = buddy_strdup(v->data);
  if (v->type == TYPE_ERR && v->err_msg)
    c.err_msg = buddy_strdup(v->err_msg);
  return c;
}

/* =========================================================
   ARITMETICA
   ========================================================= */

/*
 * val_add: supporta concatenazione stringhe.
 * FIX: il vecchio codice convertiva sempre a int/float.
 */
Value val_add(const Value *a, const Value *b) {
  /* String concatenation */
  if (a->type == TYPE_STR || b->type == TYPE_STR) {
    char buf_a[MAX_STR_LEN], buf_b[MAX_STR_LEN];
    if (a->type == TYPE_STR && a->data) {
      strncpy(buf_a, (char *)a->data, MAX_STR_LEN - 1);
      buf_a[MAX_STR_LEN - 1] = '\0';
    } else {
      /* Convert non-string to string representation */
      switch (a->type) {
      case TYPE_INT:   snprintf(buf_a, sizeof(buf_a), "%lld", (long long)a->ival); break;
      case TYPE_FLOAT: snprintf(buf_a, sizeof(buf_a), "%g", a->fval); break;
      case TYPE_BOOL:  snprintf(buf_a, sizeof(buf_a), "%s", a->ival ? "true" : "false"); break;
      case TYPE_NIL:   snprintf(buf_a, sizeof(buf_a), "nil"); break;
      default:         snprintf(buf_a, sizeof(buf_a), "<%s>", val_type_name(a->type)); break;
      }
    }
    if (b->type == TYPE_STR && b->data) {
      strncpy(buf_b, (char *)b->data, MAX_STR_LEN - 1);
      buf_b[MAX_STR_LEN - 1] = '\0';
    } else {
      switch (b->type) {
      case TYPE_INT:   snprintf(buf_b, sizeof(buf_b), "%lld", (long long)b->ival); break;
      case TYPE_FLOAT: snprintf(buf_b, sizeof(buf_b), "%g", b->fval); break;
      case TYPE_BOOL:  snprintf(buf_b, sizeof(buf_b), "%s", b->ival ? "true" : "false"); break;
      case TYPE_NIL:   snprintf(buf_b, sizeof(buf_b), "nil"); break;
      default:         snprintf(buf_b, sizeof(buf_b), "<%s>", val_type_name(b->type)); break;
      }
    }
    char result[MAX_STR_LEN * 2];
    snprintf(result, sizeof(result), "%s%s", buf_a, buf_b);
    return val_str(result);
  }
  /* Numeric addition */
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)
    return val_float(val_to_float(a) + val_to_float(b));
  return val_int(val_to_int(a) + val_to_int(b));
}

#define ARITH_OP(name, op)                                                     \
  Value val_##name(const Value *a, const Value *b) {                           \
    if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)                        \
      return val_float(val_to_float(a) op val_to_float(b));                    \
    return val_int(val_to_int(a) op val_to_int(b));                            \
  }
ARITH_OP(sub, -)
ARITH_OP(mul, *)

Value val_div(const Value *a, const Value *b) {
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT) {
    double dv = val_to_float(b);
    if (dv == 0.0)
      return val_err(1, "divisione per zero");
    return val_float(val_to_float(a) / dv);
  }
  int64_t iv = val_to_int(b);
  if (iv == 0)
    return val_err(1, "divisione per zero");
  return val_int(val_to_int(a) / iv);
}

Value val_mod(const Value *a, const Value *b) {
  int64_t iv = val_to_int(b);
  if (iv == 0)
    return val_err(1, "modulo per zero");
  return val_int(val_to_int(a) % iv);
}

#define CMP_OP(name, op)                                                       \
  Value val_##name(const Value *a, const Value *b) {                           \
    if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)                        \
      return val_bool(val_to_float(a) op val_to_float(b));                     \
    return val_bool(val_to_int(a) op val_to_int(b));                           \
  }
CMP_OP(lt, <)
CMP_OP(gt, >)
CMP_OP(le, <=)
CMP_OP(ge, >=)

Value val_eq(const Value *a, const Value *b) {
  return val_bool(val_equal(a, b));
}
Value val_ne(const Value *a, const Value *b) {
  return val_bool(!val_equal(a, b));
}
Value val_and(const Value *a, const Value *b) {
  return val_bool(val_is_truthy(a) && val_is_truthy(b));
}
Value val_or(const Value *a, const Value *b) {
  return val_bool(val_is_truthy(a) || val_is_truthy(b));
}
Value val_not(const Value *a) { return val_bool(!val_is_truthy(a)); }
