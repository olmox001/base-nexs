/* Host build-tool execution is intentionally absent in OS1/freestanding.
 * Code generation and execution remain independent from external toolchains.
 * A future on-device compiler requires a capability-owned toolchain provider.
 */
#include "include/nexs_compiler.h"

int nexs_compile_file_ex(const char *source, CompileTarget target,
                         const char *output, int no_dependencies) {
  (void)source;
  (void)target;
  (void)output;
  (void)no_dependencies;
  return -1;
}

int nexs_compile_file(const char *source, CompileTarget target,
                      const char *output) {
  return nexs_compile_file_ex(source, target, output, 0);
}
