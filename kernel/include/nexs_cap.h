/*
 * kernel/include/nexs_cap.h — Capability and CSpace Definitions
 * =============================================================
 * Fusion of Plan 9 (path-based naming) and seL4 (capability-based access).
 */

#ifndef NEXS_CAP_H
#define NEXS_CAP_H

#include <stdint.h>

typedef enum {
    CAP_NONE = 0,
    CAP_ENDPOINT,     /* Synchronous Rendezvous (seL4 style) */
    CAP_NOTIFICATION, /* Asynchronous Signaling */
    CAP_TCB,          /* Thread Control Block (Process management) */
    CAP_VSPACE,       /* Address Space management */
    CAP_CNODE,        /* Capability Table management */
    CAP_RESERVED
} CapType;

/* Capability Rights (Plan 9 style permissions mapped to Caps) */
#define CAP_READ    (1 << 0)
#define CAP_WRITE   (1 << 1)
#define CAP_EXEC    (1 << 2)
#define CAP_GRANT   (1 << 3) /* Permission to pass this cap to others */

typedef struct {
    CapType  type;
    uint32_t rights;
    uint32_t badge;     /* Opaque ID for the receiver to identify the sender */
    void    *obj;       /* Pointer to the kernel object (Endpoint, TCB, etc) */
} Cap;

/* CNode: A table of capabilities */
typedef struct {
    Cap     *caps;
    uint32_t size;
    uint32_t count;
} CNode;

/* This is a LOCAL, still-unprotected CSpace structural contract. True
 * authoritative issuance/revocation will be owned by NXOLMO/NXEC and
 * checked at protected access and commit, not a registry path or PID. */
struct NexsProc;
int cap_init(struct NexsProc *process, uint32_t capacity);
Cap *cap_lookup(uint32_t slot);
int cap_insert(struct NexsProc *process, uint32_t slot, Cap capability);
int cap_remove(struct NexsProc *process, uint32_t slot);
/* Exact provider-backed owner release, only after every live cap is retired. */
int cap_destroy(struct NexsProc *process);

#endif
