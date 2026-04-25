/*
 * nexs_runtime.h — NEXS Runtime Entry Points
 * =============================================
 * nexs_runtime_init, nexs_repl, nexs_print_version.
 */

#ifndef NEXS_RUNTIME_H
#define NEXS_RUNTIME_H
#pragma once

#include "../../core/include/nexs_common.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

NEXS_API void nexs_runtime_init(void);
NEXS_API void nexs_repl(void);
NEXS_API void nexs_print_version(FILE *out);

#ifdef NEXS_BAREMETAL
/* Entry point called from HAL boot code */
void nexs_main_baremetal(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* NEXS_RUNTIME_H */
