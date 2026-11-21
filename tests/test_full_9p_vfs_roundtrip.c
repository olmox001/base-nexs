/* End-to-end REAL product: NexsOS runtime -> kernel VFS -> NXOLMO RegKey
 * StateStore -> 9P server receive/write/read. No mocks/alternate registry.
 * Tests duplicate reads at offset zero and stale request rejection. */
#include "nexs_runtime.h"
#include "nexs_registry.h"
#include "nexs_fs9p.h"
#include "nexs_p9_frame.h"
#include "nexs_endian.h"
#include "nexs_clock.h"
#include "../../kernel/include/nexs_vfs.h"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "real 9P/VFS/NXREG roundtrip FAIL at %d: %s\n", \
            __LINE__, #x); return 1; } } while (0)

static int roundtrip(uint8_t type, const uint8_t *body, size_t len,
                     uint8_t *response, size_t *length) {
    uint8_t wire[128];
    nx_const_span input = {body, len};
    nx_mut_span output = {wire, sizeof(wire)};
    nx_const_span reply;
    nx_9p_frame_view view;
    size_t written = 0u, consumed = 0u;
    int received;
    if (nx_9p_frame_encode(output, type, 18u, input, 8192u,
                           &written) != NX_STATUS_OK)
        return 0;
    received = p9_server_handle(wire, written, response, 128u);
    if (received < 7)
        return 0;
    reply = (nx_const_span){response, (size_t)received};
    if (nx_9p_frame_decode(reply, 8192u, &view, &consumed) != NX_STATUS_OK ||
        view.tag != 18u || consumed != (size_t)received ||
        view.type != (uint8_t)(type + 1u))
        return 0;
    *length = (size_t)received;
    return 1;
}

/* Unlike the decoder benchmarks, this test mutates a live file object on
 * EVERY round and then actually reads and validates the result through the
 * real 9P/VFS interface. Writes and reads use independent FIDs. */
static int test_random_9p_commits(nx_object_ref identity) {
    uint64_t seed = UINT64_C(0x5eeda611a);
    uint64_t write_elapsed = 0u;
    uint64_t read_elapsed = 0u;
    uint8_t wire_body[32] = {0};
    uint8_t response[128];
    char expected[5] = {'h','A','X','l','o'};
    nx_result state_result;
    nx_state_node node, previous;
    size_t reply_length = 0u;
    uint64_t iteration;
    uint64_t rounds = 2000u;
    uint64_t full_start = 0u;
    uint64_t full_end = 0u;
    const char *configured = getenv("NEXS_P9_FUZZ_ROUNDS");
    if (configured != NULL) {
        char *end = NULL;
        unsigned long long parsed;
        errno = 0;
        parsed = strtoull(configured, &end, 10);
        if (errno != 0 || end == configured || *end != 0 ||
            parsed == 0u || parsed > UINT64_C(1000000000))
            return 0;
        rounds = (uint64_t)parsed;
    }
    if (reg_object_read("/fuzz/data0", &previous, &state_result)
        != NX_STATUS_OK)
        return 0;
    if (nx_monotonic_time_ns(&full_start) != NX_STATUS_OK)
        return 0;
    for (iteration = 0u; iteration < rounds; ++iteration) {
        uint64_t started, finished;
        uint32_t offset, count, j;
        seed ^= seed << 13u;
        seed ^= seed >> 7u;
        seed ^= seed << 17u;
        offset = (uint32_t)(seed % 5u);
        count = 1u + (uint32_t)((seed >> 9u) % (5u - offset));
        memset(wire_body, 0, sizeof(wire_body));
        nx_store_le32(wire_body, 45u);
        nx_store_le64(wire_body + 4u, offset);
        nx_store_le32(wire_body + 12u, count);
        for (j = 0u; j < count; ++j) {
            expected[offset + j] =
                (char)('a' + (uint8_t)((seed >> ((j & 7u) * 8u)) % 26u));
            wire_body[16u + j] = (uint8_t)expected[offset + j];
        }
        started = 0u; finished = 0u;
        if (nx_monotonic_time_ns(&started) != NX_STATUS_OK)
            return 0;
        if (!roundtrip(P9_TWRITE, wire_body, 16u + count,
                       response, &reply_length) ||
            nx_load_le32(response + 7u) != count)
            return 0;
        if (nx_monotonic_time_ns(&finished) != NX_STATUS_OK ||
            finished < started)
            return 0;
        write_elapsed += finished - started;
        if (reg_object_read("/fuzz/data0", &node, &state_result)
                != NX_STATUS_OK ||
            !nx_object_ref_equal(identity, node.ref) ||
            node.revision != previous.revision + 1u)
            return 0;
        previous = node;
        memset(wire_body, 0, sizeof(wire_body));
        nx_store_le32(wire_body, 44u);
        nx_store_le64(wire_body + 4u, 0u);
        nx_store_le32(wire_body + 12u, 5u);
        if (nx_monotonic_time_ns(&started) != NX_STATUS_OK)
            return 0;
        if (!roundtrip(P9_TREAD, wire_body, 16u,
                       response, &reply_length) ||
            reply_length != 16u ||
            nx_load_le32(response + 7u) != 5u ||
            memcmp(response + 11u, expected, 5u) != 0)
            return 0;
        if (nx_monotonic_time_ns(&finished) != NX_STATUS_OK ||
            finished < started)
            return 0;
        read_elapsed += finished - started;
    }
    if (nx_monotonic_time_ns(&full_end) != NX_STATUS_OK ||
        full_end <= full_start)
        return 0;
    printf("P9 real VFS->NXOLMO mutation fuzz: %llu write commits, "
           "%llu verified reads; average write %.1f ns, "
           "average read %.1f ns, all_validation_wall_ops_per_second=%.1f; "
           "seed deterministic PASS\n",
           (unsigned long long)rounds,
           (unsigned long long)rounds,
           (double)write_elapsed / (double)rounds,
           (double)read_elapsed / (double)rounds,
           (double)(rounds * 2u) * 1e9 /
               (double)(full_end - full_start));
    return 1;
}

