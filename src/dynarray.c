/*
 * dynarray.c — DynArray Implementation
 * ======================================
 * Array dinamici di Value con crescita esponenziale.
 */

#include "basereg.h"

#include <string.h>

/* =========================================================
   DATI GLOBALI
   ========================================================= */

DynArray *g_arrays[MAX_ARRAYS];
size_t g_array_count = 0;

/* =========================================================
   IMPLEMENTAZIONE
   ========================================================= */

DynArray *arr_get(const char *name) {
  if (!name)
    return NULL;
  for (size_t i = 0; i < g_array_count; i++)
    if (strcmp(g_arrays[i]->name, name) == 0)
      return g_arrays[i];
  return NULL;
}

DynArray *arr_get_or_create(const char *name) {
  if (!name)
    return NULL;
  DynArray *a = arr_get(name);
  if (a)
    return a;
  if (g_array_count >= MAX_ARRAYS)
    die("Limite array raggiunto");
  a = xmalloc(sizeof(DynArray));
  strncpy(a->name, name, NAME_LEN - 1);
  a->name[NAME_LEN - 1] = '\0';
  a->capacity = 4;
  a->size = 0;
  a->items = xmalloc(a->capacity * sizeof(Value));
  g_arrays[g_array_count++] = a;
  return a;
}

void arr_ensure_cap(DynArray *arr, size_t index) {
  if (!arr)
    return;
  if (index < arr->capacity)
    return;
  size_t nc = arr->capacity;
  while (nc <= index)
    nc <<= 1;
  Value *ni = xmalloc(nc * sizeof(Value));
  if (arr->items) {
    memcpy(ni, arr->items, arr->capacity * sizeof(Value));
    /* Zero-fill new portion for safety */
    memset(ni + arr->capacity, 0, (nc - arr->capacity) * sizeof(Value));
    xfree(arr->items);
  }
  arr->items = ni;
  arr->capacity = nc;
}

void arr_set(DynArray *arr, size_t index, Value val) {
  if (!arr)
    return;
  arr_ensure_cap(arr, index);
  val_free(&arr->items[index]);
  arr->items[index] = val_clone(&val);
  if (index >= arr->size)
    arr->size = index + 1;
}

Value arr_get_at(DynArray *arr, size_t index) {
  if (!arr)
    return val_err(2, "array NULL");
  if (index >= arr->size)
    return val_err(2, "indice fuori range");
  if (arr->items[index].type == TYPE_NIL && !arr->items[index].data)
    return val_nil();
  return val_clone(&arr->items[index]);
}

void arr_delete(DynArray *arr, size_t index) {
  if (!arr || index >= arr->size)
    return;
  val_free(&arr->items[index]);
  arr->items[index] = val_nil();
}

void arr_print(DynArray *arr, FILE *out) {
  if (!arr || !out)
    return;
  fprintf(out, "\n=== Array '%s' [size=%zu cap=%zu] ===\n", arr->name,
          arr->size, arr->capacity);
  size_t active = 0;
  for (size_t i = 0; i < arr->size; i++) {
    fprintf(out, "  [%zu] = ", i);
    val_print(&arr->items[i], out);
    fprintf(out, "\n");
    if (arr->items[i].type != TYPE_NIL)
      active++;
  }
  fprintf(out, "  (%zu elementi attivi)\n\n", active);
}

void arr_free(DynArray *arr) {
  if (!arr)
    return;
  for (size_t i = 0; i < arr->size; i++)
    val_free(&arr->items[i]);
  if (arr->items)
    xfree(arr->items);
  xfree(arr);
}
