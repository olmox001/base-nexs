/*
 * core/value.c — Value System Implementation
 * ============================================
 * Types: NIL INT FLOAT STR ARR FN ERR BOOL REF PTR
 */

#include "include/nexs_value.h"
#include "include/nexs_alloc.h"
#include "include/nexs_checked.h"
#include "include/nexs_common.h"
#include "include/nexs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lifetime belongs to registry Value; language owns the callable class. */
void (*nexs_val_fn_print)(const void *object, FILE *out) = NULL;
int (*nexs_val_fn_retain)(void *object) = NULL;
void (*nexs_val_fn_release)(void *object) = NULL;

/* =========================================================
   TYPE NAME
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
  case TYPE_PTR:   return "ptr";
  default:         return "?";
  }
}

/* =========================================================
   CONSTRUCTORS
   ========================================================= */

Value val_nil(void)       { return (Value){TYPE_NIL, NULL, 0, 0.0, 0, NULL}; }
Value val_bool(int b)     { return (Value){TYPE_BOOL, NULL, b ? 1 : 0, 0.0, 0, NULL}; }
Value val_int(int64_t n)  { return (Value){TYPE_INT, NULL, n, 0.0, 0, NULL}; }
Value val_float(double f) { return (Value){TYPE_FLOAT, NULL, 0, f, 0, NULL}; }

Value val_str(const char *s) {
  Value v = {TYPE_STR, NULL, 0, 0.0, 0, NULL};
  if (s != NULL) {
    v.data = buddy_strdup(s);
    if (v.data == NULL)
      return val_err(12, "string allocation failed");
  }
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
  if (path != NULL) {
    v.data = buddy_strdup(path);
    if (v.data == NULL)
      return val_err(12, "reference allocation failed");
  }
  return v;
}

Value val_ptr(const char *target) {
  Value v = {TYPE_PTR, NULL, 0, 0.0, 0, NULL};
  if (target != NULL) {
    v.data = buddy_strdup(target);
    if (v.data == NULL)
      return val_err(12, "pointer allocation failed");
  }
  return v;
}

Value val_fn_borrow(void *fn_object) {
  return (Value){TYPE_FN, fn_object, 0, 0.0, 0, NULL};
}

/* =========================================================
   PREDICATES
   ========================================================= */

int val_is_error(const Value *v) {
  return v && v->type == TYPE_ERR;
}

int val_is_truthy(const Value *v) {
  if (!v) return 0;
  switch (v->type) {
  case TYPE_NIL:   return 0;
  case TYPE_BOOL:  return v->ival != 0;
  case TYPE_INT:   return v->ival != 0;
  case TYPE_FLOAT: return v->fval != 0.0;
  case TYPE_STR:   return v->data && ((char *)v->data)[0] != '\0';
  case TYPE_ERR:   return 0;
  case TYPE_PTR:   return v->data != NULL;
  default:         return 1;
  }
}

int val_equal(const Value *a, const Value *b) {
  if (!a || !b) return 0;
  if (a->type != b->type) return 0;
  switch (a->type) {
  case TYPE_NIL:   return 1;
  case TYPE_BOOL:  return a->ival == b->ival;
  case TYPE_INT:   return a->ival == b->ival;
  case TYPE_FLOAT: return a->fval == b->fval;
  case TYPE_STR:
    if (!a->data && !b->data) return 1;
    if (!a->data || !b->data) return 0;
    return strcmp((char *)a->data, (char *)b->data) == 0;
  case TYPE_PTR:
    if (!a->data && !b->data) return 1;
    if (!a->data || !b->data) return 0;
    return strcmp((char *)a->data, (char *)b->data) == 0;
  default: return 0;
  }
}

/* =========================================================
   CONVERSIONS
   ========================================================= */

int64_t val_to_int(const Value *v) {
  if (!v) return 0;
  switch (v->type) {
  case TYPE_INT:   return v->ival;
  case TYPE_FLOAT:
    /* A floating-point NaN or value outside the signed range cannot
     * legally be cast to int64_t in C. Preserve the old zero fallback. */
    if (v->fval != v->fval || v->fval >= 0x1p63 ||
        v->fval < -0x1p63)
      return 0;
    return (int64_t)v->fval;
  case TYPE_BOOL:  return v->ival;
  case TYPE_STR:   return v->data ? atoll((char *)v->data) : 0;
  default:         return 0;
  }
}

