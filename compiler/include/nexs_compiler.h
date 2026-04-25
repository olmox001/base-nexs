/*
 * nexs_compiler.h — NEXS Compiler / Cross-Compilation API
 * ==========================================================
 * CompileTarget enum, CompileOptions, dependency scan API,
 * and the compilation pipeline.
 */

#ifndef NEXS_COMPILER_H
#define NEXS_COMPILER_H
#pragma once

#include "../../core/include/nexs_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   COMPILE TARGET
   ========================================================= */

typedef enum {
  TARGET_LINUX_AMD64     = 0,
  TARGET_LINUX_ARM64     = 1,
  TARGET_MACOS_AMD64     = 2,   /* macOS Intel (x86_64) */
  TARGET_MACOS_ARM64     = 3,   /* macOS Apple Silicon  */
  TARGET_PLAN9_AMD64     = 4,
  TARGET_BAREMETAL_ARM64 = 5,
  TARGET_BAREMETAL_AMD64 = 6,
  TARGET_COUNT           = 7,
} CompileTarget;

/* =========================================================
   COMPILE OPTIONS
   ========================================================= */

typedef struct {
  CompileTarget target;
  const char   *output_path;   /* path to write output binary */
  int           verbose;       /* 1 = print compiler commands */
  int           keep_temps;    /* 1 = do not delete temp .c file */
  const char   *extra_cflags;  /* appended to the gcc command */
  int           no_dep;        /* --no-dep: skip dependency bundling */
} CompileOptions;

/* =========================================================
   DEPENDENCY SCAN
   ========================================================= */

/* Maximum path length for a dependency entry */
#define NEXS_DEP_PATH_MAX REG_PATH_MAX

/* Maximum number of unique dependencies per compilation unit */
#define NEXS_MAX_DEPS 256

/*
 * NexsDepEntry — one unique dependency discovered via exec("path").
 *   path — the resolved filesystem path of the dependency.
 *   src  — malloc'd copy of the file contents (NULL if unreadable).
 *          Must be freed with nexs_free_deps() after use.
 */
typedef struct {
  char  path[NEXS_DEP_PATH_MAX];
  char *src;   /* heap-alloc'd by nexs_scan_deps; freed by nexs_free_deps */
} NexsDepEntry;

/*
 * nexs_scan_deps — recursively scan src_path for exec("path") calls.
 * Fills deps[] in depth-first insertion order, deduplicating by resolved path.
 * Each deps[i].src is malloc'd; call nexs_free_deps() when done.
 * Returns the count of unique deps found (0 = none or error).
 */
NEXS_API int  nexs_scan_deps(const char *src_path,
                               NexsDepEntry *deps,
                               int max_deps);

/*
 * nexs_free_deps — release .src buffers allocated by nexs_scan_deps.
 */
NEXS_API void nexs_free_deps(NexsDepEntry *deps, int count);

/* =========================================================
   COMPILER API
   ========================================================= */

/*
 * nexs_compile_file: compile a .nx source file for the given target.
 * Bundles all exec() dependencies unless no_dep != 0.
 * Returns 0 on success, -1 on error.
 */
NEXS_API int nexs_compile_file(const char *src_path,
                                CompileTarget target,
                                const char *out_path);

/*
 * nexs_compile_file_ex: compile with extended options (includes no_dep flag).
 */
NEXS_API int nexs_compile_file_ex(const char *src_path,
                                   CompileTarget target,
                                   const char *out_path,
                                   int no_dep);

/*
 * nexs_codegen: translate src_path (.nx) into a C wrapper file at out_c_path.
 * Embeds all exec() dependencies unless no_dep != 0.
 * Returns 0 on success, -1 on error.
 */
NEXS_API int nexs_codegen(const char *src_path, const char *out_c_path);
NEXS_API int nexs_codegen_ex(const char *src_path, const char *out_c_path,
                              int no_dep);

/*
 * target_gcc_flags: return a static string with all GCC flags for target.
 */
NEXS_API const char *target_gcc_flags(CompileTarget target);

/*
 * target_name: return the human-readable name of a target.
 */
NEXS_API const char *target_name(CompileTarget target);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_COMPILER_H */

