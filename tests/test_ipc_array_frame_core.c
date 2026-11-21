/* Actual hosted Unix pipe provider + original registry IPC and Value.
 * Verify nested arrays, legitimate TYPE_ERR(99), malformed array bounds,
 * and incomplete nested wire payload. No mocked authority or fake queue.
 * This does not claim NXEC REG framing, replay defense or process identity.
 */
#include "nexs_alloc.h"
#include "nexs_registry.h"
#include "nexs_value.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned checks = 0U;
static unsigned failures = 0U;

static void verify(int condition, const char *description) {
  ++checks;
  if (!condition) {
    ++failures;
    fprintf(stderr, "IPC_ARRAY_FRAME_FAIL %s\n", description);
  }
}
static void put32(unsigned char *output, uint32_t number) {
  for (size_t i = 0U; i < 4U; ++i) {
    output[i] = (unsigned char)(number & 255U);
    number >>= 8U;
  }
}
static void put64(unsigned char *output, uint64_t number) {
  for (size_t i = 0U; i < 8U; ++i) {
    output[i] = (unsigned char)(number & 255U);
    number >>= 8U;
  }
}
static void encode_header(unsigned char buffer[25], ValueType type,
                          int64_t ival, uint32_t str_count,
                          uint32_t array_count) {
  memset(buffer, 0, 25U);
  buffer[0] = (unsigned char)type;
  put64(buffer + 1U, (uint64_t)ival);
  put32(buffer + 17U, str_count);
  put32(buffer + 21U, array_count);
}
static RegIpcQueue *real_pipe_queue(const char *path) {
  RegKey *key = reg_lookup(path);
  if (key == NULL)
    return NULL;
  return key->queue;
}
static void test_real_nested_array(void) {
  RegIpcQueue *queue = real_pipe_queue("/frame");
  verify(queue != NULL && queue->use_pipe == 1 &&
         queue->pipe_fd[0] >= 0 && queue->pipe_fd[1] >= 0,
         "queue promoted through actual hosted pipe transport");
  if (queue == NULL || !queue->use_pipe)
    return;

  DynArray *inner = arr_create_anon();
  DynArray *outer = arr_create_anon();
  verify(inner != NULL && outer != NULL,
         "both real Value array owners admitted");
  if (inner == NULL || outer == NULL) {
    if (inner != NULL) arr_unref(inner);
    if (outer != NULL) arr_unref(outer);
    return;
  }
  verify(arr_set(inner, 0U, val_int(41)) == 0,
         "inner numeric element admitted");
  Value text = val_str("nested");
  verify(text.type == TYPE_STR && arr_set(inner, 1U, text) == 0,
         "inner text element admitted");
  val_free(&text);
  Value child = {0};
  child.type = TYPE_ARR;
  child.data = inner;
  verify(arr_set(outer, 0U, child) == 0,
         "nested array retained by parent owner");
  verify(arr_set(outer, 1U, val_int(84)) == 0,
         "parent scalar element admitted");

  Value source = val_nil();
  source.type = TYPE_ARR;
  source.data = outer;
  verify(reg_ipc_send("/frame", source) == 0,
         "production wire serializer writes nested array");
  verify(reg_ipc_pending("/frame") != 0,
         "real OS pipe reports pending bytes");
  Value received = val_nil();
  verify(reg_ipc_recv("/frame", &received) == 0,
         "production wire decoder returns one full array message");
  verify(received.type == TYPE_ARR && received.data != NULL,
         "decoding published exactly one complete nested outer array");
  if (received.type == TYPE_ARR && received.data != NULL) {
    Value nested = arr_get_at((DynArray *)received.data, 0U);
    Value second = arr_get_at((DynArray *)received.data, 1U);
    verify(nested.type == TYPE_ARR && nested.data != NULL &&
           second.type == TYPE_INT && second.ival == 84,
           "nested pointer and parent scalar preserved");
    if (nested.type == TYPE_ARR && nested.data != NULL) {
      Value first = arr_get_at((DynArray *)nested.data, 0U);
      Value actual_text = arr_get_at((DynArray *)nested.data, 1U);
      verify(first.type == TYPE_INT && first.ival == 41,
             "nested numeric wire value preserved byte-exactly");
      verify(actual_text.type == TYPE_STR &&
             actual_text.data != NULL &&
             strcmp(actual_text.data, "nested") == 0,
             "nested string wire value preserved byte-exactly");
      val_free(&first);
      val_free(&actual_text);
    }
    val_free(&nested);
    val_free(&second);
  }
  val_free(&received);
  verify(reg_ipc_pending("/frame") == 0,
         "consumed pipe array leaves no pending message");
  arr_unref(outer);
  arr_unref(inner);
}

