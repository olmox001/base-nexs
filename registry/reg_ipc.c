/*
 * registry/reg_ipc.c — Registry IPC Message Queue Implementation
 * ================================================================
 * Non-blocking message passing through registry keys.
 * Each key can have an associated RegIpcQueue.
 */

#include "include/nexs_registry.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_common.h"
#include "../core/include/nexs_value.h"

#include <string.h>

/* =========================================================
   reg_ipc_init_queue
   ========================================================= */

int reg_ipc_init_queue(const char *path, int max_count) {
  if (!path) return -1;

  /* Lookup or create the key */
  RegKey *k = reg_lookup(path);
  if (!k) k = reg_mkpath(path, RK_ALL);
  if (!k) return -1;

  /* Already has a queue */
  if (k->queue) return 0;

  RegIpcQueue *q = xmalloc(sizeof(RegIpcQueue));
  q->head      = NULL;
  q->tail      = NULL;
  q->count     = 0;
  q->max_count = (max_count > 0) ? max_count : MSG_QUEUE_SIZE;
  k->queue = q;
  return 0;
}

/* =========================================================
   reg_ipc_send
   ========================================================= */

int reg_ipc_send(const char *path, Value msg) {
  if (!path) return -1;

  RegKey *k = reg_lookup(path);
  /* Auto-create the key if it doesn't exist yet */
  if (!k) k = reg_mkpath(path, RK_ALL);
  if (!k) return -1;

  /* Auto-initialise queue if not present */
  if (!k->queue) {
    if (reg_ipc_init_queue(path, MSG_QUEUE_SIZE) != 0) return -1;
    k = reg_lookup(path); /* re-lookup after potential creation */
    if (!k || !k->queue) return -1;
  }

  RegIpcQueue *q = k->queue;
  if (q->count >= q->max_count) return -1; /* queue full */

  MsgNode *node = xmalloc(sizeof(MsgNode));
  /* Transfer ownership: clone the message into the node */
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
  if (!q->head || q->count == 0) return -1; /* empty */

  MsgNode *node = q->head;
  q->head = node->next;
  if (!q->head) q->tail = NULL;
  q->count--;

  /* Transfer ownership of msg to caller */
  *out_msg = node->msg;
  /* Do NOT val_free(&node->msg) — caller owns it now */
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
  return k->queue->count;
}