double val_to_float(const Value *v) {
  if (!v) return 0.0;
  switch (v->type) {
  case TYPE_INT:   return (double)v->ival;
  case TYPE_FLOAT: return v->fval;
  case TYPE_BOOL:  return (double)v->ival;
  case TYPE_STR:   return v->data ? atof((char *)v->data) : 0.0;
  default:         return 0.0;
  }
}

/* =========================================================
   PRINT
   ========================================================= */

void val_to_str(const Value *v, char *buf, size_t sz) {
  if (!v || !buf || sz == 0) return;
  switch (v->type) {
    case TYPE_NIL:   snprintf(buf, sz, "nil"); break;
    case TYPE_BOOL:  snprintf(buf, sz, "%s", v->ival ? "true" : "false"); break;
    case TYPE_INT:   snprintf(buf, sz, "%lld", (long long)v->ival); break;
    case TYPE_FLOAT: snprintf(buf, sz, "%g", v->fval); break;
    case TYPE_STR:   snprintf(buf, sz, "%s", v->data ? (char *)v->data : ""); break;
    case TYPE_ARR:   snprintf(buf, sz, "<array>"); break;
    case TYPE_FN:    snprintf(buf, sz, "<function>"); break;
    case TYPE_ERR:   snprintf(buf, sz, "ERR(%s)", v->err_msg ? v->err_msg : ""); break;
    case TYPE_REF:   snprintf(buf, sz, "REF(%s)", v->data ? (char *)v->data : ""); break;
    case TYPE_PTR:   snprintf(buf, sz, "PTR(%s)", v->data ? (char *)v->data : ""); break;
    default:         snprintf(buf, sz, "<unknown>"); break;
  }
}

void val_print(const Value *v, FILE *out) {
  if (!v) return;
  switch (v->type) {
  case TYPE_NIL:
    nexs_fprintf(out, "nil");
    break;
  case TYPE_BOOL:
    nexs_fprintf(out, "%s", v->ival ? "true" : "false");
    break;
  case TYPE_INT:
    nexs_fprintf(out, "%lld", (long long)v->ival);
    break;
  case TYPE_FLOAT:
    nexs_fprintf(out, "%g", v->fval);
    break;
  case TYPE_STR:
    if (v->data) {
      nexs_fprintf(out, "%s", (char *)v->data);
    } else {
      nexs_fprintf(out, "(null str)");
    }
    break;
  case TYPE_ARR: {
    DynArray *arr = (DynArray *)v->data;
    if (!arr) {
      nexs_fprintf(out, "[]");
    } else {
      nexs_fprintf(out, "[");
      for (size_t i = 0; i < arr->size; i++) {
        val_print(&arr->items[i], out);
        if (i < arr->size - 1)
          nexs_fprintf(out, ", ");
      }
      nexs_fprintf(out, "]");
    }
    break;
  }
  case TYPE_FN:
    if (nexs_val_fn_print && v->data)
      nexs_val_fn_print(v->data, out);
    else
      nexs_fprintf(out, "<invalid function>");
    break;
  case TYPE_REF:
    nexs_fprintf(out, "<ref:%s>", v->data ? (char *)v->data : "?");
    break;
  case TYPE_PTR:
    nexs_fprintf(out, "<ptr:%s>", v->data ? (char *)v->data : "?");
    break;
  case TYPE_ERR:
    nexs_fprintf(out, "ERR(%d: %s)", v->err_code, v->err_msg ? v->err_msg : "");
    break;
  }
}

/* =========================================================
   MEMORY MANAGEMENT
   ========================================================= */

void val_free(Value *v) {
  if (!v) return;
  if (v->type == TYPE_STR) {
    if (v->data) xfree(v->data);
    v->data = NULL;
  } else if (v->type == TYPE_ARR) {
    if (v->data) arr_unref((DynArray *)v->data);
    v->data = NULL;
  } else if (v->type == TYPE_REF || v->type == TYPE_PTR) {
    if (v->data) xfree(v->data);
    v->data = NULL;
  } else if (v->type == TYPE_FN) {
    if (v->data && nexs_val_fn_release)
      nexs_val_fn_release(v->data);
    v->data = NULL;
  }
  if (v->type == TYPE_ERR && v->err_msg) {
    xfree(v->err_msg);
    v->err_msg = NULL;
  }
  /* Every strong TYPE_FN Value releases its registry-owned callable. */
  v->type = TYPE_NIL;
}