/* Hostile semantic 9P wire fuzzer AFTER valid FIDs and a live file exist.
 * Its payloads are structurally valid but unauthorized/stale. Half the
 * cases also mutate length headers. All go through the REAL server.
 * Every accepted mutation is a failure; every denied operation must leave
 * the real NXOLMO object ref, revision and value unchanged. */
typedef struct {
    uint64_t calls, denied_local, denied_wire_error, total_ns, min_ns, max_ns;
    uint64_t latency_bins[64];
} P9AdversarialMetric;

static uint64_t adversarial_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

static void adversarial_record(P9AdversarialMetric *metric, uint64_t ns,
                               int wire_error) {
    uint32_t bin = 0u;
    uint64_t sample = ns;
    if (metric->calls == 0u)
        metric->min_ns = UINT64_MAX;
    metric->calls++;
    metric->total_ns += ns;
    if (ns < metric->min_ns)
        metric->min_ns = ns;
    if (ns > metric->max_ns)
        metric->max_ns = ns;
    if (wire_error)
        metric->denied_wire_error++;
    else
        metric->denied_local++;
    while (sample > 1u && bin < 63u) {
        sample >>= 1u;
        bin++;
    }
    metric->latency_bins[bin]++;
}

static uint64_t adversarial_p99_upper(const P9AdversarialMetric *metric) {
    uint64_t threshold = (metric->calls * 99u + 99u) / 100u;
    uint64_t found = 0u;
    uint32_t bin;
    for (bin = 0u; bin < 64u; ++bin) {
        found += metric->latency_bins[bin];
        if (found >= threshold)
            return UINT64_C(1) << bin;
    }
    return UINT64_MAX;
}

