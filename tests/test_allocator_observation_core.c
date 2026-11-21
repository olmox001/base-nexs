/* Tests the actual Buddy/page providers and their opt-in profiler. */
#include "nexs_alloc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "provider profiler failed line %d: %s\n",__LINE__,#x); \
    return 1; } } while (0)

static int off_has_no_side_effects(void) {
    NexsAllocatorMetrics a, b;
    nexs_allocator_observation_enable(0);
    nexs_allocator_metrics_read(&a);
    void *p = buddy_alloc(65u);
    CHECK(p != NULL);
    buddy_free(p);
    nexs_allocator_metrics_read(&b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    return 0;
}
static int on_counts_live_providers(void) {
    NexsAllocatorMetrics a, b;
    nexs_allocator_observation_enable(1);
    nexs_allocator_metrics_read(&a);
    void *p = buddy_alloc(73u);
    CHECK(p != NULL);
    void *page = page_alloc(2u);
    CHECK(page != NULL);
    CHECK(page_free_checked(page, 1u) != 0);
    CHECK(page_free_checked(page, 2u) == 0);
    buddy_free(p);
    nexs_allocator_metrics_read(&b);
    CHECK(b.buddy_requests == a.buddy_requests + 1u);
    CHECK(b.buddy_success == a.buddy_success + 1u);
    CHECK(b.buddy_free_success == a.buddy_free_success + 1u);
    CHECK(b.buddy_requested_bytes == a.buddy_requested_bytes + 73u);
    CHECK(b.page_requests == a.page_requests + 1u);
    CHECK(b.page_success == a.page_success + 1u);
    CHECK(b.page_free_success == a.page_free_success + 1u);
    CHECK(b.page_requested_pages == a.page_requested_pages + 2u);
    CHECK(b.buddy_failures == a.buddy_failures);
    CHECK(b.page_failures == a.page_failures);
    return 0;
}
static int rejected_allocations_are_counted(void) {
    NexsAllocatorMetrics a, b;
    nexs_allocator_metrics_read(&a);
    CHECK(buddy_alloc(SIZE_MAX) == NULL);
    CHECK(page_alloc(SIZE_MAX) == NULL);
    nexs_allocator_metrics_read(&b);
    CHECK(b.buddy_requests == a.buddy_requests + 1u);
    CHECK(b.buddy_failures == a.buddy_failures + 1u);
    CHECK(b.page_requests == a.page_requests + 1u);
    CHECK(b.page_failures == a.page_failures + 1u);
    CHECK(b.page_requested_pages == a.page_requested_pages);
    CHECK(b.buddy_requested_bytes == a.buddy_requested_bytes);
    nexs_allocator_observation_enable(0);
    return 0;
}
int main(void) {
    CHECK(off_has_no_side_effects() == 0);
    CHECK(on_counts_live_providers() == 0);
    CHECK(rejected_allocations_are_counted() == 0);
    puts("real Buddy/Page provider telemetry exact accounting and opt-out: PASS");
    return 0;
}
