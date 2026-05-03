/*
 * nexs_fd.h — Generic File Descriptor / Slot Allocator
 * ======================================================
 * A simple helper to find an empty slot in an array of structures.
 */

#ifndef NEXS_FD_H
#define NEXS_FD_H
#pragma once

#include <stdint.h>
#include <stdio.h>

#define NEXS_MAX_FDS 64

/* Unified File Descriptor */
typedef struct {
    int      in_use;
    uint64_t ino;
    int      flags;
    uint64_t pos;
    FILE    *fp;        /* Hosted mode pointer */
    const char *emb_src; /* Embedded source pointer */
    size_t      emb_pos; /* Embedded position */
    char     path[512];  /* Optional path for diagnostics */
} NexsFd;

/* 
 * NEXS_ALLOC_SLOT: Find the first index i in [start, max) where table[i].field == 0.
 * Returns the index, or -1 if no slot is available.
 */
#define NEXS_ALLOC_SLOT(table, max, start, field) ({ \
    int _found = -1; \
    for (int _i = (start); _i < (max); _i++) { \
        if (!(table)[_i].field) { _found = _i; break; } \
    } \
    _found; \
})

#endif /* NEXS_FD_H */
