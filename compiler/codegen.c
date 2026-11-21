/*
 * compiler/codegen.c — NEXS Code Generator
 * ==========================================
 * Translates a .nx source file into a C wrapper that embeds the script
 * source, all exec() dependencies, and provides a main() (or
 * nexs_main_baremetal()) that initialises the runtime and evaluates the
 * script.
 *
 * Dependency bundling:
 *   In standalone mode (default), all files referenced by exec("path") are
 *   recursively scanned and embedded as static char arrays. At runtime
 *   nexs_exec() checks the embedded table before falling back to fopen().
 *
 * New API:
 *   nexs_codegen_ex(src, out_c, no_dep)  — extended, controls bundling.
 *   nexs_codegen(src, out_c)             — backwards-compat wrapper (no_dep=0).
 */

#include "../core/include/nexs_common.h"
#include "include/nexs_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   HELPERS
   ========================================================= */

/* Read entire file into a malloc'd buffer (caller frees) */
static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "r");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  if (sz == 0) {
    fclose(f);
    *out_len = 0;
    char *empty = malloc(1);
    empty[0] = '\0';
    return empty;
  }
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  buf[rd] = '\0';
  fclose(f);
  *out_len = rd;
  return buf;
}

/* Escape a string as a C string literal (writes to out FILE*) */
static void write_c_string(FILE *out, const char *s, size_t len) {
  fputc('"', out);
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
    case '"':
      fputs("\\\"", out);
      break;
    case '\\':
      fputs("\\\\", out);
      break;
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputs("\\t", out);
      break;
    case '\0':
      fputs("\\0", out);
      break;
    default:
      if (c < 32 || c > 126)
        fprintf(out, "\\x%02x", (unsigned int)c);
      else
        fputc((char)c, out);
      break;
    }
  }
  fputc('"', out);
}

/* =========================================================
   FORWARD DECLARATIONS emitted in generated C
   ========================================================= */

static void emit_forward_decls(FILE *out) {
  /* AOT and interpreter MUST share the exact Value/EvalCtx/Result ABI.
   * Re-creating those structures in generated source was corrupting the stack
   * at POOL_64MB (REG_PATH_MAX=512 vs generated scope[256]).
   */
  fprintf(out,
          "#include \"nexs_eval.h\"\n"
          "#include \"nexs_runtime.h\"\n"
          "#include \"nexs_hal.h\"\n"
          "\n");
}

/* =========================================================
   EMBEDDED DEP TABLE emitted in generated C
   ========================================================= */

/*
 * Emits:
 *   static const char nexs_dep_N_src[] = "...";   (one per dep)
 *
 *   typedef struct { const char *path; const char *src; } NexsEmbedDep;
 *   static const NexsEmbedDep nexs_embed_deps[] = { ... };
 *   int nexs_embed_dep_count = N;
 *
 * At runtime sysproc.c will call nexs_embedded_lookup() which is also
 * emitted here as a static helper so standalone binaries are self-contained.
 */
static void emit_dep_table(FILE *out, NexsDepEntry *deps, int n_deps) {
  /* Embed each dep source */
  for (int i = 0; i < n_deps; i++) {
    fprintf(out, "static const char nexs_dep_%d_src[] =\n", i);
    if (deps[i].src) {
      write_c_string(out, deps[i].src, strlen(deps[i].src));
    } else {
      fprintf(out, "\"\"  /* WARNING: could not read '%s' */", deps[i].path);
    }
    fprintf(out, ";\n\n");
  }

  /* Emit the table struct (must match the extern in sysproc.c) */
  fprintf(
      out,
      "/* Embedded dependency table — used by nexs_exec() at runtime */\n"
      "typedef struct { const char *path; const char *src; } NexsEmbedDep;\n"
      "const NexsEmbedDep nexs_embed_deps_table[] = {\n");
  for (int i = 0; i < n_deps; i++) {
    fprintf(out, "  { \"%s\", nexs_dep_%d_src },\n", deps[i].path, i);
  }
  fprintf(out, "  { (void*)0, (void*)0 }  /* sentinel */\n};\n\n");

  fprintf(out, "int nexs_embed_dep_count = %d;\n\n", n_deps);
  /* AOT and hosted interpreter expose the same logical resource API.
   * Never interpret embedded binary assets as ordinary C strings. */
  fprintf(out,
          "size_t nexs_embedded_resource_count(void) { "
          "return (size_t)nexs_embed_dep_count; }\n"
          "const char *nexs_embedded_resource_at(size_t index) {\n"
          "  if (index >= (size_t)nexs_embed_dep_count) return (void*)0;\n"
          "  return nexs_embed_deps_table[index].path;\n"
          "}\n\n");

}

