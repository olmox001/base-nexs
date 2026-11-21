/*
 * Hosted source catalog provider for the single NEXS runtime.
 *
 * The executable's canonical containing directory is the local resource
 * installation root. The process CWD remains untouched, and neither the
 * script path nor the OS1 VFS is impersonated as a capability.
 *
 * This hosted adapter is NOT compiled on baremetal or OS1: their existing
 * SDK/HAL provider resolves the same logical paths under the caller's
 * filesystem access rights.
 */
#define _XOPEN_SOURCE 700
#include "include/nexs_runtime.h"

#include "../core/include/nexs_alloc.h"
#include "../core/include/nexs_value.h"
#include "../lang/include/nexs_eval.h"
#include "../registry/include/nexs_registry.h"

#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Host process startup configuration; this must become instance-owned
 * when multiple runtimes share a process in the future. */
static char s_source_root[PATH_MAX];

void nexs_host_source_root_init(const char *executable) {
  s_source_root[0] = '\0';
  if (executable == NULL || executable[0] == '\0')
    return;
  char canonical[PATH_MAX];
  if (realpath(executable, canonical) == NULL)
    return;
  char *slash = strrchr(canonical, '/');
  if (slash == NULL)
    return;
  if (slash == canonical) {
    s_source_root[0] = '/';
    s_source_root[1] = '\0';
    return;
  }
  size_t count = (size_t)(slash - canonical);
  memcpy(s_source_root, canonical, count);
  s_source_root[count] = '\0';
}

static int nexs_catalog_resource(const char *path) {
  /* Only explicit logical roots, never a guessed basename or prefix. */
  static const char *const roots[] = {"nxlib", "example", "tests", "config"};
  for (size_t i = 0U; i < sizeof(roots) / sizeof(roots[0]); ++i) {
    size_t count = strlen(roots[i]);
    if (strncmp(path, roots[i], count) == 0 &&
        (path[count] == '\0' || path[count] == '/'))
      return 1;
  }
  return 0;
}

static int nexs_source_traverses_parent(const char *path) {
  if (path == NULL)
    return 1;
  for (size_t i = 0U; path[i] != '\0'; ++i) {
    if (path[i] == '.' && path[i + 1U] == '.' &&
        (i == 0U || path[i - 1U] == '/') &&
        (path[i + 2U] == '/' || path[i + 2U] == '\0'))
      return 1;
  }
  return 0;
}

const char *nexs_platform_source_path(const char *path,
                                       char *storage, size_t capacity) {
  if (path == NULL || storage == NULL || capacity == 0U)
    return NULL;
  if (path[0] == '/') {
    /* Explicit absolute file paths are still governed by OS file ACLs. */
    return path;
  }
  if (nexs_source_traverses_parent(path))
    return NULL;
  if (s_source_root[0] == '\0')
    return path;

  int written = snprintf(storage, capacity, "%s/%s", s_source_root, path);
  if (written < 0 || (size_t)written >= capacity)
    return NULL;

  /* Logical product resources are pinned to the selected runtime root.
   * Do not let a similarly named file in the caller's CWD hijack them. */
  if (nexs_catalog_resource(path))
    return storage;

  /* For user-authored scripts, preserve explicit relative CWD semantics.
   * Use the installed root when the local file is not present. */
  if (access(path, R_OK) == 0)
    return path;
  return storage;
}


/* Installed source files are admitted as actual NXOLMO registry objects,
 * from the existing reviewed source manifest. No directory recursion,
 * second FS index, shadow /vfs/files nodes or phantom SD mounts. */
static int nexs_host_admit_source(const char *logical) {
  char storage[REG_PATH_MAX];
  const char *file = nexs_platform_source_path(logical, storage,
                                                sizeof(storage));
  struct stat statbuf;
  if (file == NULL || lstat(file, &statbuf) != 0 ||
      !S_ISREG(statbuf.st_mode))
    return -1;
  FILE *input = fopen(file, "rb");
  if (input == NULL)
    return -1;
  int result = -1;
  if (fseek(input, 0, SEEK_END) == 0) {
    long length = ftell(input);
    if (length >= 0 && (uintmax_t)length < SIZE_MAX &&
        (uintmax_t)length < POOL_SIZE &&
        fseek(input, 0, SEEK_SET) == 0) {
      size_t bytes = (size_t)length;
      char *source = nexs_alloc(bytes + 1U);
      if (source != NULL) {
        size_t count = fread(source, 1U, bytes, input);
        source[count] = '\0';
        if (count == bytes && memchr(source, '\0', bytes) == NULL &&
            ferror(input) == 0)
          result = nexs_runtime_install_source(logical, source);
        nexs_free(source, bytes + 1U);
      }
    }
  }
  if (fclose(input) != 0)
    return -1;
  return result;
}

static int nexs_host_activate_source(const char *logical) {
  if (strncmp(logical, "nxlib/", 6U) != 0 ||
      strchr(logical + 6U, '/') != NULL)
    return 0;
  char path[REG_PATH_MAX];
  int count = snprintf(path, sizeof(path), "/%s", logical);
  if (count < 0 || (size_t)count >= sizeof(path))
    return -1;
  Value source = reg_get(path);
  int kind = source.type == TYPE_STR && source.data != NULL
      ? nexs_source_is_bootstrap_library(logical, source.data) : -1;
  val_free(&source);
  if (kind <= 0)
    return kind;
  EvalCtx ctx;
  eval_ctx_init(&ctx);
  EvalResult executed = eval_file(&ctx, logical);
  int failed = executed.sig == CTRL_ERR;
  val_free(&executed.ret_val);
  return failed ? -1 : 0;
}

static int nexs_host_process_line(char *line, int activate) {
  size_t size = strlen(line);
  if (size == 0U || (size == REG_PATH_MAX - 1U &&
                     line[size - 1U] != '\n'))
    return -1;
  while (size > 0U &&
         (line[size - 1U] == '\n' || line[size - 1U] == '\r'))
    line[--size] = '\0';
  if (size == 0U || line[0] == '#')
    return 0;
  if (activate != 0)
    return nexs_host_activate_source(line);
  return nexs_host_admit_source(line);
}

static int nexs_host_scan_catalog(FILE *catalog, int activate) {
  int eof = 0;
  for (size_t line = 0U; line < REG_PATH_MAX && eof == 0; ++line) {
    char text[REG_PATH_MAX];
    if (fgets(text, sizeof(text), catalog) != NULL) {
      if (nexs_host_process_line(text, activate) != 0)
        return -1;
    } else {
      eof = feof(catalog) != 0;
      if (eof == 0)
        return -1;
    }
  }
  return eof != 0 ? 0 : -1;
}

int nexs_platform_autoload(void) {
  char storage[REG_PATH_MAX];
  const char *catalog_file = nexs_platform_source_path(
      "config/nx_sources.txt", storage, sizeof(storage));
  struct stat statbuf;
  if (catalog_file == NULL || lstat(catalog_file, &statbuf) != 0 ||
      !S_ISREG(statbuf.st_mode))
    return -1;
  FILE *catalog = fopen(catalog_file, "rb");
  if (catalog == NULL)
    return -1;
  /* All installed resource identities are admitted BEFORE executing the
   * first pure library. Published source is read from the same registry. */
  int status = nexs_host_scan_catalog(catalog, 0);
  if (status == 0 && fseek(catalog, 0, SEEK_SET) == 0)
    status = nexs_host_scan_catalog(catalog, 1);
  else
    status = -1;
  if (fclose(catalog) != 0)
    return -1;
  return status;
}