Value val_clone(const Value *v) {
  if (!v) return val_nil();
  Value res = *v;
  if (v->type == TYPE_STR) {
    res.data = buddy_strdup(v->data ? (const char *)v->data : "");
    if (res.data == NULL)
      return val_err(12, "string clone allocation failed");
  } else if (v->type == TYPE_ARR) {
    if (v->data) arr_ref((DynArray *)v->data);
  } else if (v->type == TYPE_REF || v->type == TYPE_PTR) {
    if (v->data != NULL) {
      res.data = buddy_strdup((const char *)v->data);
      if (res.data == NULL)
        return val_err(12, "reference clone allocation failed");
    }
  } else if (v->type == TYPE_FN && v->data) {
    if (!nexs_val_fn_retain || nexs_val_fn_retain(v->data) != 0)
      return val_err(2, "callable reference admission failed");
  }
  if (v->type == TYPE_ERR && v->err_msg) {
    res.err_msg = buddy_strdup(v->err_msg);
  } else if (v->type == TYPE_ERR) {
    res.err_msg = NULL;
  }
  return res;
}

/* One checked clone contract for the same Value owner implementation.
 * TYPE_ERR cannot serve as an OOM sentinel when the original value is an
 * error. Preserve that error's message exactly or refuse the clone. */
int val_clone_checked(const Value *source, Value *output) {
  if (source == NULL || output == NULL || source == output)
    return -1;
  Value copy = val_clone(source);
  int failed = (copy.type == TYPE_ERR && source->type != TYPE_ERR) ||
               (source->type == TYPE_ERR && source->err_msg != NULL &&
                copy.err_msg == NULL);
  if (failed) {
    val_free(&copy);
    return -1;
  }
  *output = copy;
  return 0;
}

/* =========================================================
   ARITHMETIC
   ========================================================= */

/* Convert non-string operands only into bounded scalar text.
 * Existing language string-concatenation tags are preserved. */
static const char *nexs_concat_operand(const Value *value,
                                        char buffer[64]) {
  if (value->type == TYPE_STR)
    return value->data ? (const char *)value->data : "";
  if (value->type == TYPE_INT)
    snprintf(buffer, 64U, "%lld", (long long)value->ival);
  else if (value->type == TYPE_FLOAT)
    snprintf(buffer, 64U, "%g", value->fval);
  else if (value->type == TYPE_BOOL)
    snprintf(buffer, 64U, "%s", value->ival ? "true" : "false");
  else if (value->type == TYPE_NIL)
    snprintf(buffer, 64U, "nil");
  else
    snprintf(buffer, 64U, "<%s>", val_type_name(value->type));
  return buffer;
}

Value val_add(const Value *a, const Value *b) {
  if (a == NULL || b == NULL)
    return val_err(4, "add requires two values");
  if (a->type == TYPE_STR || b->type == TYPE_STR) {
    char buffer_a[64] = {0};
    char buffer_b[64] = {0};
    const char *left = nexs_concat_operand(a, buffer_a);
    const char *right = nexs_concat_operand(b, buffer_b);
    size_t left_size = strlen(left);
    size_t right_size = strlen(right);
    size_t total = 0U;
    size_t allocation_size = 0U;
    if (nx_size_add(left_size, right_size, &total) != NX_STATUS_OK ||
        nx_size_add(total, 1U, &allocation_size) != NX_STATUS_OK)
      return val_err(6, "string concatenation size overflow");
    char *joined = buddy_alloc(allocation_size);
    if (joined == NULL)
      return val_err(12, "string concatenation allocation failed");
    memcpy(joined, left, left_size);
    memcpy(joined + left_size, right, right_size);
    joined[total] = '\0';
    Value result = val_nil();
    result.type = TYPE_STR;
    result.data = joined;
    return result;
  }
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)
    return val_float(val_to_float(a) + val_to_float(b));
  int64_t sum = 0;
  if (nx_i64_add(val_to_int(a), val_to_int(b), &sum) != NX_STATUS_OK)
    return val_err(6, "signed addition overflow");
  return val_int(sum);
}

