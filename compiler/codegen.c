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

#include "include/nexs_compiler.h"
#include "../core/include/nexs_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   HELPERS
   ========================================================= */

/* Read entire file into a malloc'd buffer (caller frees) */
static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz <= 0) { fclose(f); *out_len = 0; return NULL; }
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return NULL; }
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
    case '"':  fputs("\\\"", out); break;
    case '\\': fputs("\\\\", out); break;
    case '\n': fputs("\\n",  out); break;
    case '\r': fputs("\\r",  out); break;
    case '\t': fputs("\\t",  out); break;
    case '\0': fputs("\\0",  out); break;
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
  fprintf(out,
          "/* Forward declarations — link against nexs runtime objects */\n"
          "void nexs_runtime_init(void);\n"
          "\n"
          "typedef enum {\n"
          "    CTRL_NONE = 0, CTRL_BREAK = 1, CTRL_CONT = 2,\n"
          "    CTRL_RET  = 3, CTRL_ERR   = 4\n"
          "} CtrlSig;\n"
          "\n"
          "typedef enum {\n"
          "    TYPE_NIL = 0, TYPE_INT = 1, TYPE_FLOAT = 2,\n"
          "    TYPE_STR = 3, TYPE_ARR = 4, TYPE_FN    = 5,\n"
          "    TYPE_ERR = 6, TYPE_BOOL= 7, TYPE_REF   = 8,\n"
          "    TYPE_PTR = 9\n"
          "} ValueType;\n"
          "\n"
          "typedef struct {\n"
          "    ValueType type;\n"
          "    void *data;\n"
          "    long long ival;\n"
          "    double fval;\n"
          "    int err_code;\n"
          "    char *err_msg;\n"
          "} Value;\n"
          "\n"
          "typedef struct {\n"
          "    CtrlSig sig;\n"
          "    Value   ret_val;\n"
          "} EvalResult;\n"
          "\n"
          "typedef struct {\n"
          "    char  scope[256];\n"
          "    int   call_depth;\n"
          "    int   debug;\n"
          "    void *out;\n"
          "    void *err;\n"
          "} EvalCtx;\n"
          "\n"
          "void       eval_ctx_init(EvalCtx *ctx);\n"
          "EvalResult eval_str(EvalCtx *ctx, const char *src);\n"
          "void       val_print(const Value *v, void *out);\n"
          "void       val_free(Value *v);\n"
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
  fprintf(out,
          "/* Embedded dependency table — used by nexs_exec() at runtime */\n"
          "typedef struct { const char *path; const char *src; } NexsEmbedDep;\n"
          "static const NexsEmbedDep nexs_embed_deps_table[] = {\n");
  for (int i = 0; i < n_deps; i++) {
    fprintf(out, "  { \"%s\", nexs_dep_%d_src },\n", deps[i].path, i);
  }
  fprintf(out, "  { (void*)0, (void*)0 }  /* sentinel */\n};\n\n");

  fprintf(out,
          "int nexs_embed_dep_count = %d;\n\n", n_deps);

  /*
   * nexs_embedded_lookup: called by nexs_exec() / eval_file() before fopen().
   * Declared as a weak symbol so that the non-standalone interpreter build
   * (which provides its own empty version in sysproc.c) links cleanly.
   */
  fprintf(out,
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
          "}\n\n");
}

/* =========================================================
   nexs_codegen_ex
   ========================================================= */

