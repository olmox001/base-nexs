/*
 * compiler/dep_scan.c — NEXS Dependency Scanner
 * ================================================
 * Recursively scans a .nx source file for exec("path") calls and collects
 * all unique dependency paths needed for a self-contained standalone binary.
 *
 * Rules:
 *  - Parses lines looking for: exec("some/path.nx") or exec('some/path.nx')
 *  - Recursively processes each found dependency (depth-first).
 *  - Deduplicates: each resolved absolute path is included exactly once.
 *  - Preserves insertion order (first occurrence wins).
 *  - Resolves relative paths relative to the directory of the parent file.
 *  - Does NOT follow exec() calls whose argument is a variable (skips them
 *    gracefully — they cannot be statically resolved).
 */

#include "include/nexs_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================
   INTERNAL HELPERS
   ========================================================= */

/* Extract the directory part of path into buf (no trailing slash unless root) */
static void path_dirname(const char *path, char *buf, size_t bufsz) {
    strncpy(buf, path, bufsz - 1);
    buf[bufsz - 1] = '\0';
    char *slash = strrchr(buf, '/');
    if (slash && slash != buf) {
        *slash = '\0';
    } else if (slash == buf) {
        buf[1] = '\0'; /* root "/" */
    } else {
        strncpy(buf, ".", bufsz - 1); /* no slash → current dir */
    }
}

/* Join dir + "/" + file into buf */
static void path_join(const char *dir, const char *file, char *buf, size_t bufsz) {
    if (file[0] == '/') {
        /* Already absolute */
        strncpy(buf, file, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }
    if (strcmp(dir, ".") == 0) {
        strncpy(buf, file, bufsz - 1);
    } else {
        snprintf(buf, bufsz, "%s/%s", dir, file);
    }
}

/* Check whether path already exists in the dep list */
static int dep_already_seen(NexsDepEntry *deps, int count, const char *path) {
    for (int i = 0; i < count; i++) {
        if (strcmp(deps[i].path, path) == 0) return 1;
    }
    return 0;
}

/* Read an entire file into a malloc'd buffer; caller must free() it.
 * Returns NULL on error. */
static char *dep_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return calloc(1, 1); }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* =========================================================
   RECURSIVE SCANNER
   ========================================================= */

/*
 * Scan src (the content of a .nx file whose filesystem path is src_path)
 * for exec("...") / exec('...') calls.  For each found dependency, add it
 * to deps[] if not already present, then recurse into that file.
 *
 * Returns the new total count of deps.
 */
