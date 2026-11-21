/*
 * NEXS hosted AOT launcher.
 *
 * Compiler inputs are compiled to a private staging directory.  Make only
 * receives generated, trusted staging paths.  No user-controlled path is
 * interpolated into a shell command, no shell launcher is used and no
 * predictable PID-named /tmp file is shared with other compiler instances.
 *
 * All build source lists are owned by config/sources.mk, not duplicated here.
 * This translation unit is selected exclusively for hosted toolchain builds.
 */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#include "include/nexs_compiler.h"
#include "targets.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int nexs_dir_exists(const char *path) {
  struct stat status;
  return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static int nexs_make_directory(const char *path) {
  if (mkdir(path, 0700) == 0) return 0;
  if (errno == EEXIST && nexs_dir_exists(path)) return 0;
  return -1;
}

/* A user output path is processed as bytes, never passed to the build shell.
 * The loop grows only according to its actual checked input length. */
static int nexs_ensure_output_parent(const char *output) {
  char *copy = strdup(output);
  if (copy == NULL) return -1;
  int result = 0;
  size_t length = strlen(copy);
  for (size_t i = 1; i < length && result == 0; ++i) {
    if (copy[i] != '/') continue;
    copy[i] = '\0';
    if (nexs_make_directory(copy) != 0) result = -1;
    copy[i] = '/';
  }
  free(copy);
  return result;
}

static char *nexs_join(const char *first, const char *second) {
  size_t a = strlen(first);
  size_t b = strlen(second);
  if (a > SIZE_MAX - b - 1U) return NULL;
  char *buffer = malloc(a + b + 1U);
  if (buffer == NULL) return NULL;
  memcpy(buffer, first, a);
  memcpy(buffer + a, second, b + 1U);
  return buffer;
}

static char *nexs_assignment(const char *key, const char *value) {
  size_t a = strlen(key);
  size_t b = strlen(value);
  if (a > SIZE_MAX - b - 2U) return NULL;
  char *buffer = malloc(a + b + 2U);
  if (buffer == NULL) return NULL;
  memcpy(buffer, key, a);
  buffer[a] = '=';
  memcpy(buffer + a + 1U, value, b + 1U);
  return buffer;
}

/* Only the caller's selected historical resource profile reaches Make. */
static const char *nexs_pool_profile(void) {
  static const char *const allowed[] = {
      "4KB", "16KB", "32KB", "512KB", "4MB", "16MB", "64MB"};
  const char *requested = getenv("NEXS_POOL_PROFILE");
  if (requested == NULL || requested[0] == '\0') return "64MB";
  if (strncmp(requested, "POOL_", 5U) == 0) requested += 5U;
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
    if (strcmp(requested, allowed[i]) == 0) return allowed[i];
  }
  return NULL;
}

static const char *nexs_make_goal(CompileTarget target) {
  switch (target) {
  case TARGET_LINUX_AMD64:
  case TARGET_LINUX_ARM64:
  case TARGET_MACOS_AMD64:
  case TARGET_MACOS_ARM64:
    return "aot-hosted";
  case TARGET_OS1_AMD64:
    return "os1-aot-amd64";
  case TARGET_OS1_ARM64:
    return "os1-aot-arm64";
  case TARGET_BAREMETAL_AMD64:
    return "baremetal-amd64";
  case TARGET_BAREMETAL_ARM64:
    return "baremetal-arm64";
  default:
    return NULL;
  }
}

static const char *nexs_embed_key(CompileTarget target) {
  if (target == TARGET_OS1_AMD64 || target == TARGET_OS1_ARM64)
    return "OS1_AOT_EMBED_C";
  if (target == TARGET_BAREMETAL_AMD64 ||
      target == TARGET_BAREMETAL_ARM64)
    return "BAREMETAL_EMBED_C";
  return "AOT_EMBED_C";
}

static const char *nexs_output_key(CompileTarget target) {
  if (target == TARGET_OS1_AMD64 || target == TARGET_OS1_ARM64)
    return "OS1_AOT_OUTPUT";
  if (target == TARGET_BAREMETAL_AMD64 ||
      target == TARGET_BAREMETAL_ARM64)
    return "BAREMETAL_OUTPUT";
  return "AOT_OUTPUT";
}

/* Synchronously start the executable, not a shell. No command interpolation. */
static int nexs_run_make(char *const arguments[]) {
  pid_t child = 0;
  int result = posix_spawnp(&child, "make", NULL, NULL, arguments, environ);
  if (result != 0) {
    fprintf(stderr, "nexs: cannot launch make: %s\n", strerror(result));
    return -1;
  }
  for (unsigned int retry = 0U; retry < 32U; ++retry) {
    int status = 0;
    pid_t waited = waitpid(child, &status, 0);
    if (waited == child)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
    if (waited < 0 && errno != EINTR) return -1;
  }
  fprintf(stderr, "nexs: compiler wait interrupted repeatedly\n");
  return -1;
}

