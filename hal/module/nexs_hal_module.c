/*
 * hal/module/nexs_hal_module.c — HAL driver module registry
 */

#include "../include/nexs_hal_module.h"
#include "../../registry/include/nexs_registry.h"
#include "../../core/include/nexs_value.h"
#include "../../lang/include/nexs_fn.h"
#include <string.h>
#include <stdio.h>

static HalModule s_mods[HAL_MODULE_MAX];
static int       s_count = 0;

static const char *type_name(HalModuleType t) {
    switch (t) {
        case HAL_MOD_CHAR:  return "char";
        case HAL_MOD_BLK:   return "blk";
        case HAL_MOD_NET:   return "net";
        case HAL_MOD_TIMER: return "timer";
        case HAL_MOD_IRQ:   return "irq";
        case HAL_MOD_BUS:   return "bus";
        default:            return "unknown";
    }
}

void hal_module_register(const HalModule *mod) {
    if (!mod || s_count >= HAL_MODULE_MAX) return;
    s_mods[s_count] = *mod;
    s_count++;

    char path[128];
    snprintf(path, sizeof(path), "/hal/modules/%s/type", mod->name);
    reg_set(path, val_str(type_name(mod->type)), RK_READ);
    snprintf(path, sizeof(path), "/hal/modules/%s/state", mod->name);
    reg_set(path, val_str("registered"), RK_READ | RK_WRITE);
}

void hal_module_probe_all(HalModuleType type) {
    for (int i = 0; i < s_count; i++) {
        if (type != 0 && s_mods[i].type != type) continue;
        int ok = (!s_mods[i].probe || s_mods[i].probe() == 0);
        char path[128];
        snprintf(path, sizeof(path), "/hal/modules/%s/state", s_mods[i].name);
        if (ok && s_mods[i].init) {
            int rc = s_mods[i].init();
            reg_set(path, val_str(rc == 0 ? "active" : "error"), RK_READ | RK_WRITE);
        } else if (!ok) {
            reg_set(path, val_str("absent"), RK_READ | RK_WRITE);
        }
    }
}

const HalModule *hal_module_find(const char *name) {
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_mods[i].name, name) == 0) return &s_mods[i];
    return NULL;
}

int hal_module_count(void) { return s_count; }

/* =========================================================
   NEXS builtin: hal_module_list() → str  (newline-separated)
   ========================================================= */

#define SIG(s) s " \xe2\x86\x92 "

static Value bi_hal_module_list(Value *args, int n) {
    (void)args; (void)n;
    char buf[HAL_MODULE_MAX * (HAL_MODULE_NAME_MAX + 16)];
    int  off = 0;
    for (int i = 0; i < s_count; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/hal/modules/%s/state", s_mods[i].name);
        Value sv = reg_get(path);
        const char *state = (sv.type == TYPE_STR) ? (const char *)sv.data : "?";
        int written = snprintf(buf + off, sizeof(buf) - (size_t)off,
                               "%s [%s] %s\n",
                               s_mods[i].name, type_name(s_mods[i].type), state);
        val_free(&sv);
        if (written > 0) off += written;
    }
    if (off > 0 && buf[off - 1] == '\n') buf[off - 1] = '\0';
    return val_str(buf);
}

static Value bi_hal_module_probe(Value *args, int n) {
    (void)n;
    const char *name = (args && args[0].type == TYPE_STR) ? (const char *)args[0].data : "";
    const HalModule *m = hal_module_find(name);
    if (!m) return val_err(1, "module not found");
    int ok = (!m->probe || m->probe() == 0);
    if (ok && m->init) {
        int rc = m->init();
        char path[128];
        snprintf(path, sizeof(path), "/hal/modules/%s/state", m->name);
        reg_set(path, val_str(rc == 0 ? "active" : "error"), RK_READ | RK_WRITE);
        return val_int(rc);
    }
    return val_int(ok ? 0 : -1);
}

void hal_module_register_builtins(void) {
    fn_register_builtin_sig("hal_module_list",
        bi_hal_module_list,
        SIG("hal_module_list()") "str");
    fn_register_builtin_sig("hal_module_probe",
        bi_hal_module_probe,
        SIG("hal_module_probe(name str)") "int");
}
