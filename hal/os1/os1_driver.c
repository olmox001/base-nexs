/* NexsOS1 native userland binding.
 *
 * Console and registry access use the SDK's process-scoped capability API.
 * The kernel HAL remains the sole owner of IRQs, physical MMU mappings and
 * hardware drivers; this library never impersonates those facilities.
 */
#include "../include/hal_internal.h"
#include "../include/nexs_hal.h"
#include "include/nexs_os1_platform.h"
#include "../../core/include/nexs_value.h"
#include "../../lang/include/nexs_fn.h"

#include <os1.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Absolute image paths are stable OS1 VFS contracts; file access still
 * uses the current process's SDK FILE capabilities and host ACL checks.
 * Never manufacture a registry capability or prepend privileged parents.
 */
struct nexs_os1_source_root {
  const char *logical;
  const char *installed;
};

static const struct nexs_os1_source_root os1_sources[] = {
  {"example", "/home/nxscript/examples"},
  {"examples", "/home/nxscript/examples"},
  {"nxlib", "/lib/nxscript/nxlib"},
  {"config", "/lib/nxscript/config"},
  {"include", "/lib/nxscript/include"}
};

const char *nexs_platform_source_path(
    const char *path, char *storage, size_t capacity) {
  if (path == NULL || storage == NULL || capacity == 0U) return NULL;
  if (path[0] == '/') return path;
  /* Reject traversals into other mount prefixes; the OS1 filesystem
   * permissions remain authoritative for every effective pathname.
   */
  for (size_t k = 0U; path[k] != '\0'; ++k) {
    if (path[k] == '.' && path[k + 1U] == '.' &&
        (k == 0U || path[k - 1U] == '/') &&
        (path[k + 2U] == '/' || path[k + 2U] == '\0'))
      return NULL;
  }
  for (size_t i = 0U; i < sizeof(os1_sources) / sizeof(os1_sources[0]); ++i) {
    size_t length = strlen(os1_sources[i].logical);
    if (strncmp(path, os1_sources[i].logical, length) != 0)
      continue;
    if (path[length] != '/' && path[length] != '\0') continue;
    int result = snprintf(storage, capacity, "%s%s",
                          os1_sources[i].installed, path + length);
    if (result < 0 || (size_t)result >= capacity) return NULL;
    return storage;
  }
  return path;
}

static void os1_hal_init(void) {
  nexs_os1_arch_fence();
}

static void os1_hal_write_char(char c) {
  const char byte = c;
  (void)write(STDOUT_FILENO, &byte, 1U);
}

static int os1_hal_read_char(void) {
  char byte = 0;
  long result = read(STDIN_FILENO, &byte, 1U);
  return result == 1 ? (int)(unsigned char)byte : -1;
}

static void os1_hal_exit(void) __attribute__((noreturn));
static void os1_hal_exit(void) {
  _Exit(0);
}

static void os1_forbidden_irq_request(void) {
  /* A user process cannot manage physical interrupts. */
  __builtin_trap();
}

static HalDriver s_os1_driver = {
  .name = "os1-capability-user",
  .init = os1_hal_init,
  .write_char = os1_hal_write_char,
  .read_char = os1_hal_read_char,
  .halt = os1_hal_exit,
  .irq_disable = os1_forbidden_irq_request,
  .irq_enable = os1_forbidden_irq_request,
  .memory_map = NULL,
};

static Value os1_registry_get_builtin(Value *args, int n) {
  if (args == NULL || n != 1 || args[0].type != TYPE_STR ||
      args[0].data == NULL)
    return val_err(4, "os1_registry_get requires a key string");

  char value[128] = {0};
  int result = OS1_registry_object_get((const char *)args[0].data, value,
                                       sizeof(value));
  if (result < 0) {
    /* The SDK owns NXPaX/notification severity classification. ENOENT and
     * other expected probes remain silent inside OS1_report_error. */
    OS1_report_error("nxscript.registry.get", result);
    return val_err(3, "OS1 registry key unavailable or access denied");
  }
  return val_str(value);
}

static Value os1_registry_set_builtin(Value *args, int n) {
  if (args == NULL || n != 2 || args[0].type != TYPE_STR ||
      args[1].type != TYPE_STR || args[0].data == NULL ||
      args[1].data == NULL)
    return val_err(4, "os1_registry_set requires key and value strings");

  int result = OS1_registry_object_set((const char *)args[0].data,
                                        (const char *)args[1].data);
  if (result < 0) {
    OS1_report_error("nxscript.registry.set", result);
    return val_err(3, "OS1 registry write unavailable or access denied");
  }
  return val_int(0);
}

void nexs_platform_register(void) {
  g_hal_driver = &s_os1_driver;
  (void)fn_register_builtin_sig("os1_registry_get",
      os1_registry_get_builtin, "os1_registry_get(key str) -> str");
  (void)fn_register_builtin_sig("os1_registry_set",
      os1_registry_set_builtin, "os1_registry_set(key str, value str) -> int");
}