static int nexs_build_generated(CompileTarget target, const char *source,
                                const char *output, const char *profile) {
  char *embed = nexs_assignment(nexs_embed_key(target), source);
  char *dest = nexs_assignment(nexs_output_key(target), output);
  char *pool = nexs_assignment("POOL_PROFILE", profile);
  char *target_arg = nexs_assignment("AOT_HOST_TARGET",
                                      nexs_targets[target].name);
  const char *goal = nexs_make_goal(target);
  int result = -1;
  if (goal != NULL && embed != NULL && dest != NULL && pool != NULL &&
      target_arg != NULL) {
    char *const args[] = {
        "make", "--no-print-directory", embed, dest, pool, target_arg,
        (char *)goal, NULL};
    result = nexs_run_make(args);
  }
  free(embed);
  free(dest);
  free(pool);
  free(target_arg);
  return result;
}

static int nexs_copy_checked(int source, int target, off_t length) {
  unsigned char bytes[16384];
  off_t remaining = length;
  while (remaining > 0) {
    size_t request = remaining < (off_t)sizeof(bytes)
                         ? (size_t)remaining : sizeof(bytes);
    ssize_t count = read(source, bytes, request);
    if (count <= 0) return -1;
    size_t done = 0;
    while (done < (size_t)count) {
      ssize_t written = write(target, bytes + done, (size_t)count - done);
      if (written <= 0) return -1;
      done += (size_t)written;
    }
    remaining -= count;
  }
  return 0;
}

/* Cross-volume publication uses a private sibling, never a partial final
 * file. This preserves execution mode and replaces destination atomically. */
static int nexs_copy_cross_volume(const char *source, const char *output) {
  char *destination_temp = nexs_join(output, ".nexs-XXXXXX");
  if (destination_temp == NULL) return -1;
  int input = open(source, O_RDONLY);
  int output_fd = mkstemp(destination_temp);
  int result = -1;
  struct stat metadata;
  if (input >= 0 && output_fd >= 0 && fstat(input, &metadata) == 0 &&
      metadata.st_size > 0 && S_ISREG(metadata.st_mode) &&
      nexs_copy_checked(input, output_fd, metadata.st_size) == 0 &&
      fchmod(output_fd, metadata.st_mode & 0777U) == 0 &&
      fsync(output_fd) == 0) {
    result = 0;
  }
  if (input >= 0 && close(input) != 0) result = -1;
  if (output_fd >= 0 && close(output_fd) != 0) result = -1;
  if (result == 0 && rename(destination_temp, output) != 0) result = -1;
  if (result != 0) unlink(destination_temp);
  free(destination_temp);
  return result;
}

static int nexs_publish_binary(const char *source, const char *output) {
  struct stat metadata;
  if (stat(source, &metadata) != 0 || metadata.st_size <= 0 ||
      !S_ISREG(metadata.st_mode)) return -1;
  if (nexs_ensure_output_parent(output) != 0) return -1;
  if (rename(source, output) == 0) return 0;
  if (errno != EXDEV) return -1;
  return nexs_copy_cross_volume(source, output);
}

/* One isolated build, one generated program, one canonical Make source graph.
 * A compilation failure leaves any previous destination binary untouched. */
int nexs_compile_file_ex(const char *source, CompileTarget target,
                         const char *output, int no_dependencies) {
  if (source == NULL || source[0] == '\0' ||
      output == NULL || output[0] == '\0' ||
      (unsigned int)target >= TARGET_COUNT ||
      nexs_make_goal(target) == NULL) {
    fprintf(stderr, "nexs: unavailable target or invalid input\n");
    return -1;
  }
  const char *profile = nexs_pool_profile();
  if (profile == NULL || nexs_make_directory("build") != 0) {
    fprintf(stderr, "nexs: invalid resource profile or build directory\n");
    return -1;
  }
  char staging[] = "build/.nexs-aot-XXXXXX";
  if (mkdtemp(staging) == NULL) return -1;
  char *generated = nexs_join(staging, "/program.c");
  char *binary = nexs_join(staging, "/program.bin");
  char *object = nexs_join(staging, "/program.o");
  int rc = -1;
  if (generated != NULL && binary != NULL && object != NULL &&
      nexs_codegen_ex(source, generated, no_dependencies,
          target == TARGET_BAREMETAL_AMD64 ||
          target == TARGET_BAREMETAL_ARM64) == 0 &&
      nexs_build_generated(target, generated, binary, profile) == 0) {
    rc = nexs_publish_binary(binary, output);
  }
  if (generated != NULL) unlink(generated);
  if (binary != NULL) unlink(binary);
  if (object != NULL) unlink(object);
  if (rmdir(staging) != 0) rc = -1;
  free(generated);
  free(binary);
  free(object);
  if (rc != 0) fprintf(stderr, "nexs: target compilation failed\n");
  return rc;
}

int nexs_compile_file(const char *source, CompileTarget target,
                      const char *output) {
  return nexs_compile_file_ex(source, target, output, 0);
}
