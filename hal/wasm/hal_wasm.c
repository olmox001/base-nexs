/*
 * hal/wasm/hal_wasm.c — HAL implementation for WebAssembly (Emscripten)
 * =======================================================================
 * Mirrors hal_hosted.c exactly (same HAL driver vtable, same MMU stubs,
 * same registration pattern) with two Emscripten-specific adjustments:
 *
 *   1. halt()  uses emscripten_force_exit(0) for clean WASM shutdown.
 *   2. putc()  flushes stdout on '\n' for browser/Node.js console visibility.
 *
 * REPL and main() are the same code as every other hosted target
 * (runtime/main.c, runtime/runtime.c) — NEXS_HOST_TOOL keeps them compiled in.
 *
 * JS-facing API (Emscripten only):
 *   nexs_wasm_init()          — initialise runtime (idempotent)
 *   nexs_wasm_eval(src)       — evaluate a NEXS source string
 *   nexs_wasm_version()       — return version string
 *
 * Guard: compiled only when -DNEXS_WASM is passed.
 * g_hal_driver is defined in hal/common/console.c — assign here, never define.
 */

#ifdef NEXS_WASM

#include "../include/hal_internal.h"
#include "../include/nexs_hal.h"
#include "../include/nexs_mmu.h"
#include "../../core/include/nexs_alloc.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* =========================================================
   HAL DRIVER IMPLEMENTATION
   ========================================================= */

static void wasm_hal_init(void) {}

static void wasm_hal_putc(char c) {
    fputc((unsigned char)c, stdout);
    if (c == '\n')
        fflush(stdout);
}

static int wasm_hal_getc(void) {
    int c = fgetc(stdin);
    return (c == EOF) ? -1 : c;
}

static void wasm_hal_memory_map(NexsMemMap *map) {
    if (map) {
        map->entry_point = 0;
        map->ram_base    = 0;
        map->ram_size    = 0;
        map->uart_base   = 0;
    }
}

static void wasm_hal_irq_disable(void) {}
static void wasm_hal_irq_enable(void)  {}

static void wasm_hal_halt(void) __attribute__((noreturn));
static void wasm_hal_halt(void) {
#ifdef __EMSCRIPTEN__
    emscripten_force_exit(0);
#else
    _Exit(0);
#endif
    __builtin_unreachable();
}

/* =========================================================
   MMU / MM stubs — browser sandbox owns address space
   ========================================================= */

void mmu_worker_sync(void) {}
void mmu_init(void) {}
int  mmu_switch_address_space(uint32_t pid) { (void)pid; return 0; }
void mmu_destroy_address_space(uint32_t pid) { (void)pid; }
paddr_t mmu_create_address_space(uint32_t pid) { (void)pid; return 0; }
void mmu_flush_tlb(vaddr_t virt) { (void)virt; }
void mmu_page_fault(vaddr_t fault_addr, uint64_t err) {
    (void)fault_addr; (void)err;
}
paddr_t mmu_virt_to_phys(vaddr_t virt) { return (paddr_t)virt; }

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
    (void)pid; (void)virt; (void)flags;
    return page_alloc(1) ? 0 : -1;
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
    (void)pid;
    page_free((void *)virt, 1);
    return 0;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys,
                 uint32_t pages, uint32_t flags) {
    (void)phys;
    for (uint32_t i = 0; i < pages; i++) {
        if (mm_alloc_page(pid, virt + i * 4096, flags) != 0)
            return -1;
    }
    return 0;
}

/* =========================================================
   DRIVER REGISTRATION
   ========================================================= */

static HalDriver s_wasm_driver = {
    .name        = "wasm-emscripten",
    .init        = wasm_hal_init,
    .putc        = wasm_hal_putc,
    .getc        = wasm_hal_getc,
    .halt        = wasm_hal_halt,
    .irq_disable = wasm_hal_irq_disable,
    .irq_enable  = wasm_hal_irq_enable,
    .memory_map  = wasm_hal_memory_map,
};

__attribute__((constructor))
static void wasm_register_hal(void) {
    g_hal_driver = &s_wasm_driver;
}

/* =========================================================
   JS-FACING API
   =========================================================
   Node.js REPL (same as Linux/macOS):
     node build/wasm/nexs.js           — interactive REPL via stdin
     echo "out 1+2" | node nexs.js     — pipe evaluation

   Browser / programmatic embedding:
     const NEXS = require('./nexs.js');
     NEXS().then(mod => {
       mod.ccall('nexs_wasm_init', null, [], []);
       mod.ccall('nexs_wasm_eval', 'number', ['string'], ['out 1+2\n']);
     });

   Note: nexs_wasm_eval() is independent of main() — it maintains its own
   persistent EvalCtx.  eval_str() sets nexs_g_eval_ctx on every call so
   the eval() builtin works correctly from within embedded scripts.
   ========================================================= */

#ifdef __EMSCRIPTEN__

#include "../../core/include/nexs_common.h"
#include "../../lang/include/nexs_eval.h"
#include "../../runtime/include/nexs_runtime.h"

static EvalCtx s_wasm_ctx;
static int     s_wasm_ready = 0;

EMSCRIPTEN_KEEPALIVE
void nexs_wasm_init(void) {
    if (s_wasm_ready)
        return;
    nexs_runtime_init();
    eval_ctx_init(&s_wasm_ctx);
    nexs_g_eval_ctx = &s_wasm_ctx;
    s_wasm_ready = 1;
}

EMSCRIPTEN_KEEPALIVE
int nexs_wasm_eval(const char *src) {
    if (!src)
        return -1;
    if (!s_wasm_ready)
        nexs_wasm_init();
    EvalResult r = eval_str(&s_wasm_ctx, src);
    int rc = (r.sig == CTRL_ERR) ? -1 : 0;
    val_free(&r.ret_val);
    return rc;
}

EMSCRIPTEN_KEEPALIVE
const char *nexs_wasm_version(void) {
    return NEXS_VERSION_STR;
}

#endif /* __EMSCRIPTEN__ */

#endif /* NEXS_WASM */
