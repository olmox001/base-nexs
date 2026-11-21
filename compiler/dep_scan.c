/*
 * compiler/dep_scan.c — NEXS Dependency Scanner
 * ================================================
 * Recursively scans a .nx source file for exec("path") calls and collects
 * all unique dependency paths needed for a self-contained standalone binary.
 *
 * Rules:
 *  - Parses lines looking for: exec("some/path.nx") or exec('some/path.nx')
 *  - Recursively processes each found dependency (depth-first).
 *  - Deduplicates each logical exec() path exactly once.
 *  - Preserves insertion order (first occurrence wins).
 *  - Uses the runtime's same package root provider for nxlib and examples,
 *    with parent-dir fallback for user-authored relative modules.
 *  - Does NOT follow exec() calls whose argument is a variable (skips them
 *    gracefully — they cannot be statically resolved).
 */

#include "include/nexs_compiler.h"
#include "../core/include/nexs_utils.h"
#include "../runtime/include/nexs_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* Freestanding libc implements ferror but its minimal stdio header does
 * not declare it. This is the standard C signature on every host. */
extern int ferror(FILE *stream);
#ifndef NEXS_BAREMETAL
#include <dirent.h>
#include <sys/stat.h>
#endif

/* =========================================================
   INTERNAL HELPERS
   ========================================================= */

/* Check whether path already exists in the dep list */
static int dep_already_seen(NexsDepEntry *deps, int count, const char *path) {
    for (int i = 0; i < count; i++) {
        if (strcmp(deps[i].path, path) == 0) return 1;
    }
    return 0;
}

/* Read an entire file into a malloc'd buffer; caller must free() it.
 * Returns NULL on error. */
