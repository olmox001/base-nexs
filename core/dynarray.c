/*
 * core/dynarray.c — DynArray Implementation
 * ===========================================
 * Dynamically-typed arrays backed by the buddy allocator.
 */

#include "include/nexs_value.h"
#include "include/nexs_alloc.h"
#include "include/nexs_checked.h"
#include "include/nexs_common.h"

#include <string.h>

/* Arrays have one owner: the registry's typed Value or the anonymous Value.
 * Names live in the canonical namespace, not in a duplicate global list.
 * An array reference cannot outlive its registry key by hidden retention. */

DynArray *arr_create_anon(void) {
  DynArray *arr = buddy_alloc(sizeof(DynArray));
  if (arr == NULL)
    return NULL;
  memset(arr, 0, sizeof(*arr));
  const size_t initial_capacity = 4U;
  size_t initial_bytes = 0U;
  if (nx_size_mul(initial_capacity, sizeof(Value), &initial_bytes) !=
      NX_STATUS_OK) {
    buddy_free(arr);
    return NULL;
  }
  Value *items = buddy_alloc(initial_bytes);
  if (items == NULL) {
    buddy_free(arr);
    return NULL;
  }
  memset(items, 0, initial_bytes);
  arr->capacity = initial_capacity;
  arr->refcount = 1;
  arr->items = items;
  return arr;
}

void arr_ref(DynArray *arr) {
  if (!arr) return;
  arr->refcount++;
}

void arr_unref(DynArray *arr) {
  if (!arr) return;
  arr->refcount--;
  if (arr->refcount <= 0) {
    arr_free(arr);
  }
}

/* A failure leaves the original Value pointer/capacity/contents intact.
 * Capacity is constrained only by checked arithmetic and the actual
 * allocator provider, never by a hardcoded maximum number of arrays. */
int arr_ensure_cap(DynArray *arr, size_t index) {
  if (arr == NULL || index == SIZE_MAX)
    return -1;
  if (index < arr->capacity)
    return 0;
  size_t required = 0U;
  size_t required_bytes = 0U;
  if (nx_size_add(index, 1U, &required) != NX_STATUS_OK ||
      nx_size_mul(required, sizeof(Value), &required_bytes) != NX_STATUS_OK)
    return -1;
  size_t next = arr->capacity ? arr->capacity : 4U;
  while (next < required) {
    if (next > (SIZE_MAX / 2U))
      return -1;
    next *= 2U;
  }
  size_t bytes = 0U;
  size_t existing_bytes = 0U;
  if (nx_size_mul(next, sizeof(Value), &bytes) != NX_STATUS_OK ||
      nx_size_mul(arr->capacity, sizeof(Value), &existing_bytes) !=
      NX_STATUS_OK)
    return -1;
  Value *fresh = buddy_alloc(bytes);
  if (fresh == NULL)
    return -1;
  memset(fresh, 0, bytes);
  if (arr->items != NULL) {
    memcpy(fresh, arr->items, existing_bytes);
    buddy_free(arr->items);
  }
  arr->items = fresh;
  arr->capacity = next;
  return 0;
}

/* Preserve the caller's ownership and avoid freeing the old slot before
 * cloning its replacement (self-assignment otherwise reads freed storage). */
int arr_set(DynArray *arr, size_t index, Value val) {
  if (arr == NULL)
    return -1;
  /* Clone BEFORE any capacity or slot mutation. The fallible Value ABI
   * must never overwrite a real item with an OOM error object. */
  Value replacement = val_nil();
  if (val_clone_checked(&val, &replacement) != 0)
    return -1;
  if (arr_ensure_cap(arr, index) != 0) {
    val_free(&replacement);
    return -1;
  }
  val_free(&arr->items[index]);
  arr->items[index] = replacement;
  if (index >= arr->size)
    arr->size = index + 1U;
  return 0;
}

/* Clone the replacement into the array, then release caller-owned data. */
int arr_set_owned(DynArray *arr, size_t index, Value owned) {
  int result = arr_set(arr, index, owned);
  val_free(&owned);
  return result;
}

/* Caller already obtained capacity using arr_ensure_cap, no ghost slot. */
int arr_insert_owned_admitted(DynArray *arr, size_t index, Value owned) {
  if (arr == NULL || arr->items == NULL || index > arr->size ||
      arr->size >= arr->capacity)
    return -1;
  for (size_t j = arr->size; j > index; --j)
    arr->items[j] = arr->items[j - 1U];
  arr->items[index] = owned;
  ++arr->size;
  return 0;
}

/* Release a single owned Value, move survivors, clear the old tail. */
int arr_remove_compact(DynArray *arr, size_t index) {
  if (arr == NULL || arr->items == NULL || index >= arr->size)
    return -1;
  val_free(&arr->items[index]);
  for (size_t j = index; j + 1U < arr->size; ++j)
    arr->items[j] = arr->items[j + 1U];
  --arr->size;
  arr->items[arr->size] = val_nil();
  return 0;
}

Value arr_get_at(DynArray *arr, size_t index) {
  if (!arr) return val_err(2, "NULL array");
  if (index >= arr->size) return val_err(2, "index out of range");
  if (arr->items[index].type == TYPE_NIL && !arr->items[index].data)
    return val_nil();
  return val_clone(&arr->items[index]);
}

void arr_delete(DynArray *arr, size_t index) {
  if (!arr || index >= arr->size) return;
  val_free(&arr->items[index]);
  arr->items[index] = val_nil();
}

void arr_print(DynArray *arr, FILE *out) {
  if (!arr || !out) return;
  fprintf(out, "\n=== Array '%s' [size=%zu cap=%zu] ===\n",
          arr->name, arr->size, arr->capacity);
  size_t active = 0;
  for (size_t i = 0; i < arr->size; i++) {
    fprintf(out, "  [%zu] = ", i);
    val_print(&arr->items[i], out);
    fprintf(out, "\n");
    if (arr->items[i].type != TYPE_NIL)
      active++;
  }
  fprintf(out, "  (%zu active elements)\n\n", active);
}

void arr_free(DynArray *arr) {
  if (!arr) return;
  /* If this was called directly, we check refcount just in case, 
   * but usually arr_unref is the entry point. */
  for (size_t i = 0; i < arr->size; i++)
    val_free(&arr->items[i]);
  if (arr->items)
    xfree(arr->items);
  xfree(arr);
}
