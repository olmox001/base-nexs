/*
 * nexs_registry.h — NEXS Hierarchical Registry API
 * ====================================================
 * The "regedit linker" — hierarchical key-value store.
 * Includes IPC message queue support per key.
 */

#ifndef NEXS_REGISTRY_H
#define NEXS_REGISTRY_H
#pragma once

#include "../../core/include/nexs_common.h"
#include "../../core/include/nexs_value.h"
#include "nexs_state_store.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   IPC MESSAGE QUEUE
   ========================================================= */

typedef struct MsgNode MsgNode;
struct MsgNode {
  Value     msg;   /* owned by this node; transferred on recv */
  MsgNode  *next;
};

typedef struct {
  MsgNode *head;       /* dequeue from head */
  MsgNode *tail;       /* enqueue at tail */
  int      count;
  int      max_count;
  /* Pipe-backed IPC transport (initialized BEFORE hosted rfork/fork).
   * pipe_fd[0] = read end, pipe_fd[1] = write end.
   * -1 means the pipe has not been created yet. */
  int      pipe_fd[2];
  int      use_pipe;   /* 0 = in-process queue; 1 = pipe transport active */
} RegIpcQueue;

/* =========================================================
   REGISTRY KEY
   ========================================================= */

typedef struct RegKey RegKey;

struct RegKey {
  /* Stable NXOLMO object reference; never a pointer/slot authority grant.
   * The following named links are a local path projection until P3 cutover. */
  nx_object_ref object;
  char       name[NAME_LEN];
  char       path[REG_PATH_MAX];
  Value      val;
  uint8_t    rights;
  RegKey    *parent;
  RegKey    *children;
  RegKey    *next;
  RegIpcQueue *queue;   /* IPC queue — NULL if not initialised */
};

typedef struct {
  /* One canonical NXOLMO object state machine. No second object-ID table. */
  nx_state_store state;
  nx_state_slot *slots;
  uint32_t *index;
  uint32_t *rehash_index;
  RegKey *root;
  size_t total_keys;
} Registry;

extern Registry g_registry;

/* =========================================================
   CORE REGISTRY API
   ========================================================= */

/* 0 only when all namespace roots are admitted; -1 on OOM/reentry.
 * A failed initialization publishes no root and reclaims its allocations. */
/* ONE shared NXOLMO RegKey path admission contract for registry, VFS and
 * installed/bundled sources. 1=valid exact canonical absolute path,
 * 0=invalid. Does not authenticate a caller or allocate resources. */
NEXS_API int     reg_path_valid(const char *path);
NEXS_API int     reg_init(void);
/* Actual canonical object reference/typed metadata, not a RegKey pointer.
 * This path projection is not an authority grant; NXREG capability checks
 * must be applied at protected call/commit boundaries. */
NEXS_API nx_status reg_object_read(const char *path, nx_state_node *node,
                                    nx_result *result);
NEXS_API RegKey *reg_mkpath(const char *path, uint8_t rights);
NEXS_API RegKey *reg_lookup(const char *path);
NEXS_API RegKey *reg_resolve(const char *name, const char *scope_path);
/* Same namespace, typed callable resolution. Preserves the separate
 * variable-versus-callable lookup semantics without duplicate tables. */
NEXS_API RegKey *reg_resolve_callable(const char *name,
                                      const char *scope_path);
NEXS_API int     reg_set(const char *path, Value val, uint8_t rights);
NEXS_API Value   reg_get(const char *path);
NEXS_API int     reg_delete(const char *path);
NEXS_API void    reg_ls(const char *path, FILE *out);
NEXS_API void    reg_ls_recursive(const char *path, FILE *out, int depth);
NEXS_API int     reg_move(const char *src, const char *dst);
NEXS_API int     reg_mount(const char *src_path, const char *dst_path, int before);
NEXS_API int     reg_unmount(const char *src_path, const char *dst_path);
NEXS_API int     reg_bind(const char *src_path, const char *dst_path, int flag);
NEXS_API char   *reg_push_scope(const char *fn_name);
NEXS_API void    reg_pop_scope(const char *scope_path);
NEXS_API void    reg_key_print(RegKey *k, FILE *out);

/* =========================================================
   POINTER EXTENSIONS
   ========================================================= */

/*
 * reg_set_ptr: set key at 'path' to TYPE_PTR pointing at 'target'.
 * Creates key if it does not exist.
 */
NEXS_API int   reg_set_ptr(const char *path, const char *target);

/*
 * reg_get_deref: follow the TYPE_PTR chain at 'path' and return
 * the value at the final target.  Returns val_err if path is not
 * a pointer or the target does not exist.
 */
NEXS_API Value reg_get_deref(const char *path);

/* =========================================================
   IPC QUEUE API
   ========================================================= */

/*
 * reg_ipc_init_queue: lookup/create key at 'path', allocate a
 * RegIpcQueue if not already present.  max_count <= 0 → MSG_QUEUE_SIZE.
 */
NEXS_API int reg_ipc_init_queue(const char *path, int max_count);

/*
 * reg_ipc_send: non-blocking enqueue of 'msg' into the queue at 'path'.
 * Ownership of msg is transferred (val_clone is made internally).
 * Returns 0 on success, -1 on error (no queue, full, etc.).
 */
NEXS_API int reg_ipc_send(const char *path, Value msg);

/*
 * reg_ipc_recv: non-blocking dequeue.  Transfers ownership of the message
 * to *out_msg.  Caller must val_free(out_msg) when done.
 * Returns 0 on success, -1 if queue empty or not initialised.
 */
NEXS_API int reg_ipc_recv(const char *path, Value *out_msg);

/*
 * reg_ipc_pending: return the number of pending messages in the queue,
 * or 0 if the queue does not exist.
 */
NEXS_API int reg_ipc_pending(const char *path);

/*
 * reg_ipc_enable_pipes — prepare existing queues BEFORE hosted fork so
 * the two processes inherit the SAME actual pipe file descriptors.
 * Uses bounded in-place registry traversal; fails closed on provider pipe
 * exhaustion without discarding the old queue's pending messages.
 * Returns 0 on success, -1 on failure; not the finalized REG protocol.
 */
NEXS_API int reg_ipc_enable_pipes(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_REGISTRY_H */
