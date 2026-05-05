/*
 * kernel/cap.c — Capability Management
 * =====================================
 */

#include "include/nexs_cap.h"
#include "include/nexs_proc.h"
#include "../core/include/nexs_alloc.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"
#include <stdio.h>
#include <string.h>

/* cap_publish — exports a capability to the registry for diagnostic visibility */
static void cap_publish(NexsProc *p, uint32_t slot) {
    char path[REG_PATH_MAX];
    Cap *c = &p->cspace.caps[slot];
    if (c->type == CAP_NONE) return;

    snprintf(path, sizeof(path), "%s/caps/%u", p->reg_path, slot);
    const char *type_names[] = {"NONE", "ENDPOINT", "NOTIFICATION", "TCB", "VSPACE", "CNODE", "RESERVED"};
    char info[128];
    snprintf(info, sizeof(info), "type=%s rights=%u badge=%u", 
             type_names[c->type], c->rights, c->badge);
    reg_set(path, val_str(info), RK_READ);
}

/* cap_init — initializes a process's CSpace */
void cap_init(NexsProc *p, uint32_t size) {
    if (!p) return;
    p->cspace.size = size;
    p->cspace.count = 0;
    p->cspace.caps = xmalloc(sizeof(Cap) * size);
    memset(p->cspace.caps, 0, sizeof(Cap) * size);
}

/* cap_lookup — retrieves a capability by index from the current process */
Cap *cap_lookup(uint32_t slot) {
    NexsProc *curr = proc_current();
    if (!curr || slot >= curr->cspace.size) return NULL;
    Cap *c = &curr->cspace.caps[slot];
    if (c->type == CAP_NONE) return NULL;
    return c;
}

/* cap_insert — inserts a capability into a process's CSpace */
int cap_insert(NexsProc *p, uint32_t slot, Cap c) {
    if (!p || slot >= p->cspace.size) return -1;
    p->cspace.caps[slot] = c;
    p->cspace.count++;
    cap_publish(p, slot);
    return 0;
}

/* cap_remove — removes a capability */
void cap_remove(NexsProc *p, uint32_t slot) {
    if (!p || slot >= p->cspace.size) return;
    char path[REG_PATH_MAX];
    snprintf(path, sizeof(path), "%s/caps/%u", p->reg_path, slot);
    reg_set(path, val_nil(), 0); /* Delete from registry */
    p->cspace.caps[slot].type = CAP_NONE;
    p->cspace.count--;
}