static int test_adversarial_denied_wire(void) {
    const char *config = getenv("NEXS_P9_NEGATIVE_CASES");
    uint64_t cases = 2048u;
    uint64_t seed = UINT64_C(0x9f22dc3a6b105b71);
    uint64_t i, started, ended;
    const char *names[8] = {
        "Tattach-duplicate", "Twalk-missing", "Topen-stale",
        "Tread-writeonly", "Twrite-readonly", "Tclunk-stale",
        "Tversion-too-small", "Tstat-unimplemented"
    };
    const uint8_t types[8] = {
        P9_TATTACH, P9_TWALK, P9_TOPEN, P9_TREAD,
        P9_TWRITE, P9_TCLUNK, P9_TVERSION, P9_TSTAT
    };
    P9AdversarialMetric metrics[8] = {{0}};
    nx_result result;
    nx_state_node initial, current;
    Value value_before, value_after;
    if (config != NULL) {
        char *end = NULL;
        unsigned long long parsed;
        errno = 0;
        parsed = strtoull(config, &end, 10);
        if (errno != 0 || end == config || *end != 0 ||
            parsed == 0u || parsed > UINT64_C(1000000000))
            return 0;
        cases = (uint64_t)parsed;
    }
    if (reg_object_read("/fuzz/data0", &initial, &result)
        != NX_STATUS_OK)
        return 0;
    value_before = reg_get("/fuzz/data0");
    if (value_before.type != TYPE_STR || value_before.data == NULL)
        return 0;
    for (i = 0u; i < cases; ++i) {
        uint8_t body[64] = {0};
        uint8_t wire[128], reply[128] = {0};
        nx_const_span request_body;
        nx_mut_span wire_storage = {wire, sizeof(wire)};
        uint64_t random = adversarial_rng(&seed);
        uint32_t selection = (uint32_t)(random % 8u);
        size_t body_length = 0u, written = 0u;
        int received, negative, wire_error;
        switch (selection) {
        case 0u: /* duplicate live fid root */
            nx_store_le32(body, 42u);
            nx_store_le32(body + 4u, UINT32_MAX);
            body_length = 12u;
            break;
        case 1u: {
            char missing[26];
            int length = snprintf(missing, sizeof(missing), "missing-%08x",
                                  (unsigned)random);
            if (length <= 0 || length >= (int)sizeof(missing))
                return 0;
            nx_store_le32(body, 42u);
            nx_store_le32(body + 4u, 700u + (uint32_t)(random % 100u));
            nx_store_le16(body + 8u, 1u);
            nx_store_le16(body + 10u, (uint16_t)length);
            memcpy(body + 12u, missing, (size_t)length);
            body_length = 12u + (size_t)length;
            break;
        }
        case 2u:
            nx_store_le32(body, 700u + (uint32_t)(random % 100u));
            body_length = 5u;
            break;
        case 3u: /* the real write-only fid 45 must refuse reads */
            nx_store_le32(body, 45u);
            nx_store_le64(body + 4u, random % 5u);
            nx_store_le32(body + 12u, 1u);
            body_length = 16u;
            break;
        case 4u: /* the real read-only fid 44 must refuse writes */
            nx_store_le32(body, 44u);
            nx_store_le64(body + 4u, random % 5u);
            nx_store_le32(body + 12u, 1u);
            body[16u] = (uint8_t)random;
            body_length = 17u;
            break;
        case 5u:
            nx_store_le32(body, 700u + (uint32_t)(random % 100u));
            body_length = 4u;
            break;
        case 6u:
            nx_store_le32(body, 8u);
            nx_store_le16(body + 4u, 6u);
            memcpy(body + 6u, "9P2000", 6u);
            body_length = 12u;
            break;
        case 7u:
            nx_store_le32(body, 44u);
            body_length = 4u;
            break;
        default:
            return 0;
        }
        request_body = (nx_const_span){body, body_length};
        if (nx_9p_frame_encode(wire_storage, types[selection],
                               (uint16_t)i, request_body, 8192u,
                               &written) != NX_STATUS_OK)
            return 0;
        if ((random & 16u) != 0u)
            nx_store_le32(wire, (uint32_t)written + 1u);
        if (nx_monotonic_time_ns(&started) != NX_STATUS_OK)
            return 0;
        received = p9_server_handle(wire, written, reply, sizeof(reply));
        if (nx_monotonic_time_ns(&ended) != NX_STATUS_OK || ended < started)
            return 0;
        wire_error = received >= 7 && reply[4] == 107u &&
                     nx_load_le32(reply) == (uint32_t)received;
        negative = received < 7 || wire_error;
        if (!negative) {
            fprintf(stderr,
                    "P9_HOSTILE_UNEXPECTED_SUCCESS case=%llu seed=%llu op=%s reply=%d\n",
                    (unsigned long long)i, (unsigned long long)seed,
                    names[selection], received);
            val_free(&value_before);
            return 0;
        }
        adversarial_record(&metrics[selection], ended - started, wire_error);
    }
    if (reg_object_read("/fuzz/data0", &current, &result) !=
            NX_STATUS_OK ||
        !nx_object_ref_equal(initial.ref, current.ref) ||
        initial.revision != current.revision) {
        val_free(&value_before);
        return 0;
    }
    value_after = reg_get("/fuzz/data0");
    if (value_after.type != TYPE_STR || value_after.data == NULL ||
        strcmp((const char *)value_before.data,
               (const char *)value_after.data) != 0) {
        val_free(&value_before);
        val_free(&value_after);
        return 0;
    }
    val_free(&value_before);
    val_free(&value_after);
    for (i = 0u; i < 8u; ++i) {
        P9AdversarialMetric *metric = &metrics[i];
        if (metric->calls == 0u)
            continue;
        printf("P9_HOSTILE %-21s calls=%llu rejected=%llu wire_errors=%llu "
               "mean_ns=%.1f p99_upper_ns=%llu max_ns=%llu\n",
               names[i], (unsigned long long)metric->calls,
               (unsigned long long)metric->denied_local,
               (unsigned long long)metric->denied_wire_error,
               (double)metric->total_ns / (double)metric->calls,
               (unsigned long long)adversarial_p99_upper(metric),
               (unsigned long long)metric->max_ns);
    }
    printf("P9_HOSTILE PASS cases=%llu seed_final=%llu "
           "preserved_NXOLMO_identity_revision_and_data\n",
           (unsigned long long)cases, (unsigned long long)seed);
    return 1;
}

