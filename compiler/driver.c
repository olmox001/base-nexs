/*
 * compiler/driver.c — NEXS Compilation Driver
 * =============================================
 * Orchestrates: codegen → gcc → optional ELF inspection for baremetal.
 */

#include "include/nexs_compiler.h"
#include "targets.h"
#include "../core/include/nexs_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* =========================================================
   HELPERS
   ========================================================= */

/* Print entry-point info from an ELF binary (baremetal only) */
static void nexs_print_baremetal_info(const char *out_path,
                                       const TargetConfig *tc) {
  fprintf(stdout, "[baremetal] target=%s output=%s\n", tc->name, out_path);

  FILE *f = fopen(out_path, "rb");
  if (!f) return;

  /* Minimal ELF header parsing — just grab e_entry */
  unsigned char ident[16];
  if (fread(ident, 1, 16, f) < 16 || ident[0] != 0x7f ||
      ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
    fclose(f);
    return;
  }

  int is64 = (ident[4] == 2);
  if (is64) {
    /* Skip e_type(2) e_machine(2) e_version(4) = 8 bytes */
    fseek(f, 24, SEEK_SET); /* e_entry is at offset 24 in 64-bit ELF */
    unsigned long long entry = 0;
    if (fread(&entry, 8, 1, f) == 1)
      fprintf(stdout, "[baremetal] entry point: 0x%llx\n",
              (unsigned long long)entry);
  } else {
    fseek(f, 24, SEEK_SET);
    unsigned int entry = 0;
    if (fread(&entry, 4, 1, f) == 1)
      fprintf(stdout, "[baremetal] entry point: 0x%x\n", entry);
  }
  fclose(f);
}

/* =========================================================
   nexs_compile_file
   ========================================================= */

int nexs_compile_file(const char *src_path,
                       CompileTarget target,
                       const char *out_path) {
  if (!src_path || !out_path) return -1;
  if (target < 0 || target >= TARGET_COUNT) return -1;

  const TargetConfig *tc = &nexs_targets[target];

  /* --- Step 0: Create output directory if needed --- */
  {
    char outdir[512];
    strncpy(outdir, out_path, sizeof(outdir) - 1);
    outdir[sizeof(outdir) - 1] = '\0';
    char *slash = strrchr(outdir, '/');
    if (slash && slash != outdir) {
      *slash = '\0';
      char mkdir_cmd[640];
      snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", outdir);
      (void)system(mkdir_cmd);
    }
  }

  /* --- Step 1: Generate C wrapper file --- */
  char script_c[256];
  snprintf(script_c, sizeof(script_c), "/tmp/nexs_build_%d.c", (int)getpid());

  if (nexs_codegen(src_path, script_c) != 0) {
    fprintf(stderr, "nexs: codegen failed for '%s'\n", src_path);
    return -1;
  }

  /* --- Step 2: Determine include paths relative to source tree ---
   * We use the directory containing this binary's argv[0] as the base,
   * but the simplest portable approach is to use the compile-time paths.
   * For a development build we rely on the Makefile setting -I correctly.
   */

  /* --- Step 3: Build gcc command --- */
  char cmd[4096];
  int  pos = 0;

  /* Compiler binary */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, "%s", tc->gcc_binary);

  /* Architecture and OS flags */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " %s %s", tc->arch_flags, tc->os_flags);

  /* Common flags */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter");

  /* Include paths */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " -Icore/include -Iregistry/include -Ilang/include"
                  " -Isys/include -Iruntime/include"
                  " -Icompiler/include -Ihal/include");

  /* Source files: generated wrapper + all runtime modules.
   * runtime/main.c and compiler/ are excluded — the generated .c provides main().
   * runtime/runtime.c provides nexs_runtime_init() for the standalone binary. */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " %s"
                  " core/buddy.c core/pager.c core/value.c"
                  " core/dynarray.c core/utils.c"
                  " registry/registry.c registry/reg_ipc.c"
                  " lang/fn_table.c lang/lexer.c lang/parser.c"
                  " lang/eval.c lang/builtins.c"
                  " sys/sysio.c sys/sysproc.c"
                  " runtime/runtime.c"
                  " hal/bc/nexs_hal_bc.c hal/hal_hosted.c",
                  script_c);

  /* Bare-metal extras */
  if (tc->is_baremetal) {
    if (tc->ld_script)
      pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                      " -T %s", tc->ld_script);
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                    " -nostdlib -nostartfiles");

    /* Add HAL boot and uart */
    if (strcmp(tc->name, "baremetal-arm64") == 0) {
      pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                      " hal/arm64/boot.S hal/arm64/uart.c");
    } else if (strcmp(tc->name, "baremetal-amd64") == 0) {
      pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                      " hal/amd64/boot.S hal/amd64/uart.c");
    }
  }

  /* Output binary */
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o %s", out_path);

  fprintf(stdout, "[compile] %s\n", cmd);

  /* --- Step 4: Execute --- */
  int rc = system(cmd);

  /* --- Step 5: Cleanup temp file --- */
  unlink(script_c);

  /* --- Step 6: Print bare-metal access point info --- */
  if (rc == 0 && tc->is_baremetal)
    nexs_print_baremetal_info(out_path, tc);

  return (rc == 0) ? 0 : -1;
}
