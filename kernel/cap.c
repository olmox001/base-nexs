/*
 * Kernel CSpace structural integrity prerequisite for NXOLMO authority.
 * This table is NOT a transferable, authenticated NXEC capability engine.
 * Paths are optional diagnostics only; they never issue or verify rights.
 */

#include "include/nexs_cap.h"
#include "include/nexs_proc.h"
#include "../core/include/nexs_alloc.h"
#include "../registry/include/nexs_registry.h"
#include "../core/include/nexs_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int cap_fields_valid(const Cap *capability) {
    if (capability == NULL)
        return 0;
    if (capability->type <= CAP_NONE || capability->type >= CAP_RESERVED)
        return 0;
    if (capability->obj == NULL || capability->rights == 0U ||
        (capability->rights & ~(uint32_t)(CAP_READ | CAP_WRITE |
                                          CAP_EXEC | CAP_GRANT)) != 0U)
        return 0;
    return 1;
}

/* Best-effort diagnostics only. Failure never grants, removes or changes
 * authority, and no unowned temporary Value may leak. */
static void cap_publish(const NexsProc *process, uint32_t slot) {
    if (process == NULL || process->cspace.caps == NULL ||
        slot >= process->cspace.size)
        return;
    const Cap *capability = &process->cspace.caps[slot];
    if (!cap_fields_valid(capability))
        return;
    const char *names[] = {
        "NONE", "ENDPOINT", "NOTIFICATION", "TCB", "VSPACE", "CNODE"
    };
    char path[REG_PATH_MAX];
    int path_size = snprintf(path, sizeof(path), "%s/caps/%u",
                             process->reg_path, slot);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return;
    char info[128];
    int info_size = snprintf(info, sizeof(info), "type=%s rights=%u badge=%u",
                             names[capability->type], capability->rights,
                             capability->badge);
    if (info_size < 0 || (size_t)info_size >= sizeof(info))
        return;
    Value diagnostic = val_str(info);
    if (diagnostic.type == TYPE_STR)
        (void)reg_set(path, diagnostic, RK_READ);
    val_free(&diagnostic);
}

/* Fail-closed: failed admission leaves all CSpace fields untouched.
 * Provider size, not a MAX_CAP_SLOTS catalog, is the actual limit. */
int cap_init(NexsProc *process, uint32_t capacity) {
    if (process == NULL || capacity == 0U ||
        process->cspace.caps != NULL || process->cspace.size != 0U ||
        process->cspace.count != 0U ||
        (size_t)capacity > SIZE_MAX / sizeof(Cap))
        return -1;
    size_t bytes = (size_t)capacity * sizeof(Cap);
    Cap *slots = nexs_alloc(bytes);
    if (slots == NULL)
        return -1;
    process->cspace.caps = slots;
    process->cspace.size = capacity;
    process->cspace.count = 0U;
    return 0;
}

Cap *cap_lookup(uint32_t slot) {
    NexsProc *current = proc_current();
    if (current == NULL || current->cspace.caps == NULL ||
        slot >= current->cspace.size)
        return NULL;
    Cap *capability = &current->cspace.caps[slot];
    return cap_fields_valid(capability) ? capability : NULL;
}

/* A live slot may not be overwritten: would leak an old capability and
 * inflate accounting. No registry diagnostic is an authorization source. */
int cap_insert(NexsProc *process, uint32_t slot, Cap capability) {
    if (process == NULL || process->cspace.caps == NULL ||
        slot >= process->cspace.size || !cap_fields_valid(&capability) ||
        process->cspace.count >= process->cspace.size ||
        process->cspace.caps[slot].type != CAP_NONE)
        return -1;
    process->cspace.caps[slot] = capability;
    ++process->cspace.count;
    cap_publish(process, slot);
    return 0;
}

/* Exact live-slot removal and zeroing; repeat/cross-slot denial does not
 * decrement count or leave a stale kernel object pointer. */
int cap_remove(NexsProc *process, uint32_t slot) {
    if (process == NULL || process->cspace.caps == NULL ||
        slot >= process->cspace.size ||
        !cap_fields_valid(&process->cspace.caps[slot]) ||
        process->cspace.count == 0U)
        return -1;
    memset(&process->cspace.caps[slot], 0, sizeof(Cap));
    --process->cspace.count;
    char path[REG_PATH_MAX];
    int path_size = snprintf(path, sizeof(path), "%s/caps/%u",
                             process->reg_path, slot);
    if (path_size >= 0 && (size_t)path_size < sizeof(path))
        (void)reg_delete(path);
    return 0;
}

/* A containing process must retire live slots before releasing its CSpace;
 * never silently orphan object references or permit double-free. */
int cap_destroy(NexsProc *process) {
    if (process == NULL || process->cspace.caps == NULL ||
        process->cspace.size == 0U || process->cspace.count != 0U)
        return -1;
    size_t bytes = (size_t)process->cspace.size * sizeof(Cap);
    nexs_free(process->cspace.caps, bytes);
    process->cspace.caps = NULL;
    process->cspace.size = 0U;
    process->cspace.count = 0U;
    return 0;
}