/* A named file is one NXOLMO StateStore node, not a numeric inode entry
 * plus a second pathname -> inode cache. These operations use the actual
 * VFS, allocation providers and generational StateStore. */
static int test_canonical_named_file_adversarial(void) {
    enum { LIVE_FDS = 192, FILES = 2400 };
    int held[LIVE_FDS];
    VfsInode before, after;
    nx_state_node node;
    nx_result result;
    uint64_t begun, finished;
    uint32_t i;
    char path[96];
    char canonical[128];
    int stale, replacement;

    if (vfs_mkdir("/wide-nxolmo", 0755u) != 0)
        return 0;
    /* Explicitly exercise descriptor growth beyond the former 64-FD cap. */
    for (i = 0u; i < LIVE_FDS; ++i) {
        int size = snprintf(path, sizeof(path),
                            "/wide-nxolmo/file-%04u", i);
        if (size <= 0 || (size_t)size >= sizeof(path))
            return 0;
        held[i] = vfs_open(path, 0100 | 1);
        if (held[i] < 3 || vfs_stat(path, &before) != 0 ||
            before.ino != before.object.id.low ||
            before.mode != 0100644u)
            return 0;
        size = snprintf(canonical, sizeof(canonical),
                        "/wide-nxolmo/file-%04u", i);
        if (size <= 0 || (size_t)size >= sizeof(canonical) ||
            reg_object_read(canonical, &node, &result) != NX_STATUS_OK ||
            !nx_object_ref_equal(node.ref, before.object))
            return 0;
    }
    for (i = 0u; i < LIVE_FDS; ++i) {
        if (vfs_close(held[i]) != 0)
            return 0;
    }
    for (i = LIVE_FDS; i < FILES; ++i) {
        int size = snprintf(path, sizeof(path),
                            "/wide-nxolmo/file-%04u", i);
        if (size <= 0 || (size_t)size >= sizeof(path))
            return 0;
        int fd = vfs_open(path, 0100 | 1);
        if (fd < 3 || vfs_close(fd) != 0)
            return 0;
    }
    if (nx_monotonic_time_ns(&begun) != NX_STATUS_OK)
        return 0;
    for (i = 0u; i < 8192u; ++i) {
        uint32_t selected = (uint32_t)
            ((uint64_t)i * UINT64_C(1033) % FILES);
        (void)snprintf(path, sizeof(path),
                       "/wide-nxolmo/file-%04u", selected);
        if (vfs_stat(path, &before) != 0 ||
            !nx_object_ref_is_valid(before.object))
            return 0;
    }
    if (nx_monotonic_time_ns(&finished) != NX_STATUS_OK ||
        finished < begun)
        return 0;
    if (reg_lookup("/vfs/inode") != NULL)
        return 0; /* The old numeric inode tree must NOT be rebuilt. */

    stale = vfs_open("/wide-nxolmo/stale", 0100 | 1);
    if (stale < 3 || vfs_write(stale, "old", 3u) != 3 ||
        vfs_stat("/wide-nxolmo/stale", &before) != 0 ||
        vfs_remove("/wide-nxolmo/stale") != 0 ||
        vfs_stat("/wide-nxolmo/stale", &after) == 0 ||
        vfs_write(stale, "bad", 3u) >= 0 ||
        vfs_seek(stale, 0, 0) >= 0 ||
        nx_state_store_read(&g_registry.state, before.object, &node,
                            &result) == NX_STATUS_OK)
        return 0;
    replacement = vfs_open("/wide-nxolmo/stale", 0100 | 1);
    if (replacement < 3 ||
        vfs_stat("/wide-nxolmo/stale", &after) != 0 ||
        nx_object_ref_equal(before.object, after.object) ||
        vfs_write(stale, "bad", 3u) >= 0 ||
        vfs_write(replacement, "new", 3u) != 3 ||
        vfs_close(stale) != 0 || vfs_close(replacement) != 0)
        return 0;

    /* Malformed names are rejected before creating intermediate nodes. */
    size_t count = g_registry.total_keys;
    if (vfs_open("/wide-nxolmo/../rogue", 0100 | 1) >= 0 ||
        vfs_open("/wide-nxolmo//rogue", 0100 | 1) >= 0 ||
        vfs_mkdir("/wide-nxolmo/./rogue", 0755u) == 0 ||
        g_registry.total_keys != count)
        return 0;
    /* SEEK_END must use the real file length, never the old fake
     * SEEK_SET fallback; overflow and underflow must leave position
     * unchanged. Verify a live canonical object, not a mock FD. */
    {
        int sample = vfs_open("/wide-nxolmo/seek-valid", 0100 | 2);
        char tail[2] = {0};
        if (sample < 3 || vfs_write(sample, "abcdef", 6u) != 6 ||
            vfs_seek(sample, -2, 2) != 4 ||
            vfs_read(sample, tail, 2u) != 2 ||
            memcmp(tail, "ef", 2u) != 0 ||
            vfs_seek(sample, INT64_MAX, 1) != -1 ||
            vfs_seek(sample, INT64_MIN, 1) != -1 ||
            vfs_seek(sample, 0, 3) != -1 ||
            vfs_seek(sample, 0, 1) != 6 ||
            vfs_close(sample) != 0)
            return 0;
    }
    if (vfs_remove("/wide-nxolmo") != 0 ||
        vfs_stat("/wide-nxolmo/file-0000", &after) == 0)
        return 0;
    printf("NXOLMO_CANONICAL_VFS PASS files=%u concurrent_open_fds=%u "
           "random_stats=%u mean_stat_ns=%.1f no_numeric_inode_scan=1 "
           "stale_ref_denied=1\n", FILES, LIVE_FDS, 8192u,
           (double)(finished - begun) / 8192.0);
    return 1;
}

