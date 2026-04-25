/*
 * nexs_value.h — NEXS Value System
 * ===================================
 * ValueType enum, Value struct, DynArray struct, and all val_* / arr_* APIs.
 *
 * Depends on: nexs_alloc.h (for xmalloc / xfree / buddy_strdup)
 */

#ifndef NEXS_VALUE_H
#define NEXS_VALUE_H
#pragma once

#include "nexs_alloc.h"
#include "nexs_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   VALUE TYPE ENUM
   ========================================================= */

typedef enum {
  TYPE_NIL   = 0,
  TYPE_INT   = 1,
  TYPE_FLOAT = 2,
  TYPE_STR   = 3,
  TYPE_ARR   = 4,
  TYPE_FN    = 5,  /* index into fn_table (ival holds idx) */
  TYPE_ERR   = 6,
  TYPE_BOOL  = 7,
  TYPE_REF   = 8,  /* reference to a registry path (data = buddy_strdup'd path) */
  TYPE_PTR   = 9,  /* pointer to another registry key (data = buddy_strdup'd target path) */
} ValueType;

/* =========================================================
   VALUE STRUCT
   ========================================================= */

/*
 * Value — the fundamental runtime type.
 * Memory in 'data' is buddy_alloc'd (never use malloc/free).
 */
typedef struct {
  ValueType type;
  void    *data;      /* buddy_alloc'd; NULL for NIL/BOOL/INT/FLOAT */
  int64_t  ival;      /* TYPE_INT, TYPE_BOOL, TYPE_FN (fn_table idx) */
  double   fval;      /* TYPE_FLOAT */
  int      err_code;  /* TYPE_ERR: error code (0 = ok) */
  char    *err_msg;   /* TYPE_ERR: message (buddy_alloc'd) */
} Value;

/* =========================================================
   DYNARRAY STRUCT
   ========================================================= */

typedef struct {
  char   name[NAME_LEN];
  Value *items;    /* buddy_alloc'd */
  size_t size;
  size_t capacity;
} DynArray;

/* Global array table (for compatibility with base REPL) */
extern DynArray *g_arrays[MAX_ARRAYS];
extern size_t    g_array_count;

/* =========================================================
   VALUE CONSTRUCTORS
   ========================================================= */

NEXS_API Value val_nil(void);
NEXS_API Value val_bool(int b);
NEXS_API Value val_int(int64_t n);
NEXS_API Value val_float(double f);
NEXS_API Value val_str(const char *s);       /* copies s into buddy pool */
NEXS_API Value val_err(int code, const char *msg);
NEXS_API Value val_ref(const char *path);    /* reference to registry path */
NEXS_API Value val_ptr(const char *target);  /* TYPE_PTR: pointer to target path */
NEXS_API Value val_fn_idx(int64_t idx);      /* TYPE_FN with fn_table index */

/* =========================================================
   VALUE PREDICATES
   ========================================================= */

NEXS_API int val_is_truthy(const Value *v);
NEXS_API int val_is_error(const Value *v);
NEXS_API int val_equal(const Value *a, const Value *b);

/* =========================================================
   ARITHMETIC OPERATIONS (return TYPE_ERR on type mismatch)
   ========================================================= */

NEXS_API Value val_add(const Value *a, const Value *b);
NEXS_API Value val_sub(const Value *a, const Value *b);
NEXS_API Value val_mul(const Value *a, const Value *b);
NEXS_API Value val_div(const Value *a, const Value *b);
NEXS_API Value val_mod(const Value *a, const Value *b);
NEXS_API Value val_lt(const Value *a, const Value *b);
NEXS_API Value val_gt(const Value *a, const Value *b);
NEXS_API Value val_le(const Value *a, const Value *b);
NEXS_API Value val_ge(const Value *a, const Value *b);
NEXS_API Value val_eq(const Value *a, const Value *b);
NEXS_API Value val_ne(const Value *a, const Value *b);
NEXS_API Value val_and(const Value *a, const Value *b);
NEXS_API Value val_or(const Value *a, const Value *b);
NEXS_API Value val_not(const Value *a);

/* =========================================================
   CONVERSIONS
   ========================================================= */

NEXS_API int64_t     val_to_int(const Value *v);
NEXS_API double      val_to_float(const Value *v);
NEXS_API const char *val_type_name(ValueType t);

/* =========================================================
   PRINT / MEMORY
   ========================================================= */

NEXS_API void  val_print(const Value *v, FILE *out);
NEXS_API void  val_free(Value *v);
NEXS_API Value val_clone(const Value *v);  /* deep copy — buddy_alloc's new data */

/* =========================================================
   DYNARRAY API
   ========================================================= */

NEXS_API DynArray *arr_get_or_create(const char *name);
NEXS_API DynArray *arr_get(const char *name);
NEXS_API void      arr_ensure_cap(DynArray *arr, size_t index);
NEXS_API void      arr_set(DynArray *arr, size_t index, Value val);
NEXS_API Value     arr_get_at(DynArray *arr, size_t index);
NEXS_API void      arr_delete(DynArray *arr, size_t index);
NEXS_API void      arr_print(DynArray *arr, FILE *out);
NEXS_API void      arr_free(DynArray *arr);

/* =========================================================
   CONVENIENCE MACROS
   ========================================================= */

#define VAL_INT(n)   val_int((int64_t)(n))
#define VAL_FLOAT(f) val_float((double)(f))
#define VAL_STR(s)   val_str(s)
#define VAL_BOOL(b)  val_bool(b)
#define VAL_NIL()    val_nil()
#define VAL_ERR(c,m) val_err((c),(m))
#define VAL_TRUE     val_bool(1)
#define VAL_FALSE    val_bool(0)

#define PROPAGATE_ERR(v)                                                       \
  do {                                                                         \
    if (val_is_error(&(v)))                                                    \
      return (EvalResult){CTRL_ERR, (v)};                                      \
  } while (0)

#define IS_CTRL(er) ((er).sig != CTRL_NONE)

#ifdef __cplusplus
}
#endif

#endif /* NEXS_VALUE_H */
