/* Hosted REAL Base-NEXS full dispatch / registry / VFS / 9P fuzz.
 * No fake SYS handlers or simulated results. Does not claim guest, SMP,
 * memory/device authority or a full 9P2000.L implementation. */
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include "nexs_runtime.h"
#include "../../kernel/include/nexs_msg.h"
#include "../../kernel/include/nexs_vfs.h"
#include "../../kernel/include/nexs_sched.h"
#include "nexs_fs9p.h"
#include "nexs_p9_frame.h"
#include "nexs_registry.h"
#include "nexs_state_store.h"
#include "nexs_clock.h"
#include "nexs_endian.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static const char *const verbs[] = {
    "open", "read", "write", "close", "stat", "mkdir", "create",
    "remove", "bind", "mount", "fork", "exec", "wait", "exit",
    "chdir", "getpid", "sleep", "pipe", "dup", "seek", "fstat"
};
enum { VERBS = sizeof(verbs) / sizeof(verbs[0]), HIST = 64 };
typedef struct {
    uint64_t calls, rejected, bad, elapsed_total, elapsed_min, elapsed_max;
    uint64_t histogram[HIST], state_delta_total;
    uint64_t buddy_requests, buddy_success, buddy_failures, buddy_frees;
    uint64_t buddy_bytes, page_requests, page_success, page_failures;
    uint64_t page_frees, page_count;
} Metric;
static Metric metric[VERBS + 1u];
static Metric wire_metric[VERBS];
static Metric transport_metric[VERBS];
static uint64_t entropy = UINT64_C(0xcafebabedead1234);
static uint64_t known_failure_case = UINT64_MAX;
static uint64_t failures = 0u;
static int live_fd = -1;
static uint64_t spawned = 0u;
static uint64_t p9_valid = 0u, p9_rejected = 0u;
static int profile_allocations = 1;