static void test_invalid_array_count(void) {
  RegIpcQueue *queue = real_pipe_queue("/frame");
  if (queue == NULL || !queue->use_pipe)
    return;
  unsigned char bad[25];
  encode_header(bad, TYPE_ARR, 0, 0U, UINT32_MAX);
  verify(write(queue->pipe_fd[1], bad, sizeof(bad)) == (ssize_t)sizeof(bad),
         "actual Unix pipe receives malformed count without buffering fake");
  Value value = val_int(737);
  verify(reg_ipc_recv("/frame", &value) != 0 &&
         value.type == TYPE_INT && value.ival == 737,
         "oversized array frame rejected without mutating output owner");
  val_free(&value);
}

static void test_legitimate_nested_error_99(void) {
  RegIpcQueue *queue = real_pipe_queue("/frame");
  if (queue == NULL || !queue->use_pipe)
    return;
  unsigned char parent[25];
  unsigned char child[25];
  encode_header(parent, TYPE_ARR, 0, 0U, 1U);
  encode_header(child, TYPE_ERR, 99, 7U, 0U);
  verify(write(queue->pipe_fd[1], parent, 25U) == 25,
         "actual provider wrote parent array header");
  verify(write(queue->pipe_fd[1], child, 21U) == 21,
         "actual provider wrote legitimate error child header");
  verify(write(queue->pipe_fd[1], "valid99", 7U) == 7,
         "actual provider wrote complete error child text");
  verify(write(queue->pipe_fd[1], child + 21U, 4U) == 4,
         "actual provider wrote post-text array count as wire requires");
  Value array = val_nil();
  verify(reg_ipc_recv("/frame", &array) == 0 && array.type == TYPE_ARR &&
         array.data != NULL,
         "error code 99 cannot masquerade as malformed nested wire frame");
  if (array.type == TYPE_ARR && array.data != NULL) {
    Value elem = arr_get_at((DynArray *)array.data, 0U);
    verify(elem.type == TYPE_ERR && elem.err_code == 99 &&
           elem.err_msg != NULL &&
           strcmp(elem.err_msg, "valid99") == 0,
           "legitimate typed error retains exact code and diagnostic");
    val_free(&elem);
  }
  val_free(&array);
}
static void test_truncated_nested_frame(void) {
  verify(reg_ipc_init_queue("/truncated", 2) == 0,
         "real second queue admitted before pipe upgrade");
  verify(reg_ipc_enable_pipes() == 0,
         "existing queue upgraded by same production provider");
  RegIpcQueue *queue = real_pipe_queue("/truncated");
  verify(queue != NULL && queue->use_pipe == 1,
         "second queue owns its actual independent Unix pipe");
  if (queue == NULL || !queue->use_pipe)
    return;
  unsigned char parent[25];
  encode_header(parent, TYPE_ARR, 0, 0U, 1U);
  verify(write(queue->pipe_fd[1], parent, 25U) == 25,
         "partially transmitted nested frame has valid parent header");
  close(queue->pipe_fd[1]);
  queue->pipe_fd[1] = -1;
  Value array = val_int(818);
  verify(reg_ipc_recv("/truncated", &array) != 0 &&
         array.type == TYPE_INT && array.ival == 818,
         "EOF during nested element never publishes incomplete array");
  val_free(&array);
}

int main(void) {
  verify(reg_init() == 0,
         "canonical production registry bootstraps under real Buddy");
  verify(reg_ipc_init_queue("/frame", 8) == 0,
         "canonical production queue initialized once");
  verify(reg_ipc_enable_pipes() == 0,
         "queue converted with actual hosted pipe descriptors");
  test_real_nested_array();
  test_invalid_array_count();
  test_legitimate_nested_error_99();
  test_truncated_nested_frame();
  printf("IPC_ARRAY_WIRE_CORE %s checks=%u failures=%u\n",
         failures == 0U ? "PASS":"FAIL", checks, failures);
  return failures == 0U ? 0 : 1;
}
