/* Product port of pinned NXOLMO external/references/nxolmo/include/nxolmo/protocol/9p/frame.h; single NX ABI. */
#ifndef NEXS_P9_FRAME_H
#define NEXS_P9_FRAME_H

#include "nexs_checked.h"
#include "nexs_checked.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_9P_FRAME_HEADER_SIZE 7u

typedef struct nx_9p_frame_view {
    uint32_t size;
    uint8_t type;
    uint16_t tag;
    const uint8_t *body;
    uint32_t body_size;
} nx_9p_frame_view;

/* The view borrows input storage and is valid only while that storage lives. */
/* Validate the 9P2000 request body before the server accesses any fields.
 * No session state, copies or allocation. A validated view borrows the wire. */
nx_status nx_9p_request_validate(const nx_9p_frame_view *view);

nx_status nx_9p_frame_decode(nx_const_span input,
                             uint32_t negotiated_msize,
                             nx_9p_frame_view *view,
                             size_t *consumed);

/* Body may overlap output; it is preserved before the frame header is written. */
nx_status nx_9p_frame_encode(nx_mut_span output,
                             uint8_t type,
                             uint16_t tag,
                             nx_const_span body,
                             uint32_t negotiated_msize,
                             size_t *written);

#ifdef __cplusplus
}
#endif

#endif
