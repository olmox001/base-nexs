/* Execute actual array capacity and ownership contracts in C.
 * No fake mocks, no assert(), no hardcoded success-only test paths. */
#include "nexs_value.h"
#include "nexs_alloc.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int verify(int condition, const char *label) {
  if (!condition) {
    fprintf(stderr, "ARRAY_CAPACITY_FAIL: %s\n", label);
    return 0;
  }
  return 1;
}

static int read_int_at(DynArray *arr, size_t index, int64_t expected) {
  Value v = arr_get_at(arr, index);
  int ok = v.type == TYPE_INT && v.ival == expected;
  val_free(&v);
  return ok;
}

static int test_admission_and_growth(void) {
  DynArray *arr = arr_create_anon();
  if (!verify(arr != NULL, "initial allocation"))
    return 0;
  int ok = verify(arr->size == 0 && arr->capacity >= 1,
                  "new array initially empty");
  ok &= verify(arr_set(arr, 0U, val_int(41)) == 0, "first value");
  ok &= verify(arr_set(arr, 12U, val_int(73)) == 0, "growth beyond initial");
  ok &= verify(read_int_at(arr, 0U, 41), "old contents preserved on growth");
  ok &= verify(read_int_at(arr, 12U, 73), "new value after growth");
  size_t size = arr->size;
  size_t capacity = arr->capacity;
  Value *items = arr->items;
  ok &= verify(arr_set(arr, SIZE_MAX, val_int(1)) != 0,
               "max index fails");
  ok &= verify(arr_ensure_cap(arr, SIZE_MAX) != 0,
               "capacity max index fails");
  ok &= verify(arr_set(arr, SIZE_MAX / sizeof(Value), val_int(1)) != 0,
               "capacity multiplication overflows");
  ok &= verify(arr_set(arr, POOL_SIZE / sizeof(Value), val_int(1)) != 0,
               "provider rejects impossible allocation");
  ok &= verify(arr->size == size && arr->capacity == capacity &&
               arr->items == items, "failed admission leaves state unchanged");
  ok &= verify(read_int_at(arr, 0U, 41) && read_int_at(arr, 12U, 73),
               "failed admission never overwrites old values");
  arr_unref(arr);
  return ok;
}

static int test_self_replacement_and_refs(void) {
  DynArray *arr = arr_create_anon();
  if (!verify(arr != NULL, "self-assignment allocation"))
    return 0;
  Value text = val_str("SELF_ALIAS_TEST");
  int ok = verify(arr_set(arr, 0U, text) == 0, "store string");
  val_free(&text);
  Value borrowed = arr->items[0];
  ok &= verify(arr_set(arr, 0U, borrowed) == 0,
               "self assignment must clone before freeing old");
  ok &= verify(arr->items[0].type == TYPE_STR &&
               strcmp((const char *)arr->items[0].data,
                      "SELF_ALIAS_TEST") == 0, "self replacement retained bytes");
  arr_ref(arr);
  arr_unref(arr);
  ok &= verify(arr->refcount == 1 &&
               arr->items[0].type == TYPE_STR,
               "release of borrowed reference does not destroy array");
  arr_unref(arr);
  return ok;
}

static int test_reuse_and_reclamation(void) {
  int ok = 1;
  for (size_t i = 0; i < 4000U && ok; ++i) {
    DynArray *arr = arr_create_anon();
    if (arr == NULL) {
      ok = verify(0, "resource reuse after repeated free");
    } else {
      ok &= verify(arr_set(arr, i % 15U, val_int((int64_t)i)) == 0,
                   "repeated growth and allocation");
      ok &= verify(read_int_at(arr, i % 15U, (int64_t)i),
                   "value after repeated growth");
      arr_unref(arr);
    }
  }
  return ok;
}

int main(void) {
  int ok = test_admission_and_growth();
  ok &= test_self_replacement_and_refs();
  ok &= test_reuse_and_reclamation();
  if (ok)
    puts("ARRAY_CAPACITY_CORE_PASS");
  return ok ? 0 : 1;
}