static uint64_t random64(void) {
    uint64_t v = entropy;
    v ^= v >> 12u;
    v ^= v << 25u;
    v ^= v >> 27u;
    entropy = v;
    return v * UINT64_C(2685821657736338717);
}
static uint64_t clock_ns(void) {
    uint64_t value = 0u;
    if (nx_monotonic_time_ns(&value) != NX_STATUS_OK)
        return 0u;
    return value;
}
static uint32_t bucket(uint64_t ns) {
    uint32_t result = 0u;
    while (ns > 1u && result < HIST - 1u) {
        ns >>= 1u;
        ++result;
    }
    return result;
}
static void record(Metric *m, uint64_t delta, int rejected, int bad,
                   uint64_t state_delta, const NexsAllocatorMetrics *before,
                   const NexsAllocatorMetrics *after) {
    if (m->calls == 0u)
        m->elapsed_min = UINT64_MAX;
    m->calls++;
    m->rejected += (uint64_t)rejected;
    m->bad += (uint64_t)bad;
    m->elapsed_total += delta;
    m->state_delta_total += state_delta;
    m->buddy_requests += after->buddy_requests - before->buddy_requests;
    m->buddy_success += after->buddy_success - before->buddy_success;
    m->buddy_failures += after->buddy_failures - before->buddy_failures;
    m->buddy_frees += after->buddy_free_success - before->buddy_free_success;
    m->buddy_bytes +=
        after->buddy_requested_bytes - before->buddy_requested_bytes;
    m->page_requests += after->page_requests - before->page_requests;
    m->page_success += after->page_success - before->page_success;
    m->page_failures += after->page_failures - before->page_failures;
    m->page_frees += after->page_free_success - before->page_free_success;
    m->page_count +=
        after->page_requested_pages - before->page_requested_pages;
    if (delta < m->elapsed_min)
        m->elapsed_min = delta;
    if (delta > m->elapsed_max)
        m->elapsed_max = delta;
    m->histogram[bucket(delta)]++;
}
static uint64_t upper_percentile(const Metric *m, uint64_t percent) {
    uint64_t threshold = (m->calls * percent + 99u) / 100u;
    uint64_t observed = 0u;
    uint32_t b;
    for (b = 0u; b < HIST; ++b) {
        observed += m->histogram[b];
        if (observed >= threshold)
            return UINT64_C(1) << b;
    }
    return UINT64_MAX;
}
static void mark_failure(uint64_t iteration, const char *reason) {
    if (failures == 0u) {
        known_failure_case = iteration;
        fprintf(stderr, "FUZZ_FAIL seed_after_prng=%" PRIu64
                " case=%" PRIu64 " reason=%s\n", entropy, iteration, reason);
    }
    failures++;
}
static int choose_fd(uint64_t random) {
    if (live_fd >= 3 && (random & 3u) != 0u)
        return live_fd;
    return 3 + (int)(random % 64u);
}
static void init_request(KernelMsg *message, uint32_t op, uint64_t rand) {
    uint32_t name = (uint32_t)(rand % 8u);
    memset(message, 0, sizeof(*message));
    memcpy(message->verb, verbs[op], strlen(verbs[op]));
    message->sender_pid = (rand & 1u) != 0u ? 201u : 202u;
    (void)snprintf(message->arg0, sizeof(message->arg0),
                   "/fuzz/data%u", name);
    (void)snprintf(message->arg1, sizeof(message->arg1),
                   "msg%08" PRIx64, rand);
    message->n = (int64_t)choose_fd(rand >> 8u);
    if (op == 0u || op == 6u)
        message->n = (rand & 1u) ? 0101 : 0;
    if (op == 4u || op == 20u)
        message->n = 0;
    if (op == 5u)
        message->n = 0755;
    if (op == 8u)
        (void)snprintf(message->arg0, sizeof(message->arg0),
                       "/fuzz/ipc%u", name);
    if (op == 9u)
        (void)snprintf(message->arg0, sizeof(message->arg0),
                       "/fuzz/src%u", name);
    if (op == 11u)
        memcpy(message->arg0, "/fuzz/absent.nx", sizeof("/fuzz/absent.nx"));
    if (op == 15u)
        message->n = 0;
    if (op == 16u)
        message->n = 0;
    if (op == 18u)
        (void)snprintf(message->arg1, sizeof(message->arg1), "%d",
                       choose_fd(rand >> 16u));
    if (op == 19u) {
        (void)snprintf(message->arg0, sizeof(message->arg0),
                       "%d", (int)(rand % 8u));
        (void)snprintf(message->arg1, sizeof(message->arg1), "%d",
                       (int)(rand % 5u));
    }
}
static void execute_verb(uint32_t op, uint64_t iteration,
                         int via_wire) {
    KernelMsg message;
    uint64_t value = random64();
    uint64_t start, end, old_state, state_delta;
    Value result;
    Value encoded = val_nil();
    KernelMsg decoded;
    NexsAllocatorMetrics alloc_before, alloc_after;
    Metric *target = via_wire == 2 ? &transport_metric[op]
                     : via_wire != 0 ? &wire_metric[op] : &metric[op];
    int invalid = 0, bad = 0;
    init_request(&message, op, value);
    if (op == 10u && spawned >= 128u)
        message.sender_pid = 0u; /* actual dispatcher ESRCH; no mock */
    old_state = g_registry.total_keys;
    nexs_allocator_metrics_read(&alloc_before);
    start = clock_ns();
    if (via_wire != 0) {
        encoded = kmsg_encode(&message);
        if (via_wire == 2 && encoded.type == TYPE_STR) {
            Value received = val_nil();
            int sent = reg_ipc_send("/fuzz/kernel_wire", encoded);
            int received_ok = sent == 0 ?
                reg_ipc_recv("/fuzz/kernel_wire", &received) : -1;
            val_free(&encoded);
            encoded = received;
            if (received_ok != 0) {
                mark_failure(iteration, "Real REG IPC queue transfer");
                val_free(&encoded);
                return;
            }
        }
        if (encoded.type != TYPE_STR ||
            kmsg_decode(&encoded, &decoded) != 0 ||
            strcmp(decoded.verb, message.verb) != 0 ||
            strcmp(decoded.arg0, message.arg0) != 0 ||
            strcmp(decoded.arg1, message.arg1) != 0 ||
            decoded.n != message.n ||
            decoded.sender_pid != message.sender_pid) {
            mark_failure(iteration, "KernelMsg wire codec round-trip");
            val_free(&encoded);
            return;
        }
        result = kmsg_handle(&decoded);
    } else {
        result = kmsg_handle(&message);
    }
    end = clock_ns();
    val_free(&encoded);
    if (op == 10u && message.sender_pid != 0u && result.type == TYPE_INT)
        spawned++;
    invalid = result.type == TYPE_ERR || result.type == TYPE_NIL ||
              (result.type == TYPE_INT && result.ival < 0);
    if (end < start || start == 0u || end == 0u)
        bad = 1;
    if (result.type < TYPE_NIL || result.type > TYPE_PTR)
        bad = 1;
    if (op == 15u && message.sender_pid != 0u &&
        (result.type != TYPE_INT || result.ival != message.sender_pid))
        bad = 1;
    if ((op == 5u || op == 7u || op == 8u || op == 9u) &&
        message.sender_pid == 202u && result.type != TYPE_ERR)
        bad = 1;
    if ((op == 0u || op == 6u) && result.type == TYPE_INT &&
        result.ival >= 3 && result.ival < 64)
        live_fd = (int)result.ival;
    /* Deliberately preserve unclosed FD as a pressure/failure signal; fixed
     * capacity is expected to reject new open(), not overwrite live FDs. */
    /* A syscall returns an owned Value. Its release is a real provider
     * action attributed to this call, but is EXCLUDED from dispatch latency. */
    val_free(&result);
    nexs_allocator_metrics_read(&alloc_after);
    state_delta = g_registry.total_keys >= old_state
                      ? g_registry.total_keys - old_state : 0u;
    record(target, end - start, invalid, bad, state_delta,
           &alloc_before, &alloc_after);
    if (bad)
        mark_failure(iteration, verbs[op]);
}
static int p9_case(uint64_t iteration) {
    uint8_t request[96] = {0};
    uint8_t response[128];
    uint64_t start, end, value = random64();
    nx_9p_frame_view frame;
    size_t received = 0u;
    int rc, valid = (value & 3u) != 0u, bad = 0;
    nx_const_span wire;
    uint32_t request_length;
    NexsAllocatorMetrics alloc_before, alloc_after;
    memset(response, 0xa5, sizeof(response));
    if (valid) {
        /* Actual fully valid 9P Tversion request with protocol string. */
        static const uint8_t version[] = {'9','P','2','0','0','0'};
        uint8_t body[12];
        nx_store_le32(body, 8192u);
        nx_store_le16(body + 4u, (uint16_t)sizeof(version));
        memcpy(body + 6u, version, sizeof(version));
        nx_mut_span output = {request, sizeof(request)};
        nx_const_span content = {body, sizeof(body)};
        if (nx_9p_frame_encode(output, P9_TVERSION,
                               (uint16_t)value, content, 8192u,
                               &received) != NX_STATUS_OK)
            return 1;
        request_length = (uint32_t)received;
    } else {
        /* Frame sizes and contents controlled by a random caller, but always
         * within the local input allocation to rule out test-harness UB. */
        request_length = (uint32_t)(value % sizeof(request));
        uint32_t i;
        for (i = 0u; i < request_length; ++i)
            request[i] = (uint8_t)random64();
        if (request_length >= 4u)
            nx_store_le32(request, request_length + 1u);
    }
    nexs_allocator_metrics_read(&alloc_before);
    start = clock_ns();
    rc = p9_server_handle(request, request_length,
                          response, sizeof(response) - 8u);
    end = clock_ns();
    nexs_allocator_metrics_read(&alloc_after);
    if (end < start || start == 0u || end == 0u)
        bad = 1;
    if (valid) {
        wire = (nx_const_span){response, (size_t)(rc >= 0 ? rc : 0)};
        if (rc < 0 ||
            nx_9p_frame_decode(wire, 8192u, &frame, &received) != NX_STATUS_OK ||
            frame.type != P9_RVERSION || frame.tag != (uint16_t)value ||
            received != (size_t)rc)
            bad = 1;
        else
            p9_valid++;
    } else {
        if (rc != -1)
            bad = 1;
        else
            p9_rejected++;
    }
    if (response[sizeof(response) - 1u] != 0xa5u ||
        response[sizeof(response) - 8u] != 0xa5u)
        bad = 1;
    record(&metric[VERBS], end - start, !valid, bad, 0u,
           &alloc_before, &alloc_after);
    if (bad)
        mark_failure(iteration, "p9_server_handle");
    return bad;
}