static int scan_source(const char *src_path, const char *src,
                        NexsDepEntry *deps, int count, int max_deps) {
    if (!src || !src_path) return count;

    char parent_dir[NEXS_DEP_PATH_MAX];
    path_dirname(src_path, parent_dir, sizeof(parent_dir));

    const char *p = src;
    while (*p) {
        /* Skip comments (# to end of line) */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        /*
         * Look for exec( without requiring whitespace.
         * Both keyword form  exec("path")
         * and fn-call form   exec("path")  are identical textually.
         */
        if (strncmp(p, "exec", 4) == 0) {
            const char *after_exec = p + 4;
            /* Skip optional whitespace between exec and ( */
            while (*after_exec == ' ' || *after_exec == '\t') after_exec++;
            if (*after_exec == '(') {
                after_exec++; /* skip ( */
                /* Skip whitespace */
                while (*after_exec == ' ' || *after_exec == '\t') after_exec++;
                /* Expect a string literal */
                char quote = *after_exec;
                if (quote == '"' || quote == '\'') {
                    after_exec++; /* skip opening quote */
                    const char *start = after_exec;
                    while (*after_exec && *after_exec != quote && *after_exec != '\n')
                        after_exec++;
                    if (*after_exec == quote) {
                        size_t len = (size_t)(after_exec - start);
                        if (len > 0 && len < NEXS_DEP_PATH_MAX - 1) {
                            char rel_path[NEXS_DEP_PATH_MAX];
                            char abs_path[NEXS_DEP_PATH_MAX];
                            strncpy(rel_path, start, len);
                            rel_path[len] = '\0';

                            /*
                             * The NEXS runtime resolves exec("path") relative to
                             * the current working directory, NOT the parent file's
                             * directory.  Use the literal path as the canonical key
                             * (CWD-relative).  If the file isn't found there, fall
                             * back to parent-dir-relative (for self-contained libs).
                             */
                            if (rel_path[0] == '/') {
                                strncpy(abs_path, rel_path, NEXS_DEP_PATH_MAX - 1);
                                abs_path[NEXS_DEP_PATH_MAX - 1] = '\0';
                            } else {
                                /* Try CWD-relative first */
                                strncpy(abs_path, rel_path, NEXS_DEP_PATH_MAX - 1);
                                abs_path[NEXS_DEP_PATH_MAX - 1] = '\0';
                                /* If not readable, try parent-dir-relative */
                                FILE *probe = fopen(abs_path, "r");
                                if (!probe) {
                                    char alt[NEXS_DEP_PATH_MAX];
                                    path_join(parent_dir, rel_path, alt, sizeof(alt));
                                    FILE *probe2 = fopen(alt, "r");
                                    if (probe2) {
                                        fclose(probe2);
                                        strncpy(abs_path, alt, NEXS_DEP_PATH_MAX - 1);
                                        abs_path[NEXS_DEP_PATH_MAX - 1] = '\0';
                                    }
                                } else {
                                    fclose(probe);
                                }
                            }

                            if (!dep_already_seen(deps, count, abs_path)) {
                                if (count < max_deps) {
                                    strncpy(deps[count].path, abs_path,
                                            NEXS_DEP_PATH_MAX - 1);
                                    deps[count].path[NEXS_DEP_PATH_MAX - 1] = '\0';
                                    deps[count].src = NULL; /* filled later */
                                    count++;

                                    /* Recurse into the dependency */
                                    char *dep_src = dep_read_file(abs_path);
                                    if (dep_src) {
                                        count = scan_source(abs_path, dep_src,
                                                            deps, count, max_deps);
                                        free(dep_src);
                                    }
                                    /* If file not found, we still record the path
                                     * so the user sees it in --dep-only output. */
                                }
                            }
                        }
                    }
                }
                p = after_exec;
                continue;
            }
        }
        p++;
    }
    return count;
}

/* =========================================================
   PUBLIC API
   ========================================================= */

/*
 * nexs_scan_deps — scan src_path recursively for exec() dependencies.
 *
 * Fills deps[0..count-1] in insertion order (depth-first).
 * Each entry's .src field is set to a malloc'd copy of the file contents,
 * or NULL if the file could not be read.
 * Caller must free each deps[i].src with free().
 *
 * Returns the number of unique dependencies found (may be 0).
 */
int nexs_scan_deps(const char *src_path,
                   NexsDepEntry *deps,
                   int max_deps) {
    if (!src_path || !deps || max_deps <= 0) return 0;

    char *src = dep_read_file(src_path);
    if (!src) return 0;

    int count = scan_source(src_path, src, deps, 0, max_deps);
    free(src);

    /* Fill in .src for each discovered dep */
    for (int i = 0; i < count; i++) {
        if (!deps[i].src) {
            deps[i].src = dep_read_file(deps[i].path);
            /* NULL if file not readable — codegen will warn */
        }
    }

    return count;
}

/*
 * nexs_free_deps — release the .src buffers allocated by nexs_scan_deps().
 */
void nexs_free_deps(NexsDepEntry *deps, int count) {
    if (!deps) return;
    for (int i = 0; i < count; i++) {
        if (deps[i].src) {
            free(deps[i].src);
            deps[i].src = NULL;
        }
    }
}
