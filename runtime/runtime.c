/*
 * runtime/runtime.c — NEXS Runtime Initialisation
 * =================================================
 * Provides nexs_runtime_init() and nexs_print_version() only.
 * Compiled into both the hosted interpreter AND standalone compiled binaries.
 * Does NOT contain main() — that lives in runtime/main.c (CLI) or in the
 * codegen-generated wrapper (compiled scripts).
 */

#include "include/nexs_runtime.h"
#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"
#include "../registry/include/nexs_registry.h"
#include "../lang/include/nexs_fn.h"
#include "../sys/include/nexs_sys.h"

#include <stdio.h>
#include <string.h>

extern void builtins_register_all(void);

/* =========================================================
   VERSION
   ========================================================= */

void nexs_print_version(FILE *out) {
  fprintf(out,
          "\033[1;36mNEXS\033[0m v%d.%d.%d — Buddy/Registry Runtime + Plan 9 Syscalls\n"
          "  Pool: %dKB  MinBlock: %dB  FnTable: %d slots\n"
          "  Syscalls: open create close read write seek stat pipe rfork exec\n"
          "  Inspired by: Thompson · Ritchie · Pike · Rashid\n\n",
          NEXS_VERSION_MAJOR, NEXS_VERSION_MINOR, NEXS_VERSION_PATCH,
          POOL_SIZE / 1024, MIN_BLOCK, NEXS_MAX_FN_DEFS);
}

/* =========================================================
   RUNTIME INIT
   ========================================================= */

void nexs_runtime_init(void) {
  /* 1. Clear buddy pool and tree */
  memset(memory_pool, 0, sizeof(memory_pool));
  memset(buddy_tree,  0, sizeof(buddy_tree));
  g_array_count = 0;

  /* 2. Initialise fn_table */
  fn_table_init();

  /* 3. Initialise hierarchical registry */
  reg_init();

  /* 4. Initialise fd table (stdin/stdout/stderr) */
  sysio_init();

  /* 5. Register standard built-ins */
  builtins_register_all();

  /* 6. Register Plan 9 I/O built-ins */
  sysio_register_builtins();

  /* 7. Register Plan 9 process built-ins */
  sysproc_register_builtins();
}
