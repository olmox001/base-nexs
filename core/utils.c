/*
 * core/utils.c — Utility Functions
 * ===================================
 * Path manipulation, string trimming.
 */

#include "include/nexs_alloc.h"
#include "include/nexs_common.h"
#include "include/nexs_utils.h"

#ifdef NEXS_BAREMETAL
#include "../hal/include/nexs_hal.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <string.h>

void nexs_trim(char *s) {
  if (!s)
    return;
  char *p = s;
  while (isspace((unsigned char)*p))
    p++;
  memmove(s, p, strlen(p) + 1);
  for (int i = (int)strlen(s) - 1; i >= 0 && isspace((unsigned char)s[i]); i--)
    s[i] = '\0';
}

/* One canonical RegKey/StateStore namespace. Empty path components
 * (e.g. ls with no argument) select the current node, not /sys/.
 * Absolute components select an absolute path; this helper is never
 * an authorization check. */
void nexs_path_join(char *buf, size_t bufsz, ...) {
  if (buf == NULL || bufsz == 0U)
    return;
  va_list ap;
  va_start(ap, bufsz);
  buf[0] = '\0';
  size_t length = 0U;
  const char *segment = NULL;
  for (size_t part = 0U;
       part < REG_PATH_MAX &&
       (segment = va_arg(ap, const char *)) != NULL; ++part) {
    if (segment[0] == '\0')
      continue;
    if (segment[0] == '/') {
      length = 0U;
      buf[0] = '\0';
    } else if (length > 0U && buf[length - 1U] != '/') {
      if (length + 1U >= bufsz) {
        buf[0] = '\0';
        va_end(ap);
        return;
      }
      buf[length++] = '/';
    }
    size_t remaining = bufsz - length;
    size_t copied = 0U;
    while (copied < remaining && segment[copied] != '\0') {
      if (copied + 1U >= remaining) {
        buf[0] = '\0';
        va_end(ap);
        return;
      }
      buf[length + copied] = segment[copied];
      copied += 1U;
    }
    length += copied;
    buf[length] = '\0';
  }
  va_end(ap);
}

const char *nexs_path_basename(const char *path) {
  if (!path)
    return "";
  const char *last = strrchr(path, '/');
  return last ? last + 1 : path;
}

void nexs_path_dirname(const char *path, char *out, size_t outsz) {
  if (!path || !out || outsz == 0)
    return;
  const char *last = strrchr(path, '/');
  if (!last || last == path) {
    strncpy(out, "/", outsz);
    out[outsz - 1] = '\0';
    return;
  }
  size_t len = (size_t)(last - path);
  if (len >= outsz)
    len = outsz - 1;
  memcpy(out, path, len);
  out[len] = '\0';
}

void nexs_fprintf(FILE *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (out) {
    vfprintf(out, fmt, ap);
  } else {
#ifdef NEXS_BAREMETAL
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    nexs_hal_print(buf);
#else
    /* Fallback for hosted if out is NULL, though it shouldn't happen there */
    vfprintf(stdout, fmt, ap);
#endif
  }
  va_end(ap);
}

int g_nexs_debug = 0;