static int test_p9_dynamic_generational_fids(void) {
    enum { COUNT = 192 };
    uint8_t body[96], response[128];
    size_t received;
    uint32_t i;
    char path[96];
    VfsInode old_file, new_file;
    nx_result result;
    if (vfs_mkdir("/manyfid", 0755u) != 0)
        return 0;
    for (i = 0u; i < COUNT; ++i) {
        int size = snprintf(path, sizeof(path),
                            "/manyfid/item-%04u", i);
        if (size <= 0 || (size_t)size >= sizeof(path))
            return 0;
        int fd = vfs_open(path, 0100 | 1);
        if (fd < 3 || vfs_close(fd) != 0)
            return 0;
    }
    /* All 192 real 9P FIDs and 192 VFS FDs remain simultaneously
     * live, exercising page-backed capacity growth in both providers. */
    for (i = 0u; i < COUNT; ++i) {
        char basename[32];
        int length = snprintf(basename, sizeof(basename), "item-%04u", i);
        int size = snprintf(path, sizeof(path),
                            "/manyfid/item-%04u", i);
        if (size <= 0 || (size_t)size >= sizeof(path) ||
            length <= 0 || length >= (int)sizeof(basename))
            return 0;
        memset(body, 0, sizeof(body));
        nx_store_le32(body, 42u);
        nx_store_le32(body + 4u, 1000u + i);
        nx_store_le16(body + 8u, 2u);
        nx_store_le16(body + 10u, 7u);
        memcpy(body + 12u, "manyfid", 7u);
        nx_store_le16(body + 19u, (uint16_t)length);
        memcpy(body + 21u, basename, (size_t)length);
        if (!roundtrip(P9_TWALK, body, 21u + (size_t)length,
                       response, &received) ||
            vfs_stat(path, &old_file) != 0 ||
            nx_load_le64(response + 27u) != old_file.object.id.low)
            return 0;
        memset(body, 0, sizeof(body));
        nx_store_le32(body, 1000u + i);
        if (!roundtrip(P9_TOPEN, body, 5u, response, &received))
            return 0;
    }
    for (i = 0u; i < COUNT; ++i) {
        memset(body, 0, sizeof(body));
        nx_store_le32(body, 1000u + i);
        nx_store_le32(body + 12u, 1u);
        if (!roundtrip(P9_TREAD, body, 16u, response, &received) ||
            received != 11u || nx_load_le32(response + 7u) != 0u)
            return 0;
        memset(body, 0, sizeof(body));
        nx_store_le32(body, 1000u + i);
        if (!roundtrip(P9_TCLUNK, body, 4u, response, &received))
            return 0;
    }
    int fd = vfs_open("/manyfid/stale", 0100 | 1);
    if (fd < 3 || vfs_stat("/manyfid/stale", &old_file) != 0 ||
        vfs_close(fd) != 0)
        return 0;
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 9000u);
    nx_store_le16(body + 8u, 2u);
    nx_store_le16(body + 10u, 7u);
    memcpy(body + 12u, "manyfid", 7u);
    nx_store_le16(body + 19u, 5u);
    memcpy(body + 21u, "stale", 5u);
    if (!roundtrip(P9_TWALK, body, 26u, response, &received))
        return 0;
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 9000u);
    body[4] = 1u;
    if (!roundtrip(P9_TOPEN, body, 5u, response, &received) ||
        vfs_remove("/manyfid/stale") != 0)
        return 0;
    fd = vfs_open("/manyfid/stale", 0100 | 1);
    if (fd < 3 || vfs_stat("/manyfid/stale", &new_file) != 0 ||
        nx_object_ref_equal(old_file.object, new_file.object) ||
        vfs_close(fd) != 0)
        return 0;
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 9000u);
    nx_store_le32(body + 12u, 1u);
    body[16] = 'X';
    if (roundtrip(P9_TWRITE, body, 17u, response, &received))
        return 0;
    if (vfs_stat("/manyfid/stale", &new_file) != 0 || new_file.size != 0u)
        return 0;
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 9000u);
    if (!roundtrip(P9_TCLUNK, body, 4u, response, &received) ||
        vfs_remove("/manyfid") != 0 ||
        nx_state_store_validate(&g_registry.state, &result) != NX_STATUS_OK)
        return 0;
    printf("P9_DYNAMIC_CHANNELS PASS simultaneous_fids=%u "
           "simultaneous_vfs_fds=%u stale_generation_denied=1 "
           "NXOLMO_tree_valid=1\n", COUNT, COUNT);
    return 1;
}