static char *dep_read_file(const char *path, size_t *length) {
    char storage[NEXS_DEP_PATH_MAX];
    const char *resolved = nexs_platform_source_path(
        path, storage, sizeof(storage));
    if (length) *length = 0u;
    if (resolved == NULL) return NULL;
    FILE *f = fopen(resolved, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || (uintmax_t)size > SIZE_MAX - 1u ||
        fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t received = fread(buf, 1u, (size_t)size, f);
    int failed = received != (size_t)size || ferror(f) != 0;
    fclose(f);
    if (failed) { free(buf); return NULL; }
    buf[received] = 0;
    if (length) *length = received;
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
    nexs_path_dirname(src_path, parent_dir, sizeof(parent_dir));

    const char *p = src;
    while (*p) {
        /* Skip comments (# to end of line) */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        /*
         * Look for exec( or import( without requiring whitespace.
         * Both keyword form  exec("path") / import("path")
         * and fn-call form   exec("path") / import("path") are identical textually.
         */
        const char *keyword = NULL;
        if (strncmp(p, "exec", 4) == 0) keyword = "exec";
        else if (strncmp(p, "import", 6) == 0) keyword = "import";

        if (keyword) {
            size_t kw_len = strlen(keyword);
            const char *after_exec = p + kw_len;
            /* Skip optional whitespace between keyword and ( or " */
            while (*after_exec == ' ' || *after_exec == '\t') after_exec++;
            
            if (*after_exec == '(' || *after_exec == '"' || *after_exec == '\'') {
                int has_paren = (*after_exec == '(');
                if (has_paren) {
                    after_exec++;
                    while (*after_exec == ' ' || *after_exec == '\t') after_exec++;
                }

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

                            /* The compiled dependency key is always the
                             * logical exec() name. The same platform source
                             * provider used by eval_file and sysio resolves
                             * package-rooted nxlib/example files, preventing
                             * CWD-dependent loss and shadowed dependencies.
                             * Custom relative modules retain the parent-dir
                             * fallback when their canonical lookup fails. */
                            memcpy(abs_path, rel_path, len + 1U);
                            char resolved_storage[NEXS_DEP_PATH_MAX];
                            const char *resolved = nexs_platform_source_path(
                                rel_path, resolved_storage, sizeof(resolved_storage));
                            FILE *probe = resolved != NULL ? fopen(resolved, "r") : NULL;
                            if (probe == NULL && rel_path[0] != '/') {
                                char relative_path[NEXS_DEP_PATH_MAX];
                                nexs_path_join(relative_path,
                                               sizeof(relative_path),
                                               parent_dir, rel_path, NULL);
                                probe = fopen(relative_path, "r");
                                if (probe != NULL)
                                    snprintf(abs_path, sizeof(abs_path), "%s",
                                             relative_path);
                            }
                            if (probe != NULL)
                                fclose(probe);

                            if (!dep_already_seen(deps, count, abs_path)) {
                                if (count < max_deps) {
                                    strncpy(deps[count].path, abs_path,
                                            NEXS_DEP_PATH_MAX - 1);
                                    deps[count].path[NEXS_DEP_PATH_MAX - 1] = '\0';
                                    deps[count].src = NULL; /* filled later */
                                    count++;

                                    /* Recurse into the dependency */
                                    size_t nested_length = 0u;
                                    char *dep_src = dep_read_file(abs_path, &nested_length);
                                    if (dep_src) {
                                        size_t path_bytes = strlen(abs_path);
                                        if (path_bytes >= 3u &&
                                            strcmp(abs_path + path_bytes - 3u,
                                                   ".nx") == 0) {
                                            count = scan_source(abs_path, dep_src,
                                                                deps, count, max_deps);
                                        }
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

    size_t entry_length = 0u;
    char *src = dep_read_file(src_path, &entry_length);
    if (!src) return 0;

    int count = scan_source(src_path, src, deps, 0, max_deps);
    free(src);

    /* Fill in .src for each discovered dep */
    for (int i = 0; i < count; i++) {
        if (!deps[i].src) {
            deps[i].src = dep_read_file(deps[i].path, &deps[i].src_len);
            size_t name_size = strlen(deps[i].path);
            deps[i].is_source = name_size >= 3u &&
                strcmp(deps[i].path + name_size - 3u, ".nx") == 0;
            /* The compiler rejects unreadable deps rather than substituting
             * an empty script or data blob. */
        }
    }

    return count;
}

/*
 * nexs_free_deps — release the .src buffers allocated by nexs_scan_deps().
 */
void nexs_free_deps(NexsDepEntry *deps, int count) {
  if (!deps)
    return;
  for (int i = 0; i < count; i++) {
    if (deps[i].src) {
      free(deps[i].src);
      deps[i].src = NULL;
    }
  }
}

/*
 * nexs_scan_directory — recursively index all .nx files in a directory.
 */
#ifndef NEXS_BAREMETAL
int nexs_scan_directory(const char *dir_path, NexsDepEntry *deps,
                        int count, int max_deps) {
  if (dir_path == NULL || deps == NULL || count < 0 || max_deps <= 0)
    return -1;
  char root_storage[NEXS_DEP_PATH_MAX];
  const char *resolved = nexs_platform_source_path(
      dir_path, root_storage, sizeof(root_storage));
  if (resolved == NULL)
    return -1;
  DIR *directory = opendir(resolved);
  if (directory == NULL)
    return -1;

  struct dirent *entry;
  int error = 0;
  while ((entry = readdir(directory)) != NULL && error == 0) {
    if (entry->d_name[0] == '.')
      continue;
    /* All product names remain LOGICAL paths. Never embed the absolute
     * host install path or an alias from the current working directory. */
    char logical[NEXS_DEP_PATH_MAX];
    int length = snprintf(logical, sizeof(logical), "%s/%s",
                          dir_path, entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(logical)) {
      error = -1;
      continue;
    }
    char physical[NEXS_DEP_PATH_MAX];
    const char *mapped = nexs_platform_source_path(
        logical, physical, sizeof(physical));
    struct stat status;
    if (mapped == NULL || stat(mapped, &status) != 0) {
      error = -1;
      continue;
    }
    if (S_ISDIR(status.st_mode)) {
      count = nexs_scan_directory(logical, deps, count, max_deps);
      if (count < 0)
        error = -1;
    } else if (S_ISREG(status.st_mode)) {
      size_t name_length = strlen(entry->d_name);
      if (name_length >= 3U &&
          strcmp(entry->d_name + name_length - 3U, ".nx") == 0 &&
          !dep_already_seen(deps, count, logical)) {
        if (count >= max_deps) {
          error = -1;
          continue;
        }
        size_t content_length = 0u;
        char *contents = dep_read_file(logical, &content_length);
        if (contents == NULL) {
          error = -1;
          continue;
        }
        memcpy(deps[count].path, logical, (size_t)length + 1U);
        deps[count].src = contents;
        deps[count].src_len = content_length;
        deps[count].is_source = 1u;
        ++count;
      }
    }
  }
  closedir(directory);
  return error == 0 ? count : -1;
}
#else
int nexs_scan_directory(const char *dir_path, NexsDepEntry *deps, int count, int max_deps) {
  (void)dir_path; (void)deps; (void)max_deps;
  return count;
}
#endif
