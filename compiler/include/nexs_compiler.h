/*
 * nexs_compiler.h — NEXS Compiler / Cross-Compilation API
 * ==========================================================
 * CompileTarget enum, CompileOptions, and the compilation pipeline.
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
} CompileOptions;

/* =========================================================
   COMPILER API
   ========================================================= */

/*
 * nexs_compile_file: compile a .nx source file for the given target.
 * Returns 0 on success, -1 on error.
 */
NEXS_API int nexs_compile_file(const char *src_path,
                                CompileTarget target,
                                const char *out_path);

/*
 * nexs_codegen: translate src_path (.nx) into a C wrapper file at out_c_path.
 * The generated C file embeds the script source and provides a main() that
 * initialises the runtime and evaluates the script.
 * Returns 0 on success, -1 on error.
 */
NEXS_API int nexs_codegen(const char *src_path, const char *out_c_path);

/*
 * target_gcc_flags: return a static string with all GCC flags for target.
 * The returned string is statically allocated (do not free).
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
