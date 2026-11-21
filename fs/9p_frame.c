/* Product port of pinned NXOLMO external/references/nxolmo/src/protocol/9p/frame.c; no reference linkage. */
#include "nexs_p9_frame.h"
#include "nexs_fs9p.h"

#include "nexs_checked.h"
#include "nexs_endian.h"

#include <string.h>


nx_status nx_9p_frame_decode(nx_const_span input,
                             uint32_t negotiated_msize,
                             nx_9p_frame_view *view,
                             size_t *consumed) {
    uint32_t frame_size;

    if (view != NULL)
        memset(view, 0, sizeof(*view));
    if (consumed != NULL)
        *consumed = 0u;
    if (view == NULL || consumed == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (input.data == NULL && input.size != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (negotiated_msize < NX_9P_FRAME_HEADER_SIZE)
        return NX_STATUS_INVALID_ARGUMENT;
    if (input.size < NX_9P_FRAME_HEADER_SIZE)
        return NX_STATUS_TRUNCATED;

    frame_size = nx_load_le32(input.data);
    if (frame_size < NX_9P_FRAME_HEADER_SIZE)
        return NX_STATUS_MALFORMED;
    if (frame_size > negotiated_msize)
        return NX_STATUS_LIMIT_EXCEEDED;
    if ((uint64_t)frame_size > (uint64_t)input.size)
        return NX_STATUS_TRUNCATED;

    view->size = frame_size;
    view->type = input.data[4];
    view->tag = nx_load_le16(input.data + 5u);
    view->body = input.data + NX_9P_FRAME_HEADER_SIZE;
    view->body_size = frame_size - NX_9P_FRAME_HEADER_SIZE;
    *consumed = (size_t)frame_size;
    return NX_STATUS_OK;
}

nx_status nx_9p_frame_encode(nx_mut_span output,
                             uint8_t type,
                             uint16_t tag,
                             nx_const_span body,
                             uint32_t negotiated_msize,
                             size_t *written) {
    size_t total_size = 0u;
    nx_status status;

    if (written != NULL)
        *written = 0u;
    if (written == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (output.data == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (body.size != 0u && body.data == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (negotiated_msize < NX_9P_FRAME_HEADER_SIZE)
        return NX_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)body.size >
        (uint64_t)UINT32_MAX - (uint64_t)NX_9P_FRAME_HEADER_SIZE)
        return NX_STATUS_OVERFLOW;

    status = nx_size_add((size_t)NX_9P_FRAME_HEADER_SIZE, body.size,
                         &total_size);
    if (status != NX_STATUS_OK)
        return status;
    if ((uint64_t)total_size > (uint64_t)negotiated_msize)
        return NX_STATUS_LIMIT_EXCEEDED;
    if (total_size > output.size)
        return NX_STATUS_NO_SPACE;

    /* Move the body first so aliases into the header cannot be overwritten. */
    if (body.size != 0u) {
        if (body.data == NULL)
            return NX_STATUS_INVALID_ARGUMENT;
        memmove(output.data + NX_9P_FRAME_HEADER_SIZE, body.data, body.size);
    }

    nx_store_le32(output.data, (uint32_t)total_size);
    output.data[4] = type;
    nx_store_le16(output.data + 5u, tag);
    *written = total_size;
    return NX_STATUS_OK;
}

/* Read an untrusted length-prefixed 9P string without exposing a raw pointer
 * outside the validated frame body. Offsets never exceed body_size. */
static nx_status nx_p9_skip_string(const nx_9p_frame_view *view,
                                   size_t *cursor) {
    uint16_t length;
    if (*cursor > view->body_size)
        return NX_STATUS_MALFORMED;
    if ((size_t)view->body_size - *cursor < 2u)
        return NX_STATUS_TRUNCATED;
    length = nx_load_le16(view->body + *cursor);
    *cursor += 2u;
    if ((size_t)length > (size_t)view->body_size - *cursor)
        return NX_STATUS_TRUNCATED;
    *cursor += (size_t)length;
    return NX_STATUS_OK;
}

static nx_status nx_p9_validate_version(const nx_9p_frame_view *view) {
    size_t cursor = 4u;
    nx_status status;
    if (view->body_size < cursor)
        return NX_STATUS_TRUNCATED;
    status = nx_p9_skip_string(view, &cursor);
    if (status != NX_STATUS_OK)
        return status;
    return cursor == view->body_size ? NX_STATUS_OK : NX_STATUS_MALFORMED;
}

static nx_status nx_p9_validate_attach(const nx_9p_frame_view *view) {
    size_t cursor = 8u;
    nx_status status;
    if (view->body_size < cursor)
        return NX_STATUS_TRUNCATED;
    status = nx_p9_skip_string(view, &cursor);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_p9_skip_string(view, &cursor);
    if (status != NX_STATUS_OK)
        return status;
    return cursor == view->body_size ? NX_STATUS_OK : NX_STATUS_MALFORMED;
}

static nx_status nx_p9_validate_walk(const nx_9p_frame_view *view) {
    size_t cursor = 10u;
    uint16_t count;
    uint16_t i;
    if (view->body_size < cursor)
        return NX_STATUS_TRUNCATED;
    count = nx_load_le16(view->body + 8u);
    for (i = 0u; i < count; ++i) {
        nx_status status = nx_p9_skip_string(view, &cursor);
        if (status != NX_STATUS_OK)
            return status;
    }
    return cursor == view->body_size ? NX_STATUS_OK : NX_STATUS_MALFORMED;
}

static nx_status nx_p9_validate_write(const nx_9p_frame_view *view) {
    uint32_t length;
    if (view->body_size < 16u)
        return NX_STATUS_TRUNCATED;
    length = nx_load_le32(view->body + 12u);
    if ((uint64_t)length > (uint64_t)view->body_size - 16u)
        return NX_STATUS_TRUNCATED;
    return (uint64_t)length == (uint64_t)view->body_size - 16u
               ? NX_STATUS_OK : NX_STATUS_MALFORMED;
}

nx_status nx_9p_request_validate(const nx_9p_frame_view *view) {
    if (view == NULL || view->body == NULL ||
        view->size < NX_9P_FRAME_HEADER_SIZE ||
        (uint64_t)view->body_size + NX_9P_FRAME_HEADER_SIZE != view->size)
        return NX_STATUS_INVALID_ARGUMENT;
    switch (view->type) {
    case P9_TVERSION:
        return nx_p9_validate_version(view);
    case P9_TATTACH:
        return nx_p9_validate_attach(view);
    case P9_TWALK:
        return nx_p9_validate_walk(view);
    case P9_TOPEN:
        return view->body_size == 5u ? NX_STATUS_OK
             : view->body_size < 5u ? NX_STATUS_TRUNCATED : NX_STATUS_MALFORMED;
    case P9_TREAD:
        return view->body_size == 16u ? NX_STATUS_OK
             : view->body_size < 16u ? NX_STATUS_TRUNCATED : NX_STATUS_MALFORMED;
    case P9_TWRITE:
        return nx_p9_validate_write(view);
    case P9_TCLUNK:
    case P9_TSTAT:
        return view->body_size == 4u ? NX_STATUS_OK
             : view->body_size < 4u ? NX_STATUS_TRUNCATED : NX_STATUS_MALFORMED;
    default:
        return NX_STATUS_NOT_SUPPORTED;
    }
}
