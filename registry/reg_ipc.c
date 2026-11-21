/*
 * registry/reg_ipc.c — Registry IPC Message Queue Implementation
 * ================================================================
 * Non-blocking message passing through registry keys.
 * Each key can have an associated RegIpcQueue.
 *
 * Pipe-backed IPC (Bug 3 fix):
 *   In hosted (macOS/Linux) mode, rfork(RFPROC) calls fork() which creates
 *   independent address spaces.  The in-process queue (heap-backed) is then
 *   split across two private copies, so sendmessage in the child is never
 *   seen by receivemessage in the parent (and vice-versa).
 *
 *   BEFORE fork(), reg_ipc_enable_pipes() prepares the inherited
 *   POSIX pipe endpoints in the original registry. Both processes then
 *   inherit the SAME actual file descriptors. Failed provider admission
 *   returns -1 to the caller and prevents fork. This is a hosted bridge
 *   precondition, NOT NXEC REG transport certification.
 *
 * Serialisation format (all types, no external deps):
 *   [uint8  type]
 *   [int64  ival]       (8 bytes, little-endian)
 *   [double fval]       (8 bytes)
 *   [int32  str_len]    (4 bytes; 0 if no string data)
 *   [char[] str_data]   (str_len bytes, not NUL-terminated in wire format)
 *   [int32  arr_len]    (4 bytes; 0 if not TYPE_ARR)
 *   [N × recursive msg] (arr_len items)
 *
 *   err_code and err_msg are serialised using the str_len/str_data fields
 *   (ival = err_code, str = err_msg).
 *
 *   TYPE_PTR / TYPE_REF: target path stored as str_data.
 *   TYPE_FN:             REJECT: local callable addresses have no wire identity.
 */

#include "include/nexs_registry.h"
#include "include/nexs_ipc_transport.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_value.h"

#include <errno.h>

#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* =========================================================
   SERIALISATION HELPERS (pipe wire format)
   ========================================================= */

/* Write exactly n bytes to fd, retrying on EINTR */
static int pipe_write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = (const uint8_t *)buf;
  while (n > 0) {
    ssize_t wr = write(fd, (const char *)p, n);
    if (wr < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (wr == 0) return -1; /* No forward progress. */
    p += (size_t)wr;
    n -= (size_t)wr;
  }
  return 0;
}

/* Read exactly n bytes from fd, retrying on EINTR.
 * Returns -1 on error or EOF. */
static int pipe_read_all(int fd, void *buf, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n > 0) {
    ssize_t rd = read(fd, (char *)p, n);
    if (rd < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (rd == 0) return -1; /* EOF */
    p += rd;
    n -= (size_t)rd;
  }
  return 0;
}

