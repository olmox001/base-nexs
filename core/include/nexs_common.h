/*
 * nexs_common.h — NEXS Global Constants
 * =======================================
 * All #define constants shared across all modules.
 * Include this first; it has no other NEXS dependencies.
 */

#ifndef NEXS_COMMON_H
#define NEXS_COMMON_H
#pragma once

#include <stddef.h>

#ifdef NEXS_BAREMETAL
typedef __INT8_TYPE__ int8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   VISIBILITY MACRO
   ========================================================= */

#ifndef NEXS_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef NEXS_BUILDING
#      define NEXS_API __declspec(dllexport)
#    else
#      define NEXS_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define NEXS_API __attribute__((visibility("default")))
#  else
#    define NEXS_API
#  endif
#endif

/* =========================================================
   VERSION
   ========================================================= */

#define NEXS_VERSION_MAJOR 0
#define NEXS_VERSION_MINOR 2
#define NEXS_VERSION_PATCH 0
#define NEXS_VERSION_STR   "0.2.0"

/* =========================================================
   BUDDY ALLOCATOR
   ========================================================= */

#define POOL_SIZE  (4 * 1024 * 1024)       /* 4 MB */
#define MIN_BLOCK  32                      /* minimum block 32 bytes */
#define NUM_LEAVES (POOL_SIZE / MIN_BLOCK) /* 131072 leaves */
#define TREE_NODES (2 * NUM_LEAVES - 1)   /* total nodes */

/* =========================================================
   PAGE ALLOCATOR
   ========================================================= */

#define NEXS_PAGE_SIZE       4096
#define MAX_PAGE_ALLOCS      256
#define LARGE_ALLOC_THRESH   (512 * 1024)  /* bytes above which page alloc is used */

/* =========================================================
   DYNARRAY
   ========================================================= */

#define MAX_ARRAYS 64
#define NAME_LEN   64

/* =========================================================
   REGISTRY
   ========================================================= */

#define REG_PATH_MAX 256
#define REG_CHILDREN 16
#define REG_ROOT     "/"

/* Registry access rights (capability style) */
#define RK_READ  (1 << 0)
#define RK_WRITE (1 << 1)
#define RK_EXEC  (1 << 2)
#define RK_ADMIN (1 << 3)
#define RK_ALL   (RK_READ | RK_WRITE | RK_EXEC | RK_ADMIN)

/* Standard registry paths */
#define REG_LOCAL "/local"
#define REG_FN    "/fn"
#define REG_SYS   "/sys"
#define REG_MOD   "/mod"
#define REG_ENV   "/env"
#define REG_TYPE  "/type"

/* =========================================================
   IPC / MESSAGE QUEUE
   ========================================================= */

#define MSG_QUEUE_SIZE 64

/* =========================================================
   INTERPRETER
   ========================================================= */

#define MAX_TOKENS    4096
#define MAX_IDENT_LEN NAME_LEN
#define MAX_STR_LEN   512
#define MAX_CALL_DEPTH 128
#define MAX_PARAMS    16

/* =========================================================
   FUNCTION TABLE
   ========================================================= */

#define NEXS_MAX_FN_DEFS 1024

/* =========================================================
   PLAN 9 FILE DESCRIPTORS
   ========================================================= */

#define NEXS_MAX_FDS 64

/* Plan 9-style open flags */
#define NEXS_OREAD   0
#define NEXS_OWRITE  1
#define NEXS_ORDWR   2
#define NEXS_OTRUNC  16

/* =========================================================
   RFORK FLAGS
   ========================================================= */

#define NEXS_RFPROC   (1 << 0)
#define NEXS_RFNOWAIT (1 << 1)
#define NEXS_RFNAMEG  (1 << 2)
#define NEXS_RFMEM    (1 << 3)
#define NEXS_RFFDG    (1 << 4)

/* =========================================================
   UTILITY MACROS
   ========================================================= */

#define NEXS_ASSERT(ptr, msg)                                                  \
  do {                                                                         \
    if (!(ptr))                                                                \
      die(msg);                                                                \
  } while (0)

#define REG_PATH(buf, ...) nexs_path_join((buf), sizeof(buf), __VA_ARGS__, NULL)

#ifdef __cplusplus
}
#endif

#endif /* NEXS_COMMON_H */
