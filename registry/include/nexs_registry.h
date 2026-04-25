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
} RegIpcQueue;

/* =========================================================
   REGISTRY KEY
   ========================================================= */

typedef struct RegKey RegKey;

struct RegKey {
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
  RegKey *root;
  size_t  total_keys;
} Registry;

extern Registry g_registry;

/* =========================================================
   CORE REGISTRY API
   ========================================================= */

NEXS_API void    reg_init(void);
NEXS_API RegKey *reg_mkpath(const char *path, uint8_t rights);
NEXS_API RegKey *reg_lookup(const char *path);
NEXS_API RegKey *reg_resolve(const char *name, const char *scope_path);
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

/* =========================================================
   BUILT-IN REGISTRATION HELPER
   ========================================================= */

/* Registers a C function as a TYPE_FN value at /sys/<name> */
NEXS_API void reg_register_builtin(const char *name, void *fn);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_REGISTRY_H */