int main(void) {
    uint8_t response[128];
    uint8_t body[64] = {0};
    size_t received = 0u;
    nx_result state_result;
    nx_state_node original, updated;
    int fd;

    CHECK(nexs_runtime_init() == 0);
    /* Single path admission owner: independently exercise valid names,
     * traversal denial, repeated/trailing separators and full-path limits.
     * These tests execute the same product function as VFS and source boot. */
    CHECK(reg_path_valid("/") == 1);
    CHECK(reg_path_valid("/test/canonical.node") == 1);
    CHECK(reg_path_valid("/test/hidden.asset") == 1);
    CHECK(reg_path_valid("relative/file") == 0);
    CHECK(reg_path_valid("/test/./file") == 0);
    CHECK(reg_path_valid("/test/../file") == 0);
    CHECK(reg_path_valid("/test//file") == 0);
    CHECK(reg_path_valid("/test/file/") == 0);
    CHECK(reg_path_valid(NULL) == 0);
    char overlong_name[NAME_LEN + 3u];
    memset(overlong_name, 'q', sizeof(overlong_name));
    overlong_name[0] = '/';
    overlong_name[sizeof(overlong_name) - 1u] = '\0';
    CHECK(reg_path_valid(overlong_name) == 0);
    char overlong_path[REG_PATH_MAX + 1u];
    memset(overlong_path, 'a', sizeof(overlong_path));
    overlong_path[0] = '/';
    overlong_path[REG_PATH_MAX] = '\0';
    CHECK(reg_path_valid(overlong_path) == 0);
    vfs_init();
    CHECK(vfs_open("/test/./file", 0100 | 1) == -1);
    CHECK(vfs_mkdir("/test/../bad", 0755) == -1);
    CHECK(reg_lookup("/test/../bad") == NULL);
    p9_server_init();
    CHECK(vfs_mkdir("/fuzz", 0755) == 0);
    fd = vfs_open("/fuzz/data0", 0100 | 1);
    CHECK(fd >= 3 && vfs_close(fd) == 0);

    /* Attach root fid 42, empty uname/aname. */
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, UINT32_MAX);
    CHECK(roundtrip(P9_TATTACH, body, 12u, response, &received));
    CHECK(received == 20u && response[7] == 0x80);
    CHECK(nx_load_le64(response + 12u) != 0u);
    CHECK(!roundtrip(P9_TATTACH, body, 12u, response, &received));

    /* Walk /fuzz/data0: real file path published by the kernel VFS. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 43u);
    nx_store_le16(body + 8u, 2u);
    nx_store_le16(body + 10u, 4u);
    memcpy(body + 12u, "fuzz", 4u);
    nx_store_le16(body + 16u, 5u);
    memcpy(body + 18u, "data0", 5u);
    CHECK(roundtrip(P9_TWALK, body, 23u, response, &received));
    CHECK(received == 35u);
    CHECK(nx_load_le64(response + 14u) != 0u);
    CHECK(nx_load_le64(response + 27u) != 0u);

    /* 9P must not claim it walked a name absent from the real VFS. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 77u);
    nx_store_le16(body + 8u, 1u);
    nx_store_le16(body + 10u, 11u);
    memcpy(body + 12u, "nonexistent", 11u);
    CHECK(!roundtrip(P9_TWALK, body, 23u, response, &received));

    memset(body, 0, sizeof(body));
    nx_store_le32(body, 43u);
    body[4] = 1u;
    CHECK(roundtrip(P9_TOPEN, body, 5u, response, &received));
    CHECK(received == 24u);
    CHECK(!roundtrip(P9_TOPEN, body, 5u, response, &received));
    /* write-only channels have no READ authority */
    nx_store_le32(body + 12u, 1u);
    CHECK(!roundtrip(P9_TREAD, body, 16u, response, &received));
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 43u);
    nx_store_le32(body + 12u, 5u);
    memcpy(body + 16u, "hello", 5u);
    CHECK(roundtrip(P9_TWRITE, body, 21u, response, &received));
    CHECK(received == 11u && nx_load_le32(response + 7u) == 5u);
    CHECK(reg_object_read("/fuzz/data0", &original,
                          &state_result) == NX_STATUS_OK);

    memset(body, 0, sizeof(body));
    nx_store_le32(body, 43u);
    CHECK(roundtrip(P9_TCLUNK, body, 4u, response, &received));
    CHECK(!roundtrip(P9_TCLUNK, body, 4u, response, &received));

    /* Walk a fresh read-only fid from root. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 44u);
    nx_store_le16(body + 8u, 2u);
    nx_store_le16(body + 10u, 4u);
    memcpy(body + 12u, "fuzz", 4u);
    nx_store_le16(body + 16u, 5u);
    memcpy(body + 18u, "data0", 5u);
    CHECK(roundtrip(P9_TWALK, body, 23u, response, &received));
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 44u);
    CHECK(roundtrip(P9_TOPEN, body, 5u, response, &received));
    CHECK(nx_load_le32(response + 8u) == (uint32_t)original.revision);
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 44u);
    nx_store_le32(body + 12u, 5u);
    CHECK(roundtrip(P9_TREAD, body, 16u, response, &received));
    CHECK(received == 16u && nx_load_le32(response + 7u) == 5u);
    CHECK(memcmp(response + 11u, "hello", 5u) == 0);
    /* Same offset, same content. 9P read offset must NOT use shared fd cursor. */
    CHECK(roundtrip(P9_TREAD, body, 16u, response, &received));
    CHECK(received == 16u && nx_load_le32(response + 7u) == 5u);
    CHECK(memcmp(response + 11u, "hello", 5u) == 0);

    /* Seek is part of the P9 contract; reading from offset two is not a
     * fresh VFS open and must return exactly the suffix requested. */
    nx_store_le64(body + 4u, 2u);
    nx_store_le32(body + 12u, 3u);
    CHECK(roundtrip(P9_TREAD, body, 16u, response, &received));
    CHECK(received == 14u && nx_load_le32(response + 7u) == 3u);
    CHECK(memcmp(response + 11u, "llo", 3u) == 0);

    /* A read-only fid has NO write authority. Reject, do not claim a
     * zero-byte successful write or leak a mutable registry reference. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 44u);
    nx_store_le32(body + 12u, 1u);
    body[16] = 'X';
    CHECK(!roundtrip(P9_TWRITE, body, 17u, response, &received));

    /* Independent fid 45: write to offset 1 of existing content and
     * verify that the canonical file object is mutated, not duplicated. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 45u);
    nx_store_le16(body + 8u, 2u);
    nx_store_le16(body + 10u, 4u);
    memcpy(body + 12u, "fuzz", 4u);
    nx_store_le16(body + 16u, 5u);
    memcpy(body + 18u, "data0", 5u);
    CHECK(roundtrip(P9_TWALK, body, 23u, response, &received));
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 45u);
    body[4] = 1u;
    CHECK(roundtrip(P9_TOPEN, body, 5u, response, &received));
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 45u);
    nx_store_le64(body + 4u, 1u);
    nx_store_le32(body + 12u, 2u);
    memcpy(body + 16u, "AX", 2u);
    CHECK(roundtrip(P9_TWRITE, body, 18u, response, &received));
    CHECK(received == 11u && nx_load_le32(response + 7u) == 2u);

    memset(body, 0, sizeof(body));
    nx_store_le32(body, 44u);
    nx_store_le32(body + 12u, 5u);
    CHECK(roundtrip(P9_TREAD, body, 16u, response, &received));
    CHECK(received == 16u && nx_load_le32(response + 7u) == 5u);
    CHECK(memcmp(response + 11u, "hAXlo", 5u) == 0);
    CHECK(reg_object_read("/fuzz/data0", &updated,
                          &state_result) == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(original.ref, updated.ref) &&
          original.revision + 1u == updated.revision);
    /* A new Topen MUST expose the revised version of the SAME file
     * object; a separately versioned /data child would fail this. */
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 42u);
    nx_store_le32(body + 4u, 46u);
    nx_store_le16(body + 8u, 2u);
    nx_store_le16(body + 10u, 4u);
    memcpy(body + 12u, "fuzz", 4u);
    nx_store_le16(body + 16u, 5u);
    memcpy(body + 18u, "data0", 5u);
    CHECK(roundtrip(P9_TWALK, body, 23u, response, &received));
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 46u);
    CHECK(roundtrip(P9_TOPEN, body, 5u, response, &received));
    CHECK(nx_load_le64(response + 12u) == updated.ref.id.low);
    CHECK(nx_load_le32(response + 8u) == (uint32_t)updated.revision);
    memset(body, 0, sizeof(body));
    nx_store_le32(body, 46u);
    CHECK(roundtrip(P9_TCLUNK, body, 4u, response, &received));
    CHECK(test_random_9p_commits(original.ref) == 1);
    CHECK(test_adversarial_denied_wire() == 1);
    CHECK(test_canonical_named_file_adversarial() == 1);
    CHECK(test_p9_dynamic_generational_fids() == 1);
    printf("REAL hosted NEXS runtime -> VFS -> NXOLMO registry -> "
           "9P wire read/write offsets, identity: PASS\n");
    return 0;
}
