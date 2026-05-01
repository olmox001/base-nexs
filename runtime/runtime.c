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
#include "../lang/include/nexs_eval.h"
#include "../sys/include/nexs_sys.h"

#include <stdio.h>
#include <string.h>
#ifndef NEXS_BAREMETAL
#include <dirent.h>
#endif

extern void builtins_register_all(void);

/* =========================================================
   VERSION
   ========================================================= */

#include "../core/include/nexs_utils.h"
#include "../core/include/nexs_value.h"
#include "../core/include/nexs_common.h"

void nexs_print_version(FILE *out) {
  nexs_fprintf(out,
          "\033[1;36mNEXS\033[0m v%d.%d.%d — Buddy/Registry Runtime + Plan 9 Syscalls\n"
          "  Pool: %dKB  MinBlock: %dB  FnTable: %d slots\n"
          "  Syscalls: open create close read write seek stat pipe rfork exec\n"
          "  Inspired by: Thompson · Ritchie · Pike · Rashid\n\n",
          NEXS_VERSION_MAJOR, NEXS_VERSION_MINOR, NEXS_VERSION_PATCH,
          POOL_SIZE / 1024, MIN_BLOCK, MAX_FN_DEFS);
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

#ifndef NEXS_BAREMETAL
  /* 8. Auto-load modules/ .nx files — standard library functions */
  {
    DIR *d = opendir("modules");
    if (d) {
      struct dirent *ent;
      while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 3 && strcmp(ent->d_name + nlen - 3, ".nx") == 0) {
          char path[512];
          snprintf(path, sizeof(path), "modules/%s", ent->d_name);
          EvalCtx ctx;
          eval_ctx_init(&ctx);
          EvalResult r = eval_file(&ctx, path);
          val_free(&r.ret_val);
        }
      }
      closedir(d);
    }
  }
#endif
}