static void emit_embedded_lookup_impl(FILE *out) {
  /*
   * The nexs_embedded_lookup function is what nexs_exec() calls when it
   * can't find a file on disk. It checks this generated table.
   * Declared as a weak symbol so that the non-standalone interpreter build
   * (which provides its own empty version in sysproc.c) links cleanly.
   */
  fprintf(
      out,
      "/* Lookup helper — returns embedded source for path, or NULL */\n"
      "static const char *nexs_embedded_lookup_local(const char *path) {\n"
      "  for (int _i = 0; nexs_embed_deps_table[_i].path; _i++) {\n"
      "    if (__builtin_strcmp(nexs_embed_deps_table[_i].path, path) == 0)\n"
      "      return nexs_embed_deps_table[_i].src;\n"
      "  }\n"
      "  return (void*)0;\n"
      "}\n\n"
      "/* Provide the symbol that sysproc.c's nexs_exec() calls */\n"
      "const char *nexs_embedded_lookup(const char *path) {\n"
      "  return nexs_embedded_lookup_local(path);\n"
      "}\n\n"
      "/* Compiler-linked sources use the same PURE library classifier as hosted.\n"
      " * Resource publication must never automatically activate a system service. */\n"
      "extern int nexs_source_is_bootstrap_library(const char *, const char *);\n"
      "int nexs_embed_load_all(EvalCtx *ctx) {\n"
      "  if (ctx == NULL) return -1;\n"
      "  for (int i = 0; i < nexs_embed_dep_count; ++i) {\n"
      "    const char *name = nexs_embed_deps_table[i].path;\n"
      "    const char *source = nexs_embed_deps_table[i].src;\n"
      "    int kind = nexs_source_is_bootstrap_library(name, source);\n"
      "    if (kind < 0) return -1;\n"
      "    if (kind == 1) {\n"
      "      EvalResult r = eval_file(ctx, name);\n"
      "      int failed = r.sig == CTRL_ERR;\n"
      "      val_free(&r.ret_val);\n"
      "      if (failed != 0) return -1;\n"
      "    }\n"
      "  }\n"
      "  return 0;\n"
      "}\n"
      "\n");
}

/* =========================================================
   nexs_codegen_ex
   ========================================================= */

