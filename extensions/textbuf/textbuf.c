/*
 * NEXS native text-buffer extension, separate from the language core.
 * Shared DynArray owns all Values; all nine public functions resolve
 * through the ONE existing /sys callable registry. No second registry.
 * NXOLMO authority/generations and cross-process ABI remain pending.
 */
#include "../../core/include/nexs_alloc.h"
#include "../../core/include/nexs_checked.h"
#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"
#include "../../lang/include/nexs_fn.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* =========================================================
   TEXT BUFFER BUILT-INS
   ========================================================= */

/* Every line must be a successfully constructed string. A fallible
 * val_str error is never silently published as an array element. */
static int textbuf_set_string(DynArray *buffer, size_t index,
                               const char *content) {
  Value text = val_str(content);
  if (text.type != TYPE_STR) {
    val_free(&text);
    return -1;
  }
  return arr_set_owned(buffer, index, text);
}

static Value builtin_tb_create(Value *args, int n) {
  (void)args;
  (void)n;
  DynArray *arr = arr_create_anon();
  if (arr == NULL)
    return val_err(2, "tb_create: array allocation failed");
  if (textbuf_set_string(arr, 0U, "") != 0) {
    arr_unref(arr);
    return val_err(2, "tb_create: capacity admission failed");
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

static Value builtin_tb_load(Value *args, int n) {
  if (n < 2 || args[0].type != TYPE_ARR || !args[0].data ||
      args[1].type != TYPE_STR || !args[1].data)
    return val_err(4, "tb_load: buffer array and content string required");

  DynArray *buf = (DynArray *)args[0].data;
  const char *content = (const char *)args[1].data;
  /* Prepare a new buffer completely before replacing the existing one.
   * Admission failure cannot expose a partially overwritten text buffer. */
  DynArray *staged = arr_create_anon();
  if (staged == NULL)
    return val_err(2, "tb_load: temporary array allocation failed");
  size_t length = strlen(content);
  if (length == SIZE_MAX) {
    arr_unref(staged);
    return val_err(2, "tb_load: input size overflow");
  }
  char *copy = buddy_alloc(length + 1U);
  if (copy == NULL) {
    arr_unref(staged);
    return val_err(2, "tb_load: input copy allocation failed");
  }
  memcpy(copy, content, length + 1U);
  char *cursor = copy;
  char *line = copy;
  size_t idx = 0;
  int result = 0;
  while (*cursor && result == 0) {
    if (*cursor == '\n') {
      *cursor = '\0';
      if (textbuf_set_string(staged, idx++, line) != 0)
        result = -1;
      line = cursor + 1;
    }
    cursor++;
  }
  if (result == 0 && (*line || cursor > line)) {
    if (textbuf_set_string(staged, idx++, line) != 0)
      result = -1;
  }
  if (result == 0 && idx == 0) {
    if (textbuf_set_string(staged, 0U, "") != 0)
      result = -1;
  }
  xfree(copy);
  if (result != 0) {
    arr_unref(staged);
    return val_err(2, "tb_load: capacity admission failed");
  }

  Value *old_items = buf->items;
  size_t old_size = buf->size;
  size_t old_capacity = buf->capacity;
  buf->items = staged->items;
  buf->size = staged->size;
  buf->capacity = staged->capacity;
  staged->items = old_items;
  staged->size = old_size;
  staged->capacity = old_capacity;
  arr_unref(staged);
  return val_int((int64_t)idx);
}

static Value builtin_tb_to_str(Value *args, int n) {
  if (n < 1 || args[0].type != TYPE_ARR || args[0].data == NULL)
    return val_err(4, "tb_to_str: buffer required");
  const DynArray *buf = (const DynArray *)args[0].data;

  /* Two-phase construction: admission first, no mutation of source arrays.
   * Keep the existing representation: only string-valued rows are emitted;
   * each emitted row except the last array position adds a newline. */
  size_t total = 1U;
  for (size_t i = 0U; i < buf->size; ++i) {
    const Value *line = &buf->items[i];
    if (line->type == TYPE_STR && line->data != NULL) {
      size_t row_plus_separator = 0U;
      if (nx_size_add(strlen((const char *)line->data), 1U,
                      &row_plus_separator) != NX_STATUS_OK ||
          nx_size_add(total, row_plus_separator, &total) != NX_STATUS_OK)
        return val_err(2, "tb_to_str: length overflow");
    }
  }
  char *output = buddy_alloc(total);
  if (output == NULL)
    return val_err(2, "tb_to_str: output admission failed");
  size_t used = 0U;
  for (size_t i = 0U; i < buf->size; ++i) {
    const Value *line = &buf->items[i];
    if (line->type == TYPE_STR && line->data != NULL) {
      const char *bytes = (const char *)line->data;
      size_t length = strlen(bytes);
      memcpy(output + used, bytes, length);
      used += length;
      if (i + 1U < buf->size)
        output[used++] = '\n';
    }
  }
  output[used] = '\0';
  Value result = val_nil();
  result.type = TYPE_STR;
  result.data = output;
  return result;
}

/* Textbuffer now has one implementation in the /sys callable registry.
 * The former scripted duplicates were removed, not shadowed. */
static Value builtin_tb_get_line(Value *args, int n) {
  if (n < 2 || args[0].type != TYPE_ARR || args[0].data == NULL)
    return val_err(4, "tb_get_line: array and line index required");
  int64_t requested = val_to_int(&args[1]);
  DynArray *arr = (DynArray *)args[0].data;
  if (requested < 0 || (uint64_t)requested >= arr->size)
    return val_str("");
  Value *line = &arr->items[(size_t)requested];
  if (line->type == TYPE_ERR)
    return val_str("");
  if (line->type == TYPE_STR)
    return val_clone(line);
  char printable[MAX_STR_LEN];
  val_to_str(line, printable, sizeof(printable));
  return val_str(printable);
}

static Value builtin_tb_set_line(Value *args, int n) {
  if (n < 3 || args[0].type != TYPE_ARR || args[0].data == NULL)
    return val_err(4, "tb_set_line: array, line and value required");
  int64_t requested = val_to_int(&args[1]);
  if (requested < 0)
    return val_err(4, "tb_set_line: negative line index");
  DynArray *arr = (DynArray *)args[0].data;
  if (arr_set(arr, (size_t)requested, args[2]) != 0)
    return val_err(2, "tb_set_line: capacity admission failed");
  return val_nil();
}

/* Split one line in place. Reserve capacity and allocate both strings
 * before committing either change; the old line stays intact on failure. */
static Value builtin_tb_split_line(Value *args, int n) {
  if (n < 3 || args[0].type != TYPE_ARR || args[0].data == NULL)
    return val_err(4, "tb_split_line: array, line and column required");
  DynArray *arr = (DynArray *)args[0].data;
  int64_t requested_line = val_to_int(&args[1]);
  int64_t requested_col = val_to_int(&args[2]);
  if (requested_line < 0 || (uint64_t)requested_line >= arr->size)
    return val_err(4, "tb_split_line: index out of range");
  size_t y = (size_t)requested_line;
  Value *line_value = &arr->items[y];
  if (line_value->type != TYPE_STR || line_value->data == NULL)
    return val_err(4, "tb_split_line: line is not text");
  const char *original = (const char *)line_value->data;
  size_t length = strlen(original);
  size_t split = requested_col <= 0 ? 0U :
                 (uint64_t)requested_col >= length ? length :
                 (size_t)requested_col;
  size_t left_bytes = 0U;
  size_t right_bytes = 0U;
  if (nx_size_add(split, 1U, &left_bytes) != NX_STATUS_OK ||
      nx_size_add(length - split, 1U, &right_bytes) != NX_STATUS_OK)
    return val_err(2, "tb_split_line: size overflow");
  char *left = buddy_alloc(left_bytes);
  char *right = buddy_alloc(right_bytes);
  if (left == NULL || right == NULL) {
    if (left != NULL) buddy_free(left);
    if (right != NULL) buddy_free(right);
    return val_err(2, "tb_split_line: string admission failed");
  }
  memcpy(left, original, split);
  left[split] = '\0';
  memcpy(right, original + split, right_bytes);
  if (arr->size == SIZE_MAX || arr_ensure_cap(arr, arr->size) != 0) {
    buddy_free(left);
    buddy_free(right);
    return val_err(2, "tb_split_line: array admission failed");
  }
  Value first = val_nil(), second = val_nil();
  first.type = TYPE_STR; first.data = left;
  second.type = TYPE_STR; second.data = right;
  val_free(&arr->items[y]);
  arr->items[y] = first;
  (void)arr_insert_owned_admitted(arr, y + 1U, second);
  return val_nil();
}

/* Merge y into y-1; return the previous line length to restore cursor.
 * Both input values are read before either old Value is released. */
static Value builtin_tb_merge_line(Value *args, int n) {
  if (n < 2 || args[0].type != TYPE_ARR || args[0].data == NULL)
    return val_err(4, "tb_merge_line: array and line index required");
  int64_t requested = val_to_int(&args[1]);
  if (requested <= 0)
    return val_int(0);
  DynArray *arr = (DynArray *)args[0].data;
  if ((uint64_t)requested >= arr->size)
    return val_err(4, "tb_merge_line: index out of range");
  size_t y = (size_t)requested;
  Value *previous = &arr->items[y - 1U], *current = &arr->items[y];
  if (previous->type != TYPE_STR || current->type != TYPE_STR ||
      previous->data == NULL || current->data == NULL)
    return val_err(4, "tb_merge_line: both lines must be text");
  const char *left = (const char *)previous->data;
  const char *right = (const char *)current->data;
  size_t len_left = strlen(left), len_right = strlen(right);
  size_t combined = 0U, bytes = 0U;
  if (nx_size_add(len_left, len_right, &combined) != NX_STATUS_OK ||
      nx_size_add(combined, 1U, &bytes) != NX_STATUS_OK ||
      len_left > INT64_MAX)
    return val_err(2, "tb_merge_line: size overflow");
  char *merged = buddy_alloc(bytes);
  if (merged == NULL)
    return val_err(2, "tb_merge_line: string admission failed");
  memcpy(merged, left, len_left);
  memcpy(merged + len_left, right, len_right + 1U);
  val_free(previous);
  previous->type = TYPE_STR;
  previous->data = merged;
  (void)arr_remove_compact(arr, y);
  return val_int((int64_t)len_left);
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

  size_t ins_len = strlen(ch);
  if (ins_len > SIZE_MAX - olen - 1U) {
    val_free(&line_val);
    return val_err(4, "tb_insert_char size overflow");
  }
  size_t reservation_bytes = olen + ins_len + 1U;
  char *new_str = nexs_alloc(reservation_bytes);
  if (new_str == NULL) {
    val_free(&line_val);
    return val_err(2, "tb_insert_char out of memory");
  }
  memcpy(new_str, old, (size_t)x);
  memcpy(new_str + x, ch, ins_len);
  memcpy(new_str + x + ins_len, old + x, olen - (size_t)x + 1U);

  int stored = textbuf_set_string(buf, (size_t)y, new_str);
  nexs_free(new_str, reservation_bytes);
  val_free(&line_val);
  if (stored != 0)
    return val_err(2, "tb_insert_char: capacity admission failed");
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
  if (x <= 0) {
    val_free(&line_val);
    return val_int(0);
  }

  size_t reservation_bytes = olen + 1U;
  char *new_str = nexs_alloc(reservation_bytes);
  if (new_str == NULL) {
    val_free(&line_val);
    return val_err(2, "tb_delete_char out of memory");
  }
  memcpy(new_str, old, (size_t)x - 1U);
  memcpy(new_str + x - 1, old + x, olen - (size_t)x + 1U);

  int stored = textbuf_set_string(buf, (size_t)y, new_str);
  nexs_free(new_str, reservation_bytes);
  val_free(&line_val);
  if (stored != 0)
    return val_err(2, "tb_delete_char: capacity admission failed");
  return val_int(1);
}

#define SIG(s) s " \xe2\x86\x92 "

void nexs_textbuf_register(void) {
  /* Text Buffer library */
  (void)fn_register_builtin_sig("tb_create", builtin_tb_create,
                       SIG("tb_create()") "arr");
  (void)fn_register_builtin_sig("tb_load", builtin_tb_load,
                       SIG("tb_load(buf arr, content str)") "int");
  (void)fn_register_builtin_sig("tb_to_str", builtin_tb_to_str,
                       SIG("tb_to_str(buf arr)") "str");
  (void)fn_register_builtin_sig("tb_get_line", builtin_tb_get_line,
                       SIG("tb_get_line(buf arr, y int)") "str");
  (void)fn_register_builtin_sig("tb_set_line", builtin_tb_set_line,
                       SIG("tb_set_line(buf arr, y int, value)") "nil");
  (void)fn_register_builtin_sig("tb_split_line", builtin_tb_split_line,
                       SIG("tb_split_line(buf arr, y int, x int)") "nil");
  (void)fn_register_builtin_sig("tb_merge_line", builtin_tb_merge_line,
                       SIG("tb_merge_line(buf arr, y int)") "int");
  (void)fn_register_builtin_sig(
      "tb_insert_char", builtin_tb_insert_char,
      SIG("tb_insert_char(buf arr, y int, x int, ch str)") "nil");
  (void)fn_register_builtin_sig("tb_delete_char", builtin_tb_delete_char,
                       SIG("tb_delete_char(buf arr, y int, x int)") "int");
}

#undef SIG
