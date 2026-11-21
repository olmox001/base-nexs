/* Source I/O adapter: NXOLMO typed outcomes for hosted and freestanding.
 * This maps existing platform errno values, not resource permissions or
 * capability grants. A denied/read-fault path must never load a fallback. */
#ifndef NEXS_SOURCE_STATUS_H
#define NEXS_SOURCE_STATUS_H

#include "nexs_checked.h"
#include <errno.h>

static inline nx_status nexs_source_status_from_errno(int error) {
    if (error == ENOENT)
        return NX_STATUS_NOT_FOUND;
    if (error == ENOSYS)
        return NX_STATUS_NOT_SUPPORTED;
    if (error == EACCES)
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_IO_ERROR;
}

/* Embedded fallback is only possible after absent or unavailable file
 * providers; it never converts ACCESS_DENIED, IO_ERROR or corrupt input
 * into successfully executed embedded code. */
static inline int nexs_source_embedded_admissible(nx_status status) {
    return status == NX_STATUS_NOT_FOUND ||
           status == NX_STATUS_NOT_SUPPORTED;
}

#endif /* NEXS_SOURCE_STATUS_H */