int nexs_codegen_ex(const char *src_path, const char *out_c_path, int no_dep,
                    int is_baremetal) {
  size_t src_len = 0;
  char *src = read_file(src_path, &src_len);
  if (!src) {
    fprintf(stderr, "nexs: cannot read source file '%s'\n", src_path);
    return -1;
  }

  FILE *out = fopen(out_c_path, "w");
  if (!out) {
    fprintf(stderr, "nexs: cannot open output file '%s'\n", out_c_path);
    free(src);
    return -1;
  }

  fprintf(out, "/* Generated by NEXS AOT Compiler — DO NOT EDIT */\n");
  fprintf(out, "#include <stdio.h>\n"
               "#include <stdlib.h>\n"
               "#include <string.h>\n"
               "#include <stddef.h>\n\n");

  /* --- Dependency scanning --- */
  NexsDepEntry *deps = NULL;
  int n_deps = 0;
  if (!no_dep) {
    deps = calloc((size_t)NEXS_MAX_DEPS, sizeof(NexsDepEntry));
    if (deps) {
      /* 1. Explicit dependencies from source */
      n_deps = nexs_scan_deps(src_path, deps, NEXS_MAX_DEPS);

      /* 2. The SAME nxlib tree owns both libraries and services.
       * Logical source paths remain the unique embedded dependency keys. */
      n_deps = nexs_scan_directory("nxlib", deps, n_deps, NEXS_MAX_DEPS);
      if (n_deps < 0) {
        fprintf(stderr, "nexs: incomplete nxlib dependency catalog\n");
        nexs_free_deps(deps, NEXS_MAX_DEPS);
        free(deps);
        fclose(out);
        free(src);
        return -1;
      }

      if (n_deps > 0) {
        fprintf(stdout, "[codegen] bundling %d dependenc%s\n", n_deps,
                n_deps == 1 ? "y" : "ies");
        for (int i = 0; i < n_deps; i++)
          fprintf(stdout, "  dep[%d] = %s%s\n", i, deps[i].path,
                  deps[i].src ? "" : "  (UNREADABLE — will be empty)");
      }
    }
  }

  /* --- Forward declarations --- */
  emit_forward_decls(out);

  /* --- Embedded script --- */
  fprintf(out, "static const char nexs_script_src[] =\n");
  write_c_string(out, src, src_len);
  fprintf(out, ";\n\n");

  /* --- Canonical dependencies table, including an empty sentinel table --- */
  emit_dep_table(out, deps, n_deps);
  emit_embedded_lookup_impl(out);
  if (deps != NULL) {
    nexs_free_deps(deps, n_deps);
    free(deps);
  }

  /* --- main() or nexs_main_baremetal() --- */
  fprintf(out, "extern void nexs_repl(void);\n"
               "extern void nexs_hal_halt(void);\n\n");

  if (is_baremetal) {
    fprintf(out, "void nexs_main_baremetal(void) {\n"
                 "    extern int g_nexs_debug;\n"
                 "    g_nexs_debug = 1; /* Full execution trace enabled */\n"
                 "    if (nexs_runtime_init() != 0) {\n"
                 "        extern void nexs_hal_print(const char *s);\n"
                 "        nexs_hal_print(\"NEXS runtime admission failed\\n\");\n"
                 "        nexs_hal_halt();\n"
                 "        return;\n"
                 "    }\n"
                 "    if (nexs_script_src[0]) {\n"
                 "        EvalCtx ctx;\n"
                 "        eval_ctx_init(&ctx);\n"
                 "        EvalResult r = eval_str(&ctx, nexs_script_src);\n"
                 "        if (r.sig == CTRL_ERR) {\n"
                 "            void nexs_hal_print(const char *s);\n"
                 "            nexs_hal_print(\"\\033[1;31m[NEXS FATAL ERROR]\\033[0m\\n\");\n"
                 "            if (r.ret_val.type == TYPE_ERR && r.ret_val.err_msg) {\n"
                 "                nexs_hal_print(r.ret_val.err_msg);\n"
                 "                nexs_hal_print(\"\\n\");\n"
                 "            }\n"
                 "        }\n"
                 "        val_free(&r.ret_val);\n"
                 "    } else {\n"
                 "        nexs_repl();\n"
                 "    }\n"
                 "    nexs_hal_halt();\n"
                 "}\n");
  } else {
    fprintf(out, "int main(int argc, char *argv[]) {\n"
                 "    (void)argc; (void)argv;\n"
                 "    if (nexs_runtime_init() != 0) {\n"
                 "        fprintf(stderr, \"NEXS runtime admission failed\\n\");\n"
                 "        return 1;\n"
                 "    }\n"
                 "    if (nexs_script_src[0]) {\n"
                 "        EvalCtx ctx;\n"
                 "        eval_ctx_init(&ctx);\n"
                 "        EvalResult r = eval_str(&ctx, nexs_script_src);\n"
                 "        if (r.sig == CTRL_ERR) {\n"
                 "            fprintf(stderr, \"[ERR] \");\n"
                 "            val_print(&r.ret_val, stderr);\n"
                 "            fprintf(stderr, \"\\n\");\n"
                 "            val_free(&r.ret_val);\n"
                 "            return 1;\n"
                 "        }\n"
                 "        val_free(&r.ret_val);\n"
                 "    } else {\n"
                 "        nexs_repl();\n"
                 "    }\n"
                 "    return 0;\n"
                 "}\n");
  }

  fclose(out);
  free(src);
  return 0;
}

/* =========================================================
   nexs_codegen — backwards-compatible wrapper
   ========================================================= */

int nexs_codegen(const char *src_path, const char *out_c_path) {
  return nexs_codegen_ex(src_path, out_c_path, 0 /* bundle deps */,
                         0 /* is_baremetal */);
}

/* =========================================================
   TARGET INFO
   ========================================================= */

#include "targets.h"

const char *target_name(CompileTarget target) {
  if (target < 0 || target >= TARGET_COUNT)
    return "unknown";
  return nexs_targets[target].name;
}

const char *target_gcc_flags(CompileTarget target) {
  static char buf[512];
  if (target < 0 || target >= TARGET_COUNT)
    return "";
  const TargetConfig *tc = &nexs_targets[target];
  snprintf(buf, sizeof(buf), "%s %s", tc->arch_flags, tc->os_flags);
  return buf;
}
