/*
 * nexs_utils.h — NEXS Utility Functions
 * ========================================
 * Path manipulation, string trimming, buddy_strdup.
 */

#ifndef NEXS_UTILS_H
#define NEXS_UTILS_H
#pragma once

#include "nexs_common.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trim leading/trailing whitespace in-place */
NEXS_API void        nexs_trim(char *s);

/* Join path segments (variadic, NULL-terminated list) into buf */
NEXS_API void        nexs_path_join(char *buf, size_t bufsz, ...);

/* Return pointer to last component of path (after final '/') */
NEXS_API const char *nexs_path_basename(const char *path);

/* Write directory part of path into out (up to outsz bytes) */
NEXS_API void        nexs_path_dirname(const char *path, char *out, size_t outsz);

/* Duplicate string into buddy pool — caller must xfree() result */
NEXS_API char       *buddy_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_UTILS_H */