Value val_sub(const Value *a, const Value *b) {
  if (a == NULL || b == NULL)
    return val_err(4, "sub requires two values");
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)
    return val_float(val_to_float(a) - val_to_float(b));
  int64_t difference = 0;
  if (nx_i64_sub(val_to_int(a), val_to_int(b), &difference) != NX_STATUS_OK)
    return val_err(6, "signed subtraction overflow");
  return val_int(difference);
}

Value val_mul(const Value *a, const Value *b) {
  if (a == NULL || b == NULL)
    return val_err(4, "mul requires two values");
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)
    return val_float(val_to_float(a) * val_to_float(b));
  int64_t product = 0;
  if (nx_i64_mul(val_to_int(a), val_to_int(b), &product) != NX_STATUS_OK)
    return val_err(6, "signed multiplication overflow");
  return val_int(product);
}

Value val_div(const Value *a, const Value *b) {
  if (a == NULL || b == NULL)
    return val_err(4, "div requires two values");
  if (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT) {
    double dv = val_to_float(b);
    if (dv == 0.0) return val_err(1, "division by zero");
    return val_float(val_to_float(a) / dv);
  }
  int64_t iv = val_to_int(b);
  if (iv == 0) return val_err(1, "division by zero");
  int64_t dividend = val_to_int(a);
  if (dividend == INT64_MIN && iv == -1)
    return val_err(6, "signed division overflow");
  return val_int(dividend / iv);
}

Value val_mod(const Value *a, const Value *b) {
  if (a == NULL || b == NULL)
    return val_err(4, "mod requires two values");
  int64_t iv = val_to_int(b);
  if (iv == 0) return val_err(1, "modulo by zero");
  int64_t dividend = val_to_int(a);
  if (dividend == INT64_MIN && iv == -1)
    return val_err(6, "signed modulo overflow");
  return val_int(dividend % iv);
}

/* Explicit, compiler-visible comparator implementations. The former
 * token-pasting CMP_OP macro hid four REAL function bodies from the P0
 * source and Clang AST inventories, despite linking their symbols. */
typedef enum {
  NEXS_ORDER_LT,
  NEXS_ORDER_GT,
  NEXS_ORDER_LE,
  NEXS_ORDER_GE
} NexsValueOrder;

static Value nexs_value_order(const Value *left, const Value *right,
                              NexsValueOrder order) {
  if (left == NULL || right == NULL)
    return val_err(4, "comparison requires two values");
  int relation = 0;
  if (left->type == TYPE_FLOAT || right->type == TYPE_FLOAT) {
    double a = val_to_float(left);
    double b = val_to_float(right);
    if (a != a || b != b)
      return val_bool(0); /* NaN is unordered for all four relations. */
    relation = (a > b) - (a < b);
  } else {
    int64_t a = val_to_int(left);
    int64_t b = val_to_int(right);
    relation = (a > b) - (a < b);
  }
  switch (order) {
  case NEXS_ORDER_LT:
    return val_bool(relation < 0);
  case NEXS_ORDER_GT:
    return val_bool(relation > 0);
  case NEXS_ORDER_LE:
    return val_bool(relation <= 0);
  case NEXS_ORDER_GE:
    return val_bool(relation >= 0);
  default:
    return val_err(4, "invalid ordered comparison");
  }
}

Value val_lt(const Value *a, const Value *b) {
  return nexs_value_order(a, b, NEXS_ORDER_LT);
}

Value val_gt(const Value *a, const Value *b) {
  return nexs_value_order(a, b, NEXS_ORDER_GT);
}

Value val_le(const Value *a, const Value *b) {
  return nexs_value_order(a, b, NEXS_ORDER_LE);
}

Value val_ge(const Value *a, const Value *b) {
  return nexs_value_order(a, b, NEXS_ORDER_GE);
}

Value val_eq(const Value *a, const Value *b) { return val_bool(val_equal(a, b)); }
Value val_ne(const Value *a, const Value *b) { return val_bool(!val_equal(a, b)); }
Value val_and(const Value *a, const Value *b) { return val_bool(val_is_truthy(a) && val_is_truthy(b)); }
Value val_or(const Value *a, const Value *b)  { return val_bool(val_is_truthy(a) || val_is_truthy(b)); }
Value val_not(const Value *a) { return val_bool(!val_is_truthy(a)); }
