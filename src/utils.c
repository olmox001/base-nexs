/*
 * utils.c — Utility Functions
 * ============================
 * Path manipulation, string trimming, buddy_strdup.
 */

#include "basereg.h"

#include <ctype.h>
#include <stdarg.h>
#include <string.h>

char *buddy_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = xmalloc(n);
  if (!d)
    return NULL;
  memcpy(d, s, n);
  return d;
}

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

void nexs_path_join(char *buf, size_t bufsz, ...) {
  if (!buf || bufsz == 0)
    return;
  va_list ap;
  va_start(ap, bufsz);
  buf[0] = '\0';
  const char *seg;
  while ((seg = va_arg(ap, const char *)) != NULL) {
    if (seg[0] == '/') {
      snprintf(buf, bufsz, "%s", seg);
    } else {
      size_t l = strlen(buf);
      if (l > 0 && buf[l - 1] != '/')
        strncat(buf, "/", bufsz - strlen(buf) - 1);
      strncat(buf, seg, bufsz - strlen(buf) - 1);
    }
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