/* LE helpers (avoids alignment issues) */
static void le_write64(uint8_t *dst, uint64_t v) {
  for (int i = 0; i < 8; i++) { dst[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
static uint64_t le_read64(const uint8_t *src) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) { v <<= 8; v |= src[i]; }
  return v;
}
static void le_write32(uint8_t *dst, uint32_t v) {
  for (int i = 0; i < 4; i++) { dst[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
static uint32_t le_read32(const uint8_t *src) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; i--) { v <<= 8; v |= src[i]; }
  return v;
}

/*
 * pipe_serialize_value — write a Value to the pipe write-end.
 * All types supported; TYPE_ARR recurses for each element.
 */
static int pipe_serialize_value(int wfd, const Value *v) {
  if (v == NULL || v->type == TYPE_FN)
    return -1;
  uint8_t type_byte = (uint8_t)v->type;
  if (pipe_write_all(wfd, &type_byte, 1) < 0) return -1;

  /* ival (8 bytes) */
  uint8_t ibuf[8];
  le_write64(ibuf, (uint64_t)v->ival);
  if (pipe_write_all(wfd, ibuf, 8) < 0) return -1;

  /* fval (8 bytes) */
  uint8_t fbuf[8];
  double fv = v->fval;
  memcpy(fbuf, &fv, 8);
  if (pipe_write_all(wfd, fbuf, 8) < 0) return -1;

  /* str_len + str_data */
  uint32_t str_len = 0;
  const char *str_data = NULL;

  switch (v->type) {
  case TYPE_STR:
  case TYPE_REF:
  case TYPE_PTR:
    str_data = v->data ? (const char *)v->data : "";
    if (strlen(str_data) > UINT32_MAX)
      return -1;
    str_len = (uint32_t)strlen(str_data);
    break;
  case TYPE_ERR:
    str_data = v->err_msg ? v->err_msg : "";
    if (strlen(str_data) > UINT32_MAX)
      return -1;
    str_len = (uint32_t)strlen(str_data);
    break;
  default:
    break;
  }

  uint8_t slbuf[4];
  le_write32(slbuf, str_len);
  if (pipe_write_all(wfd, slbuf, 4) < 0) return -1;
  if (str_len > 0 && pipe_write_all(wfd, str_data, str_len) < 0) return -1;

  /* arr_len + elements (TYPE_ARR only) */
  uint32_t arr_len = 0;
  if (v->type == TYPE_ARR && v->data) {
    DynArray *arr = (DynArray *)v->data;
    if (arr->size > UINT32_MAX || arr->size > POOL_SIZE / MIN_BLOCK ||
        (arr->size > 0U && (arr->items == NULL ||
                           arr->size > arr->capacity)))
      return -1;
    arr_len = (uint32_t)arr->size;
  }
  uint8_t albuf[4];
  le_write32(albuf, arr_len);
  if (pipe_write_all(wfd, albuf, 4) < 0) return -1;

  if (arr_len > 0 && v->data) {
    DynArray *arr = (DynArray *)v->data;
    for (uint32_t i = 0; i < arr_len; i++) {
      Value elem = val_nil();
      /* A failed clone of a legitimate TYPE_ERR is NOT a wire element. */
      if (val_clone_checked(&arr->items[i], &elem) != 0)
        return -1;
      int sent = pipe_serialize_value(wfd, &elem);
      val_free(&elem);
      if (sent != 0)
        return -1;
    }
  }

  return 0;
}

/*
 * pipe_deserialize_value — read a Value from the pipe read-end.
 * On error returns a TYPE_ERR value; caller must val_free() the result.
 */
/* The valid bit is independent from the Value TYPE_ERR payload:
 * error code 99 can be a legitimate user value, NOT a framing failure. */
static Value pipe_deserialize_value_inner(int rfd, int *valid) {
  if (valid != NULL)
    *valid = 0;
  uint8_t type_byte;
  if (pipe_read_all(rfd, &type_byte, 1) < 0)
    return val_err(99, "pipe_recv: read type failed");

  uint8_t ibuf[8];
  if (pipe_read_all(rfd, ibuf, 8) < 0)
    return val_err(99, "pipe_recv: read ival failed");
  int64_t ival = (int64_t)le_read64(ibuf);

  uint8_t fbuf[8];
  if (pipe_read_all(rfd, fbuf, 8) < 0)
    return val_err(99, "pipe_recv: read fval failed");
  double fval = 0.0;
  memcpy(&fval, fbuf, 8);

  uint8_t slbuf[4];
  if (pipe_read_all(rfd, slbuf, 4) < 0)
    return val_err(99, "pipe_recv: read str_len failed");
  uint32_t str_len = le_read32(slbuf);

  char *str_buf = NULL;
  size_t source_bytes = (size_t)str_len;
  if (source_bytes == SIZE_MAX)
    return val_err(99, "pipe_recv: string length overflows provider");
  size_t reservation_bytes = source_bytes + 1U;
  if (str_len > 0U) {
    str_buf = nexs_alloc(reservation_bytes);
    if (str_buf == NULL)
      return val_err(12, "pipe_recv: source allocation failed");
    if (pipe_read_all(rfd, str_buf, source_bytes) < 0) {
      nexs_free(str_buf, reservation_bytes);
      return val_err(99, "pipe_recv: read str_data failed");
    }
    str_buf[source_bytes] = '\0';
  }

  uint8_t albuf[4];
  if (pipe_read_all(rfd, albuf, 4) < 0) {
    if (str_buf != NULL) nexs_free(str_buf, reservation_bytes);
    return val_err(99, "pipe_recv: read arr_len failed");
  }
  uint32_t arr_len = le_read32(albuf);

  /* Reconstruct the Value. Valid means complete wire decode, not merely
   * that result happens to have a non-error ValueType. */
  Value result;
  int decoded = 1;
  switch ((ValueType)type_byte) {
  case TYPE_NIL:
    result = val_nil();
    break;
  case TYPE_INT:
    result = val_int(ival);
    break;
  case TYPE_FLOAT:
    result = val_float(fval);
    break;
  case TYPE_BOOL:
    result = val_bool((int)ival);
    break;
  case TYPE_FN:
    result = val_err(99, "pipe_recv: function object requires capability transfer");
    decoded = 0;
    break;
  case TYPE_STR:
    result = val_str(str_buf ? str_buf : "");
    decoded = result.type == TYPE_STR;
    break;
  case TYPE_REF:
    result = val_ref(str_buf ? str_buf : "/");
    decoded = result.type == TYPE_REF;
    break;
  case TYPE_PTR:
    result = val_ptr(str_buf ? str_buf : "/");
    decoded = result.type == TYPE_PTR;
    break;
  case TYPE_ERR: {
    int code = (int)ival;
    result = val_err(code, str_buf ? str_buf : "");
    decoded = result.err_msg != NULL;
    break;
  }
  case TYPE_ARR: {
    /* This legacy nonframed pipe must not admit unbounded allocation
     * demands or publish a partially decoded array. A future NXEC REG
     * envelope needs separate byte/operation budgets and typed errors. */
    if (str_len != 0U || arr_len > POOL_SIZE / MIN_BLOCK) {
      result = val_err(99, "pipe_recv: array frame exceeds provider budget");
      decoded = 0;
      break;
    }
    DynArray *arr = arr_create_anon();
    if (arr == NULL) {
      result = val_err(12, "pipe_recv: array admission failed");
      decoded = 0;
      break;
    }
    int complete = 1;
    for (uint32_t i = 0; i < arr_len && complete; ++i) {
      int child_valid = 0;
      Value elem = pipe_deserialize_value_inner(rfd, &child_valid);
      if (!child_valid || arr_set(arr, (size_t)i, elem) != 0)
        complete = 0;
      val_free(&elem);
    }
    if (!complete) {
      arr_unref(arr);
      result = val_err(99, "pipe_recv: array decode refused");
      decoded = 0;
    } else {
      result.type = TYPE_ARR;
      result.data = arr;
      result.ival = 0;
      result.fval = 0.0;
      result.err_code = 0;
      result.err_msg  = NULL;
    }
    break;
  }
  default:
    result = val_err(99, "pipe_recv: unknown type");
    decoded = 0;
    break;
  }

  if (str_buf != NULL) nexs_free(str_buf, reservation_bytes);
  if (valid != NULL)
    *valid = decoded;
  return result;
}

static int pipe_deserialize_value(int rfd, Value *output) {
  if (output == NULL)
    return -1;
  int valid = 0;
  Value candidate = pipe_deserialize_value_inner(rfd, &valid);
  if (!valid) {
    val_free(&candidate);
    return -1;
  }
  *output = candidate;
  return 0;
}

/* =========================================================
   IN-PLACE QUEUE UPGRADE — pre-fork, not post-fork
   ========================================================= */

/* One owned queue is converted only after every pending message was
 * successfully transferred. A failed conversion retains the old queue.
 * The write side is nonblocking so a full provider pipe fails rather
 * than stalling fork forever. Larger messages need REG's future bounded
 * frame/lease transport; this is NOT the finalized REG wire format. */
static int reg_ipc_write_pending(int fd, const RegIpcQueue *queue) {
  MsgNode *entry = queue->head;
  if (queue->count < 0)
    return -1;
  size_t count = 0U;
  for (; entry != NULL && count < (size_t)queue->count;
       entry = entry->next, ++count) {
    if (pipe_serialize_value(fd, &entry->msg) != 0)
      return -1;
  }
  return (entry == NULL && count == (size_t)queue->count) ? 0 : -1;
}

static void reg_ipc_release_pending(RegIpcQueue *queue) {
  MsgNode *entry = queue->head;
  size_t budget = (size_t)queue->count;
  for (size_t n = 0U; entry != NULL && n < budget; ++n) {
    MsgNode *next = entry->next;
    val_free(&entry->msg);
    xfree(entry);
    entry = next;
  }
  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
}

/* The tentative pipe is closed on failure, leaving all original Values
 * in the local RegIpcQueue (no mutation before successful admission). */
static int reg_ipc_upgrade_queue(RegIpcQueue *queue) {
  if (queue == NULL || queue->use_pipe)
    return 0;
  int pair[2] = {-1, -1};
  if (nexs_ipc_pipe_open(pair) != 0)
    return -1;
  if (reg_ipc_write_pending(pair[1], queue) != 0) {
    nexs_ipc_pipe_abort(pair);
    return -1;
  }
  reg_ipc_release_pending(queue);
  queue->pipe_fd[0] = pair[0];
  queue->pipe_fd[1] = pair[1];
  queue->use_pipe = 1;
  return 0;
}

/* Traverse the ONE owned RegKey tree using its existing parent/sibling
 * links. The old fixed-size DFS stack silently skipped queues. */
static int walk_and_enable_pipes(RegKey *root) {
  if (root == NULL)
    return -1;
  size_t budget = g_registry.total_keys;
  RegKey *node = root;
  for (size_t visited = 0U; node != NULL && visited <= budget; ++visited) {
    if (reg_ipc_upgrade_queue(node->queue) != 0)
      return -1;
    if (node->children != NULL) {
      node = node->children;
    } else {
      size_t hops = 0U;
      while (node != root && node->next == NULL && hops < budget) {
        node = node->parent;
        ++hops;
        if (node == NULL)
          return -1;
      }
      node = node == root ? NULL : node->next;
    }
  }
  return node == NULL ? 0 : -1;
}

int reg_ipc_enable_pipes(void) {
  if (g_registry.root == NULL)
    return -1;
  return walk_and_enable_pipes(g_registry.root);
}

/* =========================================================
   reg_ipc_init_queue
   ========================================================= */

int reg_ipc_init_queue(const char *path, int max_count) {
  if (!path) return -1;

  RegKey *k = reg_lookup(path);
  if (!k) k = reg_mkpath(path, RK_ALL);
  if (!k) return -1;

  if (k->queue) return 0; /* Already has a queue */

  RegIpcQueue *q = nexs_alloc(sizeof(RegIpcQueue));
  if (q == NULL)
    return -1;
  q->head        = NULL;
  q->tail        = NULL;
  q->count       = 0;
  q->max_count   = (max_count > 0) ? max_count : MSG_QUEUE_SIZE;
  q->pipe_fd[0]  = -1;
  q->pipe_fd[1]  = -1;
  q->use_pipe    = 0;
  k->queue = q;
  return 0;
}

/* =========================================================
   reg_ipc_send
   ========================================================= */

int reg_ipc_send(const char *path, Value msg) {
  if (!path) return -1;

  RegKey *k = reg_lookup(path);
  if (!k) k = reg_mkpath(path, RK_ALL);
  if (!k) return -1;

  if (!k->queue) {
    if (reg_ipc_init_queue(path, MSG_QUEUE_SIZE) != 0) return -1;
    k = reg_lookup(path);
    if (!k || !k->queue) return -1;
  }

  RegIpcQueue *q = k->queue;

  /* --- Pipe transport path --- */
  if (q->use_pipe) {
    if (q->pipe_fd[1] < 0) return -1;
    return pipe_serialize_value(q->pipe_fd[1], &msg);
  }

  /* --- In-process queue path --- */
  if (q->count >= q->max_count) return -1; /* full */

  MsgNode *node = nexs_alloc(sizeof(MsgNode));
  if (node == NULL)
    return -1;
  Value copy = val_nil();
  if (val_clone_checked(&msg, &copy) != 0) {
    nexs_free(node, sizeof(MsgNode));
    return -1;
  }
  node->msg = copy;
  node->next = NULL;

  if (!q->tail) {
    q->head = q->tail = node;
  } else {
    q->tail->next = node;
    q->tail = node;
  }
  q->count++;
  return 0;
}

/* =========================================================
   reg_ipc_recv
   ========================================================= */

int reg_ipc_recv(const char *path, Value *out_msg) {
  if (!path || !out_msg) return -1;

  RegKey *k = reg_lookup(path);
  if (!k || !k->queue) return -1;

  RegIpcQueue *q = k->queue;

  /* --- Pipe transport path --- */
  if (q->use_pipe) {
    if (q->pipe_fd[0] < 0) return -1;
    /* Non-blocking check via poll */
    struct pollfd pfd;
    pfd.fd     = q->pipe_fd[0];
    pfd.events = POLLIN;
    int ready  = poll(&pfd, 1, 0); /* timeout = 0 ms → non-blocking */
    if (ready <= 0) return -1;     /* no data available */
    Value v = val_nil();
    /* Framing validity is a separate status. TYPE_ERR(99) may be a
     * legitimate user's message and cannot be the transport sentinel. */
    if (pipe_deserialize_value(q->pipe_fd[0], &v) != 0)
      return -1;
    *out_msg = v;
    return 0;
  }

  /* --- In-process queue path --- */
  if (!q->head || q->count == 0) return -1; /* empty */

  MsgNode *node = q->head;
  q->head = node->next;
  if (!q->head) q->tail = NULL;
  q->count--;

  *out_msg = node->msg; /* Transfer ownership */
  xfree(node);
  return 0;
}

/* =========================================================
   reg_ipc_pending
   ========================================================= */

int reg_ipc_pending(const char *path) {
  if (!path) return 0;
  RegKey *k = reg_lookup(path);
  if (!k || !k->queue) return 0;

  RegIpcQueue *q = k->queue;

  /* --- Pipe transport path: use poll to check without consuming --- */
  if (q->use_pipe) {
    if (q->pipe_fd[0] < 0) return 0;
    struct pollfd pfd;
    pfd.fd     = q->pipe_fd[0];
    pfd.events = POLLIN;
    int ready  = poll(&pfd, 1, 0);
    return (ready > 0) ? 1 : 0;
    /* Note: returns 1 or 0, not an exact count — pipes are not random-access.
     * For the common check `if msgpending /q > 0` this is correct. */
  }

  return q->count;
}