int nexs_codegen_ex(const char *src_path, const char *out_c_path, int no_dep) {
  size_t src_len = 0;
  char *src = read_file(src_path, &src_len);
  if (!src) {
    fprintf(stderr, "nexs_codegen: cannot read '%s'\n", src_path);
    return -1;
  }

  FILE *out = fopen(out_c_path, "w");
  if (!out) {
    fprintf(stderr, "nexs_codegen: cannot write '%s'\n", out_c_path);
    free(src);
    return -1;
  }

  /* --- Generated file header --- */
  fprintf(out,
          "/*\n"
          " * Generated by NEXS codegen — do not edit.\n"
          " * Source: %s\n"
          " * Dependency bundling: %s\n"
          " */\n\n",
          src_path, no_dep ? "disabled (--no-dep)" : "enabled");

  /* --- Scan and embed dependencies (unless --no-dep) --- */
  NexsDepEntry *deps = NULL;
  int n_deps = 0;

  if (!no_dep) {
    deps = calloc((size_t)NEXS_MAX_DEPS, sizeof(NexsDepEntry));
    if (deps) {
      n_deps = nexs_scan_deps(src_path, deps, NEXS_MAX_DEPS);
      if (n_deps > 0) {
        fprintf(stdout, "[codegen] bundling %d dependenc%s\n",
                n_deps, n_deps == 1 ? "y" : "ies");
        for (int i = 0; i < n_deps; i++)
          fprintf(stdout, "  dep[%d] = %s%s\n", i, deps[i].path,
                  deps[i].src ? "" : "  (UNREADABLE — will be empty)");
      }
    }
  }

  /* --- Embedded script --- */
  fprintf(out, "static const char nexs_script_src[] =\n");
  write_c_string(out, src, src_len);
  fprintf(out, ";\n\n");
  free(src);

  /* --- Dep table (or empty stub) --- */
  if (!no_dep && deps && n_deps > 0) {
    emit_dep_table(out, deps, n_deps);
    nexs_free_deps(deps, n_deps);
    free(deps);
  } else {
    /* Emit an empty stub so sysproc.c can still call nexs_embedded_lookup */
    fprintf(out,
            "/* No dependencies bundled */\n"
            "const char *nexs_embedded_lookup(const char *path) {\n"
            "  (void)path; return (void*)0;\n"
            "}\n\n");
    if (deps) free(deps);
  }

  /* --- Forward declarations --- */
  emit_forward_decls(out);

  /* --- main() or nexs_main_baremetal() --- */
#ifdef NEXS_BAREMETAL
  fprintf(out,
          "#include <stddef.h>\n"
          "void nexs_hal_print(const char *s);\n"
          "\n"
          "void nexs_main_baremetal(void) {\n"
          "    nexs_runtime_init();\n"
          "    EvalCtx ctx;\n"
          "    eval_ctx_init(&ctx);\n"
          "    ctx.out = (void*)0; /* use nexs_hal_print */\n"
          "    EvalResult r = eval_str(&ctx, nexs_script_src);\n"
          "    (void)r;\n"
          "    while (1) {}\n"
          "}\n");
#else
  fprintf(out,
          "#include <stdio.h>\n"
          "#include <stdlib.h>\n"
          "\n"
          "int main(int argc, char *argv[]) {\n"
          "    (void)argc; (void)argv;\n"
          "    nexs_runtime_init();\n"
          "    EvalCtx ctx;\n"
          "    eval_ctx_init(&ctx);\n"
          "    EvalResult r = eval_str(&ctx, nexs_script_src);\n"
          "    if (r.sig == 4 /* CTRL_ERR */) {\n"
          "        fprintf(stderr, \"[ERR] \");\n"
          "        val_print(&r.ret_val, stderr);\n"
          "        fprintf(stderr, \"\\n\");\n"
          "        val_free(&r.ret_val);\n"
          "        return 1;\n"
          "    }\n"
          "    val_free(&r.ret_val);\n"
          "    return 0;\n"
          "}\n");
#endif

  fclose(out);
  return 0;
}

/* =========================================================
   nexs_codegen — backwards-compatible wrapper
   ========================================================= */

int nexs_codegen(const char *src_path, const char *out_c_path) {
  return nexs_codegen_ex(src_path, out_c_path, 0 /* bundle deps */);
}

/* =========================================================
   TARGET INFO
   ========================================================= */

#include "targets.h"

const char *target_name(CompileTarget target) {
  if (target < 0 || target >= TARGET_COUNT) return "unknown";
  return nexs_targets[target].name;
}

const char *target_gcc_flags(CompileTarget target) {
  static char buf[512];
  if (target < 0 || target >= TARGET_COUNT) return "";
  const TargetConfig *tc = &nexs_targets[target];
  snprintf(buf, sizeof(buf), "%s %s", tc->arch_flags, tc->os_flags);
  return buf;
}
