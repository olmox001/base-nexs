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
#include <stdio.h>

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
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef NEXS_BUILDING
#define NEXS_API __declspec(dllexport)
#else
#define NEXS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define NEXS_API __attribute__((visibility("default")))
#else
#define NEXS_API
#endif
#endif

/* =========================================================
   VERSION
   ========================================================= */

#define NEXS_VERSION_MAJOR 0
#define NEXS_VERSION_MINOR 2
#define NEXS_VERSION_PATCH 0
#define NEXS_VERSION_STR "0.1.8"

/* =========================================================
   BUDDY ALLOCATOR & DYNAMIC LIMITS
   ========================================================= */

#if defined(POOL_64MB)
#define POOL_SIZE (64 * 1024 * 1024)
#define LARGE_POOL_PAGES 16384 /* 64MB */
#define MIN_BLOCK 128
#define MAX_STR_LEN 8192
#define MAX_TOKENS 32768
#define REG_PATH_MAX 512
#define MAX_ARRAYS 2048
#define NAME_LEN 64
#define MAX_PARAMS 16
#define MAX_FN_DEFS 2048
#elif defined(POOL_16MB)
#define POOL_SIZE (16 * 1024 * 1024)
#define LARGE_POOL_PAGES 4096 /* 16MB */
#define MIN_BLOCK 128
#define MAX_STR_LEN 8192
#define MAX_TOKENS 16384
#define REG_PATH_MAX 512
#define MAX_ARRAYS 1024
#define NAME_LEN 64
#define MAX_PARAMS 16
#define MAX_FN_DEFS 1024
#elif defined(POOL_4MB)
#define POOL_SIZE (4 * 1024 * 1024)
#define LARGE_POOL_PAGES 1024 /* 4MB */
#define MIN_BLOCK 32
#define MAX_STR_LEN 4096
#define MAX_TOKENS 8192
#define REG_PATH_MAX 512
#define MAX_ARRAYS 512
#define NAME_LEN 64
#define MAX_PARAMS 16
#define MAX_FN_DEFS 512
#elif defined(POOL_512KB)
#define POOL_SIZE (512 * 1024)
#define LARGE_POOL_PAGES 128 /* 512KB */
#define MIN_BLOCK 16
#define MAX_STR_LEN 2048
#define MAX_TOKENS 1024
#define REG_PATH_MAX 256
#define MAX_ARRAYS 128
#define NAME_LEN 64
#define MAX_PARAMS 16
#define MAX_FN_DEFS 256
#elif defined(POOL_32KB)
#define POOL_SIZE (32 * 1024)
#define LARGE_POOL_PAGES 8 /* 32KB */
#define MIN_BLOCK 16
#define MAX_STR_LEN 512
#define MAX_TOKENS 128
#define REG_PATH_MAX 128
#define MAX_ARRAYS 16
#define NAME_LEN 32
#define MAX_PARAMS 8
#define MAX_FN_DEFS 64
#elif defined(POOL_16KB)
#define POOL_SIZE (16 * 1024)
#define LARGE_POOL_PAGES 4 /* 16KB */
#define MIN_BLOCK 16
#define MAX_STR_LEN 256
#define MAX_TOKENS 64
#define REG_PATH_MAX 64
#define MAX_ARRAYS 8
#define NAME_LEN 24
#define MAX_PARAMS 4
#define MAX_FN_DEFS 32
#elif defined(POOL_4KB)
#define POOL_SIZE (4 * 1024)
#define LARGE_POOL_PAGES 1 /* 4KB */
#define MIN_BLOCK 16
#define MAX_STR_LEN 128
#define MAX_TOKENS 32
#define REG_PATH_MAX 32
#define MAX_ARRAYS 4
#define NAME_LEN 16
#define MAX_PARAMS 4
#define MAX_FN_DEFS 16
#else
/* Default (64MB) - High Stability & Performance */
#define POOL_SIZE (64 * 1024 * 1024)
#define LARGE_POOL_PAGES 16384
#define MIN_BLOCK 128
#define MAX_STR_LEN 8192
#define MAX_TOKENS 32768
#define REG_PATH_MAX 512
#define MAX_ARRAYS 2048
#define NAME_LEN 64
#define MAX_PARAMS 16
#define MAX_FN_DEFS 2048
#endif

#define NUM_LEAVES (POOL_SIZE / MIN_BLOCK)
#define TREE_NODES (2 * NUM_LEAVES - 1)

/* =========================================================
   PAGE ALLOCATOR
   ========================================================= */

#define NEXS_PAGE_SIZE 4096
#define MAX_PAGE_ALLOCS 256
#define LARGE_ALLOC_THRESH                                                     \
  (512 * 1024) /* bytes above which page alloc is used */

/* =========================================================
   DYNARRAY
   ========================================================= */

#define MAX_ARRAYS_GLOBAL 64 /* Registry-based global arrays limit */

/* =========================================================
   REGISTRY
   ========================================================= */

#define REG_ROOT "/"
#define REG_MAX_STACK_DEPTH 1024 /* For iterative DFS on heap */

/* Registry access rights (capability style) */
#define RK_READ (1 << 0)
#define RK_WRITE (1 << 1)
#define RK_EXEC (1 << 2)
#define RK_ADMIN (1 << 3)
#define RK_ALL (RK_READ | RK_WRITE | RK_EXEC | RK_ADMIN)

/* Standard registry paths */
#define REG_LOCAL "/local"
#define REG_FN "/fn"
#define REG_SYS "/sys"
#define REG_MOD "/mod"
#define REG_ENV "/env"
#define REG_TYPE "/type"

/* =========================================================
   IPC / MESSAGE QUEUE
   ========================================================= */

#define MSG_QUEUE_SIZE 64

/* =========================================================
   INTERPRETER
   ========================================================= */

#define MAX_IDENT_LEN NAME_LEN
#define MAX_CALL_DEPTH 128

/* =========================================================
   PLAN 9 FILE DESCRIPTORS
   ========================================================= */

#define NEXS_MAX_FDS 64

/* Plan 9-style open flags */
#define NEXS_OREAD 0
#define NEXS_OWRITE 1
#define NEXS_ORDWR 2
#define NEXS_OTRUNC 16

/* =========================================================
   RFORK FLAGS
   ========================================================= */

#define NEXS_RFPROC (1 << 0)
#define NEXS_RFNOWAIT (1 << 1)
#define NEXS_RFNAMEG (1 << 2)
#define NEXS_RFMEM (1 << 3)
#define NEXS_RFFDG (1 << 4)

/* =========================================================
   UTILITY MACROS
   ========================================================= */

#define NEXS_ASSERT(ptr, msg)                                                  \
  do {                                                                         \
    if (!(ptr))                                                                \
      die(msg);                                                                \
  } while (0)

#define REG_PATH(buf, ...) nexs_path_join((buf), sizeof(buf), __VA_ARGS__, NULL)

NEXS_API void nexs_fprintf(FILE *out, const char *fmt, ...);

/* Global Debug Flag */
extern int g_nexs_debug;

#ifdef __cplusplus
}
#endif

#endif /* NEXS_COMMON_H */
