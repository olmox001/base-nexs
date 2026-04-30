/*
 * compiler/driver.c — NEXS Compilation Driver
 * =============================================
 * Orchestrates: dep-scan → codegen → gcc → optional ELF inspection.
 *
 * New:
 *   nexs_compile_file_ex(src, target, out, no_dep)
 *   nexs_compile_file is a backwards-compat wrapper (no_dep=0).
 */

#include "../core/include/nexs_common.h"
#include "include/nexs_compiler.h"
#include "targets.h"

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
  if (!f)
    return;

  /* Minimal ELF header parsing — just grab e_entry */
  unsigned char ident[16];
  if (fread(ident, 1, 16, f) < 16 || ident[0] != 0x7f || ident[1] != 'E' ||
      ident[2] != 'L' || ident[3] != 'F') {
    fclose(f);
    return;
  }

  int is64 = (ident[4] == 2);
  if (is64) {
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
   nexs_compile_file_ex
   ========================================================= */

int nexs_compile_file_ex(const char *src_path, CompileTarget target,
                         const char *out_path, int no_dep) {
  if (!src_path || !out_path)
    return -1;
  if (target < 0 || target >= TARGET_COUNT)
    return -1;

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

  /* --- Step 1: Detect memory footprint via Host dry-runs --- */
  const char *profiles[] = {"POOL_4KB",   "POOL_16KB", "POOL_32KB",
                            "POOL_512KB", "POOL_4MB",  "POOL_16MB"};
  const int profile_count = 6;
  int best_profile_idx = profile_count - 1; /* Default to max fallback */

  char test_c[256];
  snprintf(test_c, sizeof(test_c), "/tmp/nexs_prof_test_%d.c", (int)getpid());
  if (nexs_codegen_ex(src_path, test_c, no_dep, 0 /* test on host */) != 0) {
    fprintf(stderr, "nexs: codegen failed during profiling for '%s'\n",
            src_path);
    return -1;
  }

  if (tc->is_baremetal) {
    /* Skip host profiling for bare-metal targets - avoid infinite loops/alarms */
    best_profile_idx = profile_count - 1;
  } else {
    for (int i = 0; i < profile_count; i++) {
      char test_bin[256], build_cmd[1024], run_cmd[1024];
      snprintf(test_bin, sizeof(test_bin), "/tmp/nexs_prof_test_%d.bin",
               (int)getpid());

      snprintf(
          build_cmd, sizeof(build_cmd),
          "gcc -O2 -std=c11 -Icore/include -Iregistry/include -Ilang/include "
          "-Isys/include -Iruntime/include -Icompiler/include -Ihal/include "
          "-D%s %s core/buddy.c core/pager.c core/value.c core/dynarray.c "
          "core/utils.c registry/registry.c registry/reg_ipc.c lang/fn_table.c "
          "lang/lexer.c lang/parser.c lang/eval.c lang/builtins.c sys/sysio.c "
          "sys/sysproc.c runtime/runtime.c runtime/nexs_line.c "
          "hal/bc/nexs_hal_bc.c hal/hal_hosted.c -o %s >/dev/null 2>&1",
          profiles[i], test_c, test_bin);

      if (system(build_cmd) != 0) {
        continue;
      }

      char out_path[256];
      snprintf(out_path, sizeof(out_path), "/tmp/nexs_prof_out_%d.txt",
               (int)getpid());

      /* Use perl to set a cross-platform 1-second timeout */
      snprintf(run_cmd, sizeof(run_cmd),
               "perl -e 'alarm 1; exec \"%s\"' > %s 2>&1", test_bin, out_path);
      system(run_cmd);

      FILE *fout = fopen(out_path, "r");
      int failed = 0;
      if (fout) {
        char line[1024];
        while (fgets(line, sizeof(line), fout)) {
          if (strstr(line, "Buddy alloc failed")) {
            failed = 1;
            break;
          }
        }
        fclose(fout);
      }
      unlink(out_path);
      unlink(test_bin);

      if (!failed) {
        best_profile_idx = i;
        break;
      }
    }
  }
  unlink(test_c);

  /* --- Step 2: Generate actual C wrapper file --- */
  char script_c[256];
  snprintf(script_c, sizeof(script_c), "/tmp/nexs_build_%d.c", (int)getpid());

  if (nexs_codegen_ex(src_path, script_c, no_dep, tc->is_baremetal) != 0) {
    fprintf(stderr, "nexs: codegen failed for '%s'\n", src_path);
    return -1;
  }

  /* --- Step 3: Build gcc command --- */
  char cmd[4096];
  int pos = 0;

  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, "%s", tc->gcc_binary);
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " %s %s",
                  tc->arch_flags, tc->os_flags);
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -D%s",
                  profiles[best_profile_idx]);
  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " -Icore/include -Iregistry/include -Ilang/include"
                  " -Isys/include -Iruntime/include"
                  " -Icompiler/include -Ihal/include");

  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                  " %s"
                  " core/buddy.c core/pager.c core/value.c"
                  " core/dynarray.c core/utils.c"
                  " registry/registry.c registry/reg_ipc.c"
                  " lang/fn_table.c lang/lexer.c lang/parser.c"
                  " lang/eval.c lang/builtins.c"
                  " sys/sysio.c sys/sysproc.c"
                  " runtime/runtime.c runtime/nexs_line.c"
                  " hal/bc/nexs_hal_bc.c",
                  script_c);

  if (tc->is_baremetal) {
    pos +=
        snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " kernel/libc_stub.c");
  } else {
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " hal/hal_hosted.c");
  }

  /* Bare-metal extras */
  if (tc->is_baremetal) {
    if (tc->ld_script)
      pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -T %s",
                      tc->ld_script);
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                    " -nostdlib -nostartfiles -ffreestanding -Ikernel/include "
                    "-Wl,--no-warn-rwx-segments");

    if (strcmp(tc->name, "baremetal-arm64") == 0) {
      pos += snprintf(
          cmd + pos, sizeof(cmd) - (size_t)pos,
          " hal/arm64/boot.S hal/arm64/exc_vectors.S hal/arm64/exc_handler.c"
          " hal/arm64/uart.c hal/arm64/mmu.c hal/arm64/gic.c"
          " hal/arm64/timer.c hal/arm64/fdt.c kernel/ctx_arm64.S");
    } else if (strcmp(tc->name, "baremetal-amd64") == 0) {
      pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos,
                      " hal/amd64/boot.S hal/amd64/isr_stubs.S hal/amd64/uart.c"
                      " hal/amd64/gdt.c hal/amd64/idt.c hal/amd64/apic.c"
                      " hal/amd64/mmu.c hal/amd64/acpi.c kernel/ctx_amd64.S");
    }
  }

  pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o %s", out_path);

  fprintf(stdout, "[compile] %s\n", cmd);

  /* --- Step 3: Execute --- */
  int rc = system(cmd);

  /* --- Step 4: Cleanup temp file --- */
  unlink(script_c);

  /* --- Step 5: Print bare-metal access point info --- */
  if (rc == 0 && tc->is_baremetal)
    nexs_print_baremetal_info(out_path, tc);

  return (rc == 0) ? 0 : -1;
}

/* =========================================================
   nexs_compile_file — backwards-compat wrapper
   ========================================================= */

int nexs_compile_file(const char *src_path, CompileTarget target,
                      const char *out_path) {
  return nexs_compile_file_ex(src_path, target, out_path, 0 /* bundle deps */);
}