/* A genuine positive kernel EXEC path must evaluate a source program and
 * publish an independently testable NXOLMO registry side effect. This is
 * separate from the randomized non-existing-path negative EXEC corpus. */
static int test_real_exec_positive(void) {
    const char *path = "build/assurance/fuzz_exec_real.nx";
    static const char source[] = "reg /fuzz/exec_real = 7919\n";
    FILE *file = fopen(path, "wb");
    KernelMsg message;
    Value result, value;
    int ok;
    if (file == NULL)
        return 0;
    if (fwrite(source, 1u, sizeof(source) - 1u, file)
            != sizeof(source) - 1u || fclose(file) != 0)
        return 0;
    memset(&message, 0, sizeof(message));
    memcpy(message.verb, "exec", sizeof("exec"));
    memcpy(message.arg0, "build/assurance/fuzz_exec_real.nx",
           sizeof("build/assurance/fuzz_exec_real.nx"));
    message.sender_pid = 201u;
    result = kmsg_handle(&message);
    value = reg_get("/fuzz/exec_real");
    ok = result.type != TYPE_ERR && value.type == TYPE_INT &&
         value.ival == INT64_C(7919);
    val_free(&value);
    val_free(&result);
    if (remove(path) != 0)
        return 0;
    return ok;
}

static int parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long number;
    errno = 0;
    number = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || number == 0u)
        return 0;
    *value = (uint64_t)number;
    return 1;
}
static int write_report(const char *path, uint64_t seed,
                        uint64_t iterations, uint64_t wall,
                        const struct rusage *usage) {
    FILE *file = fopen(path, "w");
    uint32_t i;
    if (file == NULL)
        return 1;
    fprintf(file, "operation,calls,rejected,assertion_failures,total_ns,"
                  "min_ns,mean_ns,max_ns,p50_upper_ns,p95_upper_ns,"
                  "p99_upper_ns,new_registry_nodes,buddy_requests,"
                  "buddy_success,buddy_failures,buddy_frees,buddy_bytes,"
                  "page_requests,page_success,page_failures,page_frees,"
                  "page_request_pages\n");
    for (i = 0u; i < 3u * VERBS + 1u; ++i) {
        Metric *m = i < VERBS ? &metric[i]
                  : i < 2u * VERBS ? &wire_metric[i - VERBS]
                  : i < 3u * VERBS ? &transport_metric[i - 2u * VERBS]
                  : &metric[VERBS];
        const char *operation = i == 3u * VERBS ? "9P_SERVER"
                              : verbs[i % VERBS];
        const char *transport = i >= 2u * VERBS
                                    ? "REG_IPC/" : i >= VERBS
                                    ? "wire/" : "direct/";
        char operation_label[64];
        if (i == 3u * VERBS)
            (void)snprintf(operation_label, sizeof(operation_label),
                           "9P_SERVER");
        else
            (void)snprintf(operation_label, sizeof(operation_label),
                           "%s%s", transport, operation);
        fprintf(file, "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 ",%.3f,%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%" PRIu64 "\n",
                operation_label,
                m->calls, m->rejected, m->bad, m->elapsed_total,
                m->elapsed_min,
                m->calls == 0u ? 0.0 :
                (double)m->elapsed_total / (double)m->calls,
                m->elapsed_max, upper_percentile(m, 50u),
                upper_percentile(m, 95u), upper_percentile(m, 99u),
                m->state_delta_total,
                m->buddy_requests, m->buddy_success, m->buddy_failures,
                m->buddy_frees, m->buddy_bytes, m->page_requests,
                m->page_success, m->page_failures, m->page_frees,
                m->page_count);
    }
    fprintf(file, "# seed=%" PRIu64 " iterations=%" PRIu64
                  " wall_ns=%" PRIu64 " registry_nodes=%zu"
                  " minor_faults=%ld major_faults=%ld"
                  " maxrss_raw=%ld rss_platform_units=macOS_bytes_or_Linux_KiB"
                  " context_switches_voluntary=%ld involuntary=%ld"
                  " failures=%" PRIu64 " first_failed_case=%" PRIu64
                  " p9_valid=%" PRIu64 " p9_rejected=%" PRIu64
                  " allocator_profile_enabled=%d\n",
            seed, iterations, wall, g_registry.total_keys,
            usage->ru_minflt, usage->ru_majflt, usage->ru_maxrss,
            usage->ru_nvcsw, usage->ru_nivcsw, failures,
            known_failure_case, p9_valid, p9_rejected,
            profile_allocations);
    return fclose(file) != 0;
}
int main(int argc, char **argv) {
    uint64_t cases = UINT64_C(100000);
    uint64_t seed = UINT64_C(0xa11ce20261121);
    uint64_t start, end, i;
    nx_state_store_stats stats;
    nx_result result;
    struct rusage usage;
    const char *report = "build/assurance/system_syscall_fuzz.csv";
    uint32_t op;
    if ((argc > 1 && !parse_u64(argv[1], &cases)) ||
        (argc > 2 && !parse_u64(argv[2], &seed)) || argc > 5)
        return 2;
    if (argc >= 4)
        report = argv[3];
    if (argc == 5) {
        if (strcmp(argv[4], "0") != 0 && strcmp(argv[4], "1") != 0)
            return 2;
        profile_allocations = strcmp(argv[4], "1") == 0;
    }
    entropy = seed;
    nexs_allocator_observation_enable(profile_allocations);
    if (nexs_runtime_init() != 0) {
        fprintf(stderr, "real NEXS runtime init failed\n");
        return 2;
    }
    vfs_init();
    if (vfs_mkdir("/fuzz", 0755u) != 0) {
        fprintf(stderr, "real VFS namespace root admission failed\\n");
        return 2;
    }
    sched_init();
    p9_server_init();
    if (reg_ipc_init_queue("/fuzz/kernel_wire", 64) != 0 ||
        reg_ipc_enable_pipes() != 0) {
        fprintf(stderr, "Real REG IPC POSIX pipe provider admission failed\n");
        return 2;
    }
    if (reg_set("/proc/201/caps", val_int(RK_ADMIN), RK_ALL) != 0 ||
        reg_set("/proc/202/caps", val_int(0), RK_ALL) != 0)
        return 2;
    if (test_real_exec_positive() == 0) {
        fprintf(stderr, "REAL syscall exec positive source evaluation FAILED\n");
        return 2;
    }
    start = clock_ns();
    for (op = 0u; op < VERBS; ++op) {
        execute_verb(op, op, 0);
        execute_verb(op, op + VERBS, 1);
        execute_verb(op, op + 2u * VERBS, 2);
    }
    for (i = 0u; i < cases; ++i) {
        op = (uint32_t)(random64() % VERBS);
        execute_verb(op, i + 3u * VERBS, (int)(random64() % 3u));
        if ((i & 3u) == 0u)
            (void)p9_case(i + 3u * VERBS);
        if ((i & UINT64_C(4095)) == 0u) {
            if (nx_state_store_validate(&g_registry.state,
                                         &result) != NX_STATUS_OK)
                mark_failure(i, "NXOLMO StateStore graph validation");
            if (nx_state_store_stats_read(&g_registry.state,
                                           &stats, &result) != NX_STATUS_OK ||
                stats.count != g_registry.total_keys)
                mark_failure(i, "NXOLMO registry identity/node mismatch");
        }
    }
    end = clock_ns();
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 2;
    for (op = 0u; op < VERBS; ++op) {
        if (metric[op].calls == 0u || wire_metric[op].calls == 0u ||
            transport_metric[op].calls == 0u)
            mark_failure(cases + 1u, "uncovered direct/wire/REG IPC dispatch");
    }
    if (metric[VERBS].calls == 0u)
        mark_failure(cases + 1u, "uncovered 9P server");
    if (write_report(report, seed, cases, end - start, &usage) != 0)
        return 2;
    printf("NEXS real syscall+9P fuzz seed=%" PRIu64
           " random_cases=%" PRIu64
           " verbs=%u direct+wire+IPC_covered=%u 9P_calls=%" PRIu64
           " elapsed=%.3fs registry_objects=%zu"
           " failed_assertions=%" PRIu64 " report=%s\n",
           seed, cases, VERBS, VERBS * 3u, metric[VERBS].calls,
           (double)(end - start) / 1e9,
           g_registry.total_keys, failures, report);
    printf("NOTE: hosted dispatcher, real registry/VFS+9P, NOT an OS1 "
           "kernel/IPC execution or 9P2000.L interoperability test\n");
    return failures == 0u ? 0 : 1;
}
