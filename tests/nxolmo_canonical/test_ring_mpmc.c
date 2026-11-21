/* Upstream NXOLMO tests linked to actual Base-NEXS product code. */
#include "nexs_buffer.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRODUCERS 4u
#define CONSUMERS 4u
#define ITEMS_PER_PRODUCER 2000u
#define TOTAL_ITEMS (PRODUCERS * ITEMS_PER_PRODUCER)
#define WORKER_RETRY_LIMIT (TOTAL_ITEMS * 128u)

typedef struct worker_context {
    nx_buffer_ring *ring;
    _Atomic uint32_t *seen;
    _Atomic uint32_t *consumed;
    _Atomic uint32_t *failed;
    uint32_t worker_id;
} worker_context;

static nx_buffer_message make_message(uint64_t id) {
    nx_buffer_message message;
    memset(&message, 0, sizeof(message));
    message.request_id = id;
    message.request_generation = 1u;
    message.buffer.id.high = UINT64_C(0x4d504d4354455354);
    message.buffer.id.low = 1u;
    message.buffer.generation = 1u;
    message.length = 64u;
    message.lease_id = id;
    message.expected_revision = 1u;
    message.operation = NX_BUFFER_MESSAGE_READ;
    message.rights = (uint32_t)NX_BUFFER_RIGHT_READ;
    return message;
}

static void *producer_main(void *argument) {
    worker_context *context = (worker_context *)argument;
    uint32_t i;
    for (i = 0u; i < ITEMS_PER_PRODUCER; ++i) {
        uint64_t id = (uint64_t)context->worker_id * ITEMS_PER_PRODUCER + i + 1u;
        nx_buffer_message message = make_message(id);
        nx_result result;
        uint32_t attempt;
        uint32_t enqueued = 0u;
        for (attempt = 0u; attempt < WORKER_RETRY_LIMIT; ++attempt) {
            nx_status status = nx_buffer_ring_enqueue(context->ring, &message,
                                                      &result);
            if (status == NX_STATUS_OK) {
                enqueued = 1u;
                break;
            }
            if (status != NX_STATUS_NO_SPACE && status != NX_STATUS_BUSY &&
                status != NX_STATUS_BUDGET_EXHAUSTED) {
                atomic_store_explicit(context->failed, 1u, memory_order_release);
                return NULL;
            }
            (void)sched_yield();
        }
        if (enqueued == 0u) {
            atomic_store_explicit(context->failed, 1u, memory_order_release);
            return NULL;
        }
    }
    return NULL;
}

static void *consumer_main(void *argument) {
    worker_context *context = (worker_context *)argument;
    uint32_t iteration;
    for (iteration = 0u; iteration < WORKER_RETRY_LIMIT; ++iteration) {
        nx_buffer_message message;
        nx_result result;
        uint32_t current = atomic_load_explicit(context->consumed,
                                               memory_order_acquire);
        if (current >= TOTAL_ITEMS ||
            atomic_load_explicit(context->failed, memory_order_acquire) != 0u)
            break;
        nx_status status = nx_buffer_ring_dequeue(context->ring, &message,
                                                  &result);
        if (status == NX_STATUS_OK) {
            if (message.request_id == 0u || message.request_id > TOTAL_ITEMS) {
                atomic_store_explicit(context->failed, 1u, memory_order_release);
                break;
            }
            if (atomic_fetch_add_explicit(&context->seen[message.request_id - 1u],
                                          1u, memory_order_acq_rel) != 0u) {
                atomic_store_explicit(context->failed, 1u, memory_order_release);
                break;
            }
            atomic_fetch_add_explicit(context->consumed, 1u, memory_order_acq_rel);
        } else if (status == NX_STATUS_NOT_FOUND || status == NX_STATUS_BUSY ||
                   status == NX_STATUS_BUDGET_EXHAUSTED) {
            (void)sched_yield();
        } else {
            atomic_store_explicit(context->failed, 1u, memory_order_release);
            break;
        }
    }
    if (iteration == WORKER_RETRY_LIMIT &&
        atomic_load_explicit(context->consumed, memory_order_acquire) < TOTAL_ITEMS &&
        atomic_load_explicit(context->failed, memory_order_acquire) == 0u)
        atomic_store_explicit(context->failed, 1u, memory_order_release);
    return NULL;
}

int main(void) {
    nx_buffer_ring ring;
    nx_buffer_ring_cell cells[64];
    _Atomic uint32_t seen[TOTAL_ITEMS];
    _Atomic uint32_t consumed;
    _Atomic uint32_t failed;
    pthread_t producers[PRODUCERS];
    pthread_t consumers[CONSUMERS];
    worker_context producer_contexts[PRODUCERS];
    worker_context consumer_contexts[CONSUMERS];
    nx_result result;
    uint32_t i;

    atomic_init(&consumed, 0u);
    atomic_init(&failed, 0u);
    for (i = 0u; i < TOTAL_ITEMS; ++i)
        atomic_init(&seen[i], 0u);
    if (nx_buffer_ring_init(&ring, cells, 64u, &result) != NX_STATUS_OK) {
        (void)fprintf(stderr, "ring init failed: %u\n", result.status);
        return 1;
    }
    for (i = 0u; i < CONSUMERS; ++i) {
        consumer_contexts[i] = (worker_context){
            &ring, seen, &consumed, &failed, i
        };
        if (pthread_create(&consumers[i], NULL, consumer_main,
                           &consumer_contexts[i]) != 0)
            return 1;
    }
    for (i = 0u; i < PRODUCERS; ++i) {
        producer_contexts[i] = (worker_context){
            &ring, seen, &consumed, &failed, i
        };
        if (pthread_create(&producers[i], NULL, producer_main,
                           &producer_contexts[i]) != 0)
            return 1;
    }
    for (i = 0u; i < PRODUCERS; ++i)
        (void)pthread_join(producers[i], NULL);
    for (i = 0u; i < CONSUMERS; ++i)
        (void)pthread_join(consumers[i], NULL);
    if (atomic_load_explicit(&failed, memory_order_acquire) != 0u ||
        atomic_load_explicit(&consumed, memory_order_acquire) != TOTAL_ITEMS) {
        (void)fprintf(stderr, "MPMC ring failed: consumed=%u expected=%u\n",
                      atomic_load_explicit(&consumed, memory_order_relaxed),
                      TOTAL_ITEMS);
        return 1;
    }
    for (i = 0u; i < TOTAL_ITEMS; ++i) {
        if (atomic_load_explicit(&seen[i], memory_order_acquire) != 1u) {
            (void)fprintf(stderr, "MPMC ring lost/duplicated message %u\n", i + 1u);
            return 1;
        }
    }
    (void)puts("buffer ring MPMC: 4 producers x 2000, 4 consumers, PASS");
    return 0;
}
