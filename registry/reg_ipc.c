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
 *   After fork(), reg_ipc_enable_pipes() is called in both parent and child.
 *   It upgrades every existing RegIpcQueue to use a POSIX pipe pair as the
 *   real transport.  Serialised messages are written/read through the pipe
 *   file descriptors, which are inherited across fork() and are genuinely
 *   shared.
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
 *   TYPE_FN:             only ival (fn_table index) is meaningful.
 */

#include "include/nexs_registry.h"
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
    ssize_t wr = write(fd, p, n);
    if (wr < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += wr;
    n -= (size_t)wr;
  }
  return 0;
}

/* Read exactly n bytes from fd, retrying on EINTR.
 * Returns -1 on error or EOF. */
static int pipe_read_all(int fd, void *buf, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n > 0) {
    ssize_t rd = read(fd, p, n);
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
    str_len = (uint32_t)strlen(str_data);
    break;
  case TYPE_ERR:
    str_data = v->err_msg ? v->err_msg : "";
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
    arr_len = (uint32_t)arr->size;
  }
  uint8_t albuf[4];
  le_write32(albuf, arr_len);
  if (pipe_write_all(wfd, albuf, 4) < 0) return -1;

  if (arr_len > 0 && v->data) {
    DynArray *arr = (DynArray *)v->data;
    for (uint32_t i = 0; i < arr_len; i++) {
      Value elem = arr_get_at(arr, (size_t)i);
      if (pipe_serialize_value(wfd, &elem) < 0) {
        val_free(&elem);
        return -1;
      }
      val_free(&elem);
    }
  }

  return 0;
}

/*
 * pipe_deserialize_value — read a Value from the pipe read-end.
 * On error returns a TYPE_ERR value; caller must val_free() the result.
 */
static Value pipe_deserialize_value(int rfd) {
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
  if (str_len > 0) {
    str_buf = xmalloc(str_len + 1);
    if (pipe_read_all(rfd, str_buf, str_len) < 0) {
      xfree(str_buf);
      return val_err(99, "pipe_recv: read str_data failed");
    }
    str_buf[str_len] = '\0';
  }

  uint8_t albuf[4];
  if (pipe_read_all(rfd, albuf, 4) < 0) {
    if (str_buf) xfree(str_buf);
    return val_err(99, "pipe_recv: read arr_len failed");
  }
  uint32_t arr_len = le_read32(albuf);

  /* Reconstruct the Value */
  Value result;
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
    result = val_fn_idx(ival);
    break;
  case TYPE_STR:
    result = val_str(str_buf ? str_buf : "");
    break;
  case TYPE_REF:
    result = val_ref(str_buf ? str_buf : "/");
    break;
  case TYPE_PTR:
    result = val_ptr(str_buf ? str_buf : "/");
    break;
  case TYPE_ERR: {
    int code = (int)ival;
    result = val_err(code, str_buf ? str_buf : "");
    break;
  }
  case TYPE_ARR: {
    /* Build a temporary array name from the pointer */
    char arr_name[NAME_LEN];
    snprintf(arr_name, sizeof(arr_name), "__pipe_arr_%p__", (void *)&result);
    DynArray *arr = arr_get_or_create(arr_name);
    for (uint32_t i = 0; i < arr_len; i++) {
      Value elem = pipe_deserialize_value(rfd);
      arr_set(arr, (size_t)i, elem);
      val_free(&elem);
    }
    result.type = TYPE_ARR;
    result.data = arr;
    result.ival = 0;
    result.fval = 0.0;
    result.err_code = 0;
    result.err_msg  = NULL;
    break;
  }
  default:
    result = val_err(99, "pipe_recv: unknown type");
    break;
  }

  if (str_buf) xfree(str_buf);
  return result;
}

/* =========================================================
   QUEUE TREE WALKER — used by reg_ipc_enable_pipes
   ========================================================= */

/* Recursively walk all RegKeys and upgrade queues to pipe transport */
static void walk_and_enable_pipes(RegKey *node) {
  if (!node) return;
  if (node->queue && !node->queue->use_pipe) {
    RegIpcQueue *q = node->queue;
    if (pipe(q->pipe_fd) == 0) {
      q->use_pipe = 1;
      /* Drain the in-process queue into the pipe so we don't lose messages
       * that were sent before fork(). */
      MsgNode *m = q->head;
      while (m) {
        pipe_serialize_value(q->pipe_fd[1], &m->msg);
        MsgNode *nx = m->next;
        val_free(&m->msg);
        xfree(m);
        m = nx;
      }
      q->head  = NULL;
      q->tail  = NULL;
      q->count = 0;
    }
  }
  /* Recurse siblings and children */
  walk_and_enable_pipes(node->children);
  walk_and_enable_pipes(node->next);
}

/* =========================================================
   PUBLIC API
   ========================================================= */

void reg_ipc_enable_pipes(void) {
  /* Walk from root, skipping the root key itself */
  if (g_registry.root)
    walk_and_enable_pipes(g_registry.root->children);
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

  RegIpcQueue *q = xmalloc(sizeof(RegIpcQueue));
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

  MsgNode *node = xmalloc(sizeof(MsgNode));
  node->msg  = val_clone(&msg);
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
    Value v = pipe_deserialize_value(q->pipe_fd[0]);
    if (v.type == TYPE_ERR && v.err_code == 99) {
      /* Serialisation error — discard */
      val_free(&v);
      return -1;
    }
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
