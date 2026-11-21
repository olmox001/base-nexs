/* NXOLMO block source adapted into Base-NEXS product ABI.
 * No implicit locks, threads, pointers across domains or hardware authority.
 * This backend has one owner; caller serialized transitions are mandatory. */
#include "nexs_block_memory.h"
#include "nexs_checked.h"
#include <stddef.h>
#include <string.h>
#define NX_BLOCK_SIZE_MIN UINT32_C(512)
static int overlaps_device_storage(const nx_block_memory *, const void *,
                                   size_t);
static uint32_t ring_index(uint32_t head, uint32_t offset, uint32_t capacity) {
  return offset >= capacity - head ? offset - (capacity - head) : head + offset;
}
static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn) {
  uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
  if (an == 0u || bn == 0u)
    return 0;
  if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn)
    return -1;
  return x < y + bn && y < x + an ? 1 : 0;
}
static nx_status validate_request_storage(const nx_block_memory *d,
                                          size_t *request_bytes,
                                          size_t *completion_bytes,
                                          uint64_t *block_size) {
    if (d->volatile_bytes == NULL || d->durable_bytes == NULL ||
        d->requests == NULL || d->completions == NULL || d->block_count == 0u ||
        d->byte_count == 0u || d->byte_count > (uint64_t)SIZE_MAX ||
        d->byte_count % d->block_count != 0u || d->request_capacity == 0u ||
        d->completion_capacity == 0u || d->request_head >= d->request_capacity ||
        d->completion_head >= d->completion_capacity ||
        d->request_count > d->request_capacity ||
        d->completion_count > d->completion_capacity ||
        d->next_request_id == 0u || d->fail_status == NX_STATUS_OK)
        return NX_STATUS_CORRUPT;
    *block_size = d->byte_count / d->block_count;
    if (*block_size < NX_BLOCK_SIZE_MIN || *block_size > UINT32_MAX ||
        (*block_size & (*block_size - 1u)) != 0u ||
        nx_size_mul((size_t)d->request_capacity, sizeof(*d->requests),
                    request_bytes) != NX_STATUS_OK ||
        nx_size_mul((size_t)d->completion_capacity, sizeof(*d->completions),
                    completion_bytes) != NX_STATUS_OK)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status validate_request_aliases(const nx_block_memory *d,
                                          size_t request_bytes,
                                          size_t completion_bytes) {
    if (ranges_overlap(d->volatile_bytes, (size_t)d->byte_count, d->durable_bytes,
                       (size_t)d->byte_count) != 0 ||
        ranges_overlap(d, sizeof(*d), d->volatile_bytes, (size_t)d->byte_count) != 0 ||
        ranges_overlap(d, sizeof(*d), d->durable_bytes, (size_t)d->byte_count) != 0 ||
        ranges_overlap(d, sizeof(*d), d->requests, request_bytes) != 0 ||
        ranges_overlap(d, sizeof(*d), d->completions, completion_bytes) != 0 ||
        ranges_overlap(d->requests, request_bytes, d->completions,
                       completion_bytes) != 0 ||
        ranges_overlap(d->requests, request_bytes, d->volatile_bytes,
                       (size_t)d->byte_count) != 0 ||
        ranges_overlap(d->requests, request_bytes, d->durable_bytes,
                       (size_t)d->byte_count) != 0 ||
        ranges_overlap(d->completions, completion_bytes, d->volatile_bytes,
                       (size_t)d->byte_count) != 0 ||
        ranges_overlap(d->completions, completion_bytes, d->durable_bytes,
                       (size_t)d->byte_count) != 0)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

static nx_status validate_queued_request(const nx_block_memory *d,
                                         const nx_block_request *q,
                                         uint64_t block_size) {
    uint64_t end = 0u;
    uint64_t bytes = 0u;
    if (q->request_id == 0u || q->request_id >= d->next_request_id ||
        q->reserved != 0u)
        return NX_STATUS_CORRUPT;
    if (q->operation == (uint32_t)NX_BLOCK_OP_FLUSH)
        return q->first_block == 0u && q->block_count == 0u && q->buffer == NULL
                   ? NX_STATUS_OK : NX_STATUS_CORRUPT;
    if (q->operation != (uint32_t)NX_BLOCK_OP_READ &&
        q->operation != (uint32_t)NX_BLOCK_OP_WRITE)
        return NX_STATUS_CORRUPT;
    if (q->block_count == 0u ||
        q->buffer == NULL ||
        nx_u64_add(q->first_block, (uint64_t)q->block_count, &end) != NX_STATUS_OK ||
        end > d->block_count ||
        nx_u64_mul((uint64_t)q->block_count, block_size, &bytes) != NX_STATUS_OK ||
        bytes > (uint64_t)SIZE_MAX ||
        overlaps_device_storage(d, q->buffer, (size_t)bytes) != 0)
        return NX_STATUS_CORRUPT;
    return NX_STATUS_OK;
}

nx_status nx_block_memory_validate(const nx_block_memory *d) {
    size_t request_bytes = 0u;
    size_t completion_bytes = 0u;
    uint64_t block_size = 0u;
    uint32_t i;
    nx_status status;
    if (d == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    if (d->initialized != 1u)
        return NX_STATUS_UNAVAILABLE;
    status = validate_request_storage(d, &request_bytes, &completion_bytes,
                                      &block_size);
    if (status != NX_STATUS_OK)
        return status;
    status = validate_request_aliases(d, request_bytes, completion_bytes);
    if (status != NX_STATUS_OK)
        return status;
    for (i = 0u; i < d->request_count; ++i) {
        uint32_t slot = ring_index(d->request_head, i, d->request_capacity);
        status = validate_queued_request(d, &d->requests[slot], block_size);
        if (status != NX_STATUS_OK)
            return status;
    }
    return NX_STATUS_OK;
}

static nx_status ready(const nx_block_memory *d) {
  return nx_block_memory_validate(d);
}
static int overlaps_device_storage(const nx_block_memory *d, const void *p,
                                   size_t n) {
  size_t rb = (size_t)d->request_capacity * sizeof(*d->requests);
  size_t cb = (size_t)d->completion_capacity * sizeof(*d->completions);
  if (ranges_overlap(p, n, d, sizeof(*d)) != 0 ||
      ranges_overlap(p, n, d->volatile_bytes, (size_t)d->byte_count) != 0 ||
      ranges_overlap(p, n, d->durable_bytes, (size_t)d->byte_count) != 0 ||
      ranges_overlap(p, n, d->requests, rb) != 0 ||
      ranges_overlap(p, n, d->completions, cb) != 0)
    return 1;
  return 0;
}
nx_status nx_block_memory_init(nx_block_memory *d, uint8_t *v, uint8_t *p,
                               uint64_t blocks, uint32_t size,
                               nx_block_request *rq, uint32_t rc,
                               nx_block_completion *cq, uint32_t cc) {
  uint64_t bytes = 0u;
  size_t rb = 0u, cb = 0u;
  if (d == NULL || v == NULL || p == NULL || rq == NULL || cq == NULL ||
      blocks == 0u || rc == 0u || cc == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (size < NX_BLOCK_SIZE_MIN ||
      (size & (size - 1u)) != 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  if (nx_u64_mul(blocks, (uint64_t)size, &bytes) != NX_STATUS_OK ||
      bytes > (uint64_t)SIZE_MAX)
    return NX_STATUS_OVERFLOW;
  if (nx_size_mul((size_t)rc, sizeof(*rq), &rb) != NX_STATUS_OK ||
      nx_size_mul((size_t)cc, sizeof(*cq), &cb) != NX_STATUS_OK)
    return NX_STATUS_OVERFLOW;
  if (ranges_overlap(v, (size_t)bytes, p, (size_t)bytes) != 0 ||
      ranges_overlap(d, sizeof(*d), v, (size_t)bytes) != 0 ||
      ranges_overlap(d, sizeof(*d), p, (size_t)bytes) != 0 ||
      ranges_overlap(d, sizeof(*d), rq, rb) != 0 ||
      ranges_overlap(d, sizeof(*d), cq, cb) != 0 ||
      ranges_overlap(rq, rb, cq, cb) != 0 ||
      ranges_overlap(rq, rb, v, (size_t)bytes) != 0 ||
      ranges_overlap(rq, rb, p, (size_t)bytes) != 0 ||
      ranges_overlap(cq, cb, v, (size_t)bytes) != 0 ||
      ranges_overlap(cq, cb, p, (size_t)bytes) != 0)
    return NX_STATUS_INVALID_ARGUMENT;
  memset(d, 0, sizeof(*d));
  memset(v, 0, (size_t)bytes);
  memset(p, 0, (size_t)bytes);
  memset(rq, 0, rb);
  memset(cq, 0, cb);
  d->volatile_bytes = v;
  d->durable_bytes = p;
  d->block_count = blocks;
  d->byte_count = bytes;
  d->next_request_id = 1u;
  d->requests = rq;
  d->request_capacity = rc;
  d->completions = cq;
  d->completion_capacity = cc;
  d->fail_status = NX_STATUS_IO_ERROR;
  d->initialized = 1u;
  return NX_STATUS_OK;
}
static nx_status validate_submit_request(
    nx_block_memory *d, nx_block_operation op, uint64_t first,
    uint32_t count, void *buffer, uint64_t *id_out, size_t *transfer_size) {
    uint64_t end = 0u;
    uint64_t transfer = 0u;
    uint64_t block_size;
    if (id_out == NULL ||
        ((uintptr_t)id_out % (uintptr_t)_Alignof(uint64_t)) != 0u ||
        overlaps_device_storage(d, id_out, sizeof(*id_out)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    if (op != NX_BLOCK_OP_READ && op != NX_BLOCK_OP_WRITE &&
        op != NX_BLOCK_OP_FLUSH)
        return NX_STATUS_INVALID_ARGUMENT;
    if (op == NX_BLOCK_OP_FLUSH)
        return first == 0u && count == 0u && buffer == NULL
                   ? NX_STATUS_OK : NX_STATUS_INVALID_ARGUMENT;
    if (count == 0u ||
        buffer == NULL ||
        nx_u64_add(first, (uint64_t)count, &end) != NX_STATUS_OK ||
        end > d->block_count)
        return NX_STATUS_INVALID_ARGUMENT;
    block_size = d->byte_count / d->block_count;
    if (nx_u64_mul((uint64_t)count, block_size, &transfer) != NX_STATUS_OK ||
        transfer > (uint64_t)SIZE_MAX)
        return NX_STATUS_OVERFLOW;
    *transfer_size = (size_t)transfer;
    if (overlaps_device_storage(d, buffer, *transfer_size) != 0 ||
        ranges_overlap(buffer, *transfer_size, id_out, sizeof(*id_out)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

nx_status nx_block_memory_submit(nx_block_memory *d, nx_block_operation op,
                                 uint64_t first, uint32_t count, void *buf,
                                 uint64_t *id_out) {
  nx_block_request q;
  size_t transfer_size = 0u;
  uint32_t tail;
  nx_status status = ready(d);
  if (status != NX_STATUS_OK)
    return status;
  status = validate_submit_request(d, op, first, count, buf, id_out,
                                   &transfer_size);
  if (status != NX_STATUS_OK)
    return status;
  (void)transfer_size;
  if (d->request_count == d->request_capacity)
    return NX_STATUS_NO_SPACE;
  if (d->next_request_id == 0u || d->next_request_id == UINT64_MAX)
    return NX_STATUS_RESOURCE_EXHAUSTED;
  q.request_id = d->next_request_id++;
  q.first_block = first;
  q.block_count = count;
  q.operation = (uint32_t)op;
  q.buffer = buf;
  q.reserved = 0u;
  tail = ring_index(d->request_head, d->request_count, d->request_capacity);
  d->requests[tail] = q;
  d->request_count++;
  *id_out = q.request_id;
  return NX_STATUS_OK;
}

nx_status nx_block_memory_pump(nx_block_memory *d, uint32_t limit,
                               uint32_t *processed) {
  uint32_t done = 0u;
  nx_status s = ready(d);
  if (s != NX_STATUS_OK)
    return s;
  if (processed == NULL || limit == 0u)
    return NX_STATUS_INVALID_ARGUMENT;
  *processed = 0u;
  while (done < limit && d->request_count != 0u &&
         d->completion_count < d->completion_capacity) {
    nx_block_request q;
    nx_block_completion c;
    uint64_t off = 0u, n = 0u;
    uint32_t tail = 0u;

    q = d->requests[d->request_head];
    memset(&c, 0, sizeof(c));
    c.request_id = q.request_id;
    c.operation = q.operation;
    c.status = NX_STATUS_OK;
    if (d->fail_next_count != 0u) {
      d->fail_next_count--;
      c.status = d->fail_status;
    } else if (q.operation == (uint32_t)NX_BLOCK_OP_FLUSH) {
      memcpy(d->durable_bytes, d->volatile_bytes, (size_t)d->byte_count);
      c.completed_bytes = d->byte_count;
    } else {
      uint64_t bs = d->byte_count / d->block_count;
      off = q.first_block * bs;
      n = (uint64_t)q.block_count * bs;
      if (q.operation == (uint32_t)NX_BLOCK_OP_WRITE)
        memcpy(d->volatile_bytes + (size_t)off, q.buffer, (size_t)n);
      else
        memcpy(q.buffer, d->volatile_bytes + (size_t)off, (size_t)n);
      c.completed_bytes = n;
    }
    d->request_head = (d->request_head + 1u) % d->request_capacity;
    d->request_count--;
    tail = ring_index(d->completion_head, d->completion_count,
                      d->completion_capacity);
    d->completions[tail] = c;
    d->completion_count++;
    done++;
  }
  *processed = done;
  return done == 0u && d->request_count != 0u ? NX_STATUS_NO_SPACE
                                              : NX_STATUS_OK;
}
nx_status nx_block_memory_poll(nx_block_memory *d, nx_block_completion *out) {
  nx_status s = ready(d);
  if (s != NX_STATUS_OK)
    return s;
  if (out == NULL ||
      ((uintptr_t)out % (uintptr_t)_Alignof(nx_block_completion)) != 0u ||
      overlaps_device_storage(d, out, sizeof(*out)) != 0)
    return NX_STATUS_INVALID_ARGUMENT;
  if (d->completion_count == 0u)
    return NX_STATUS_NOT_FOUND;
  *out = d->completions[d->completion_head];
  memset(&d->completions[d->completion_head], 0, sizeof(*out));
  d->completion_head = (d->completion_head + 1u) % d->completion_capacity;
  d->completion_count--;
  return NX_STATUS_OK;
}
nx_status nx_block_memory_fail_next(nx_block_memory *d, uint32_t count,
                                    nx_status failure) {
  nx_status s = ready(d);
  if (s != NX_STATUS_OK)
    return s;
  if (count == 0u || failure == NX_STATUS_OK || failure == NX_STATUS_BUSY ||
      failure == NX_STATUS_NO_SPACE || failure == NX_STATUS_INVALID_ARGUMENT)
    return NX_STATUS_INVALID_ARGUMENT;
  d->fail_next_count = count;
  d->fail_status = failure;
  return NX_STATUS_OK;
}
nx_status nx_block_memory_crash(nx_block_memory *d) {
  nx_status s = ready(d);
  if (s != NX_STATUS_OK)
    return s;
  memcpy(d->volatile_bytes, d->durable_bytes, (size_t)d->byte_count);
  memset(d->requests, 0, (size_t)d->request_capacity * sizeof(*d->requests));
  memset(d->completions, 0,
         (size_t)d->completion_capacity * sizeof(*d->completions));
  d->request_head = 0u;
  d->request_count = 0u;
  d->completion_head = 0u;
  d->completion_count = 0u;
  d->fail_next_count = 0u;
  d->fail_status = NX_STATUS_IO_ERROR;
  return NX_STATUS_OK;
}
