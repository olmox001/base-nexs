/* Product-owned independent NXOLMO regression test port. */
#include "nexs_nxreg.h"
#include "nexs_state_store.h"
#include "nexs_operation_ids.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

struct nxreg_auth_context {
    uint64_t marker;
};

typedef struct nxreg_test_authority {
    struct nxreg_auth_context caller;
    uint64_t denied_operation;
    _Atomic uint64_t authorization_count;
    uint64_t fuel_to_consume;
} nxreg_test_authority;

static nx_status authorize_test_call(
    void *context, const nxreg_auth_context *caller,
    uint64_t operation_id, nx_object_ref target, uint64_t required_rights,
    nx_operation_budget *budget) {
    nxreg_test_authority *authority = context;
    if (authority == NULL || caller != &authority->caller ||
        !nx_object_ref_is_valid(target))
        return NX_STATUS_ACCESS_DENIED;
    (void)atomic_fetch_add_explicit(&authority->authorization_count, 1u,
                                    memory_order_relaxed);
    {
        nx_status status = nx_operation_budget_consume(
            budget, authority->fuel_to_consume);
        if (status != NX_STATUS_OK)
            return status;
    }
    if (operation_id == authority->denied_operation)
        return NX_STATUS_ACCESS_DENIED;
    if (required_rights != NXREG_RIGHT_READ &&
        required_rights != NXREG_RIGHT_ENUMERATE)
        return NX_STATUS_ACCESS_DENIED;
    return NX_STATUS_OK;
}

static nx_state_node_spec make_spec(uint64_t type_id) {
    nx_state_node_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type_id = type_id;
    spec.schema_id = 1u;
    spec.schema_version = 1u;
    spec.lifecycle = NX_STATE_LIFECYCLE_LIVE;
    return spec;
}

static int test_api_read_and_tree(void) {
    nx_state_store store;
    nx_state_slot slots[8];
    uint32_t index[16], rehash_index[16];
    uint64_t key[2] = {UINT64_C(0x12345678), UINT64_C(0xabcdef01)};
    nx_state_node_spec spec = make_spec(1u);
    nx_object_ref root, child, first, next;
    nx_state_node node;
    nx_state_store_stats stats;
    nxreg_api_v1 api;
    nxreg_state_store_adapter adapter;
    nxreg_authorization_fuel_limits fuel_limits = {2u, 2u, 1u, 2u};
    nxreg_client client;
    nxreg_test_authority authority;
    nx_result result;
    uint32_t count = 0u;
    memset(&authority, 0, sizeof(authority));
    authority.caller.marker = UINT64_C(0x4e58524547415554);
    authority.fuel_to_consume = 1u;
    CHECK(nx_state_store_init(&store, slots, 8u, index, rehash_index, 16u,
                              UINT64_C(0x4e58524547), key, &result) == NX_STATUS_OK);
    CHECK(nx_state_store_create(&store, &spec, &root, &result) == NX_STATUS_OK);
    spec = make_spec(2u);
    spec.parent = root;
    CHECK(nx_state_store_create(&store, &spec, &child, &result) == NX_STATUS_OK);
    CHECK(nxreg_state_store_adapter_init(&api, &adapter, &store,
                                         authorize_test_call, &authority,
                                         &fuel_limits) == NX_STATUS_OK);
    CHECK(nxreg_api_validate(&api, 0u) == NX_STATUS_OK);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_OBJECT_READ |
                            NXREG_FEATURE_TREE_RELATIONSHIPS) == NX_STATUS_OK);
    {
        nx_status status = nxreg_client_object_read(
            &client, &authority.caller, root, &node, &result);
        if (status != NX_STATUS_OK)
            fprintf(stderr, "NXREG object read status=%s result=%s fuel=%llu\n",
                    nx_status_name(status), nx_status_name(result.status),
                    (unsigned long long)result.fuel_used);
        CHECK(status == NX_STATUS_OK);
    }
    CHECK(nx_object_ref_equal(node.ref, root) != 0);
    CHECK(result.fuel_used > 1u);
    CHECK(nxreg_client_child_count(&client, &authority.caller, root,
                                   &count, &result) == NX_STATUS_OK);
    CHECK(count == 1u);
    CHECK(nxreg_client_child_first(&client, &authority.caller, root,
                                   &first, &result) == NX_STATUS_OK);
    CHECK(nx_object_ref_equal(first, child) != 0);
    CHECK(nxreg_client_child_next(&client, &authority.caller, root, child,
                                  &next, &result) == NX_STATUS_NOT_FOUND);
    CHECK(result.fuel_used > 1u);
    authority.fuel_to_consume = 2u;
    CHECK(nxreg_client_child_next(&client, &authority.caller, root, child,
                                  &next, &result) == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(result.fuel_used == 1u);
    CHECK(atomic_load_explicit(&authority.authorization_count, memory_order_relaxed) == 5u);
    CHECK(nx_state_store_stats_read(&store, &stats, &result) == NX_STATUS_OK);
    CHECK(stats.count == 2u);
    return 0;
}

static int test_authority_and_feature_failures(void) {
    nx_state_store store;
    nx_state_slot slots[4];
    uint32_t index[8], rehash_index[8];
    uint64_t key[2] = {UINT64_C(0x11112222), UINT64_C(0x33334444)};
    nx_state_node_spec spec = make_spec(1u);
    nx_object_ref root;
    nxreg_api_v1 api, altered;
    nxreg_state_store_adapter adapter;
    nxreg_authorization_fuel_limits fuel_limits = {2u, 2u, 1u, 2u};
    nxreg_client client, unchanged;
    nxreg_test_authority authority;
    nx_state_node node;
    nx_result result;
    memset(&authority, 0, sizeof(authority));
    authority.caller.marker = UINT64_C(0x415554484f524954);
    authority.fuel_to_consume = 1u;
    CHECK(nx_state_store_init(&store, slots, 4u, index, rehash_index, 8u,
                              UINT64_C(0x4e58524547), key, &result) == NX_STATUS_OK);
    CHECK(nx_state_store_create(&store, &spec, &root, &result) == NX_STATUS_OK);
    {
        nxreg_authorization_fuel_limits invalid_limits = {2u, 0u, 1u, 2u};
        CHECK(nxreg_state_store_adapter_init(
            &api, &adapter, &store, authorize_test_call, &authority,
            &invalid_limits) == NX_STATUS_INVALID_ARGUMENT);
    }
    CHECK(nxreg_state_store_adapter_init(&api, &adapter, &store,
                                         authorize_test_call, &authority,
                                         &fuel_limits) == NX_STATUS_OK);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_OBJECT_READ) == NX_STATUS_OK);
    CHECK(nxreg_client_object_read(&client, NULL, root, &node, &result) == NX_STATUS_ACCESS_DENIED);
    CHECK(result.domain == NX_RESULT_DOMAIN_SECURITY);
    CHECK(result.fuel_used == 0u);
    authority.denied_operation = NX_OP_NXREG_CLIENT_OBJECT_READ;
    CHECK(nxreg_client_object_read(&client, &authority.caller, root,
                                   &node, &result) == NX_STATUS_ACCESS_DENIED);
    CHECK(result.fuel_used == 1u);
    memset(&unchanged, 0xa5, sizeof(unchanged));
    client = unchanged;
    CHECK(nxreg_client_bind(&client, &api, 1u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_OBJECT_READ) == NX_STATUS_VERSION_MISMATCH);
    CHECK(memcmp(&client, &unchanged, sizeof(client)) == 0);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_OBJECT_READ,
                            NXREG_FEATURE_OBJECT_READ |
                            NXREG_FEATURE_TREE_RELATIONSHIPS) == NX_STATUS_NOT_SUPPORTED);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_OBJECT_READ) == NX_STATUS_OK);
    authority.denied_operation = 0u;
    authority.fuel_to_consume = 3u;
    CHECK(nxreg_client_object_read(&client, &authority.caller, root,
                                   &node, &result) == NX_STATUS_BUDGET_EXHAUSTED);
    CHECK(result.fuel_used == 2u);
    authority.fuel_to_consume = 1u;
    altered = api;
    altered.abi_major++;
    CHECK(nxreg_api_validate(&altered, 0u) == NX_STATUS_VERSION_MISMATCH);
    altered = api;
    altered.struct_size = (uint32_t)(NXREG_API_V1_MIN_SIZE - 1u);
    CHECK(nxreg_api_validate(&altered, 0u) == NX_STATUS_VERSION_MISMATCH);
    altered = api;
    altered.feature_bits |= UINT64_C(1) << 63;
    altered.required_feature_bits = UINT64_C(1) << 63;
    CHECK(nxreg_api_validate(&altered, 0u) == NX_STATUS_NOT_SUPPORTED);
    altered = api;
    altered.feature_bits |= UINT64_C(1) << 62;
    CHECK(nxreg_api_validate(&altered, 0u) == NX_STATUS_OK);
    CHECK(nxreg_client_bind(&client, &altered, 0u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_OBJECT_READ) == NX_STATUS_OK);
    CHECK((client.feature_bits & (UINT64_C(1) << 62)) == 0u);
    altered = api;
    altered.child_next = NULL;
    CHECK(nxreg_api_validate(&altered, 0u) == NX_STATUS_MALFORMED);
    return 0;
}

#define NXREG_STRESS_THREAD_COUNT 8u
#define NXREG_STRESS_PER_THREAD UINT64_C(125000)
#define NXREG_STRESS_OPERATIONS \
    ((uint64_t)NXREG_STRESS_THREAD_COUNT * NXREG_STRESS_PER_THREAD)

typedef struct nxreg_read_stress_shared {
    const nxreg_client *client;
    const nxreg_auth_context *caller;
    nx_object_ref object;
    _Atomic uint32_t failed;
} nxreg_read_stress_shared;

typedef struct nxreg_read_stress_argument {
    nxreg_read_stress_shared *shared;
    uint32_t thread_index;
    uint64_t completed;
    uint64_t attempts;
    uint64_t retries;
} nxreg_read_stress_argument;

static void *nxreg_read_stress_worker(void *opaque) {
    nxreg_read_stress_argument *argument = opaque;
    while (argument->completed < NXREG_STRESS_PER_THREAD) {
        nx_state_node node;
        nx_result result;
        nx_status status = nxreg_client_object_read(
            argument->shared->client, argument->shared->caller,
            argument->shared->object, &node, &result);
        argument->attempts++;
        if (status == NX_STATUS_BUSY) {
            argument->retries++;
            (void)sched_yield();
            continue;
        }
        if (status != NX_STATUS_OK ||
            nx_object_ref_equal(node.ref, argument->shared->object) == 0 ||
            result.operation_id != NX_OP_NXREG_CLIENT_OBJECT_READ ||
            result.status != status || result.fuel_used < 2u) {
            (void)fprintf(stderr,
                          "NXREG direct reader failure thread=%u completed=%llu attempts=%llu status=%s result=%s fuel=%llu\n",
                          (unsigned int)argument->thread_index,
                          (unsigned long long)argument->completed,
                          (unsigned long long)argument->attempts,
                          nx_status_name(status), nx_status_name(result.status),
                          (unsigned long long)result.fuel_used);
            (void)atomic_store_explicit(&argument->shared->failed, 1u,
                                        memory_order_release);
            return NULL;
        }
        argument->completed++;
    }
    return NULL;
}

static int test_million_concurrent_direct_reads(void) {
    nx_state_store store;
    nx_state_slot slots[4];
    uint32_t index[8], rehash_index[8];
    const uint64_t key[2] = {UINT64_C(0x4e58524547535452),
                             UINT64_C(0x4553533030303031)};
    nx_state_node_spec spec = make_spec(UINT64_C(0x4e58524547524f4f));
    nx_object_ref root;
    nxreg_api_v1 api;
    nxreg_state_store_adapter adapter;
    nxreg_authorization_fuel_limits limits = {2u, 2u, 1u, 2u};
    nxreg_client client;
    nxreg_test_authority authority;
    nxreg_read_stress_shared shared;
    nxreg_read_stress_argument arguments[NXREG_STRESS_THREAD_COUNT];
    pthread_t threads[NXREG_STRESS_THREAD_COUNT];
    nx_result result;
    uint64_t completed = 0u, attempts = 0u, retries = 0u;
    uint32_t created = 0u, joined = 0u, thread_index;
    memset(&authority, 0, sizeof(authority));
    authority.caller.marker = UINT64_C(0x5354524553533031);
    authority.fuel_to_consume = 1u;
    CHECK(nx_state_store_init(&store, slots, 4u, index, rehash_index, 8u,
                              UINT64_C(0x4e58524547535452), key,
                              &result) == NX_STATUS_OK);
    CHECK(nx_state_store_create(&store, &spec, &root, &result) == NX_STATUS_OK);
    CHECK(nxreg_state_store_adapter_init(&api, &adapter, &store,
                                         authorize_test_call, &authority,
                                         &limits) == NX_STATUS_OK);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_OBJECT_READ,
                            NXREG_FEATURE_OBJECT_READ) == NX_STATUS_OK);
    memset(&shared, 0, sizeof(shared));
    shared.client = &client;
    shared.caller = &authority.caller;
    shared.object = root;
    for (thread_index = 0u; thread_index < NXREG_STRESS_THREAD_COUNT;
         ++thread_index) {
        arguments[thread_index].shared = &shared;
        arguments[thread_index].thread_index = thread_index;
        arguments[thread_index].completed = 0u;
        arguments[thread_index].attempts = 0u;
        arguments[thread_index].retries = 0u;
        if (pthread_create(&threads[thread_index], NULL,
                           nxreg_read_stress_worker,
                           &arguments[thread_index]) != 0)
            break;
        created++;
    }
    for (thread_index = 0u; thread_index < created; ++thread_index) {
        if (pthread_join(threads[thread_index], NULL) != 0)
            return 1;
        joined++;
        CHECK(arguments[thread_index].completed == NXREG_STRESS_PER_THREAD);
        completed += arguments[thread_index].completed;
        attempts += arguments[thread_index].attempts;
        retries += arguments[thread_index].retries;
    }
    CHECK(created == NXREG_STRESS_THREAD_COUNT);
    CHECK(joined == NXREG_STRESS_THREAD_COUNT);
    CHECK(atomic_load_explicit(&shared.failed, memory_order_acquire) == 0u);
    CHECK(completed == NXREG_STRESS_OPERATIONS);
    CHECK(atomic_load_explicit(&authority.authorization_count,
                               memory_order_relaxed) == attempts);
    CHECK(attempts == completed + retries);
    CHECK(nx_state_store_validate(&store, &result) == NX_STATUS_OK);
    printf("NXREG concurrent direct-read: threads=%u per_thread=%llu completed=%llu attempts=%llu retries=%llu authorized=%llu\n",
           (unsigned int)joined,
           (unsigned long long)NXREG_STRESS_PER_THREAD,
           (unsigned long long)completed,
           (unsigned long long)attempts,
           (unsigned long long)retries,
           (unsigned long long)atomic_load_explicit(
               &authority.authorization_count, memory_order_relaxed));
    return 0;
}

typedef struct nxreg_dispatch_stress_shared {
    const nxreg_client *client;
    const nxreg_auth_context *caller;
    nx_object_ref root;
    nx_object_ref child;
    _Atomic uint32_t failed;
} nxreg_dispatch_stress_shared;

typedef struct nxreg_dispatch_stress_argument {
    nxreg_dispatch_stress_shared *shared;
    uint32_t thread_index;
    uint64_t completed;
    uint64_t attempts;
    uint64_t retries;
    uint64_t codec_round_trips;
    uint64_t operation_counts[4];
} nxreg_dispatch_stress_argument;

static void *nxreg_dispatch_stress_worker(void *opaque) {
    nxreg_dispatch_stress_argument *argument = opaque;
    nxreg_dispatch_stress_shared *shared = argument->shared;
    nxreg_dispatch_request request;
    nx_state_node node;
    nx_object_ref output, next;
    nx_result result;
    uint8_t encoded[8];
    uint64_t decoded;
    uint32_t count;
    memset(&request, 0, sizeof(request));
    request.client = shared->client;
    request.caller = shared->caller;
    request.object = shared->root;
    request.parent = shared->root;
    request.result = &result;
    while (argument->completed < NXREG_STRESS_PER_THREAD) {
        uint32_t selected = (uint32_t)(argument->completed & UINT64_C(3));
        uint64_t operation_id;
        nx_status status;
        int operation_valid = 0;
        switch (selected) {
        case 0u: operation_id = NX_OP_NXREG_CLIENT_OBJECT_READ; break;
        case 1u: operation_id = NX_OP_NXREG_CLIENT_CHILD_FIRST; break;
        case 2u: operation_id = NX_OP_NXREG_CLIENT_CHILD_NEXT; break;
        case 3u: operation_id = NX_OP_NXREG_CLIENT_CHILD_COUNT; break;
        default:
            (void)atomic_store_explicit(&shared->failed, 1u,
                                        memory_order_release);
            return NULL;
        }
        if (nxreg_opcode_encode(operation_id, encoded) != NX_STATUS_OK ||
            nxreg_opcode_decode(encoded, &decoded) != NX_STATUS_OK ||
            decoded != operation_id) {
            (void)atomic_store_explicit(&shared->failed, 1u,
                                        memory_order_release);
            return NULL;
        }
        argument->codec_round_trips += 2u;
        argument->attempts++;
        request.operation_id = decoded;
        request.node = NULL;
        request.reference_output = NULL;
        request.count_output = NULL;
        request.current = shared->child;
        if (selected == 0u) {
            request.node = &node;
            status = nxreg_dispatch(&request);
            operation_valid =
                (status == NX_STATUS_BUSY) ||
                (status == NX_STATUS_OK &&
                 nx_object_ref_equal(node.ref, shared->root) != 0);
        } else if (selected == 1u) {
            request.reference_output = &output;
            status = nxreg_dispatch(&request);
            operation_valid =
                (status == NX_STATUS_BUSY) ||
                (status == NX_STATUS_OK &&
                 nx_object_ref_equal(output, shared->child) != 0);
        } else if (selected == 2u) {
            request.reference_output = &next;
            status = nxreg_dispatch(&request);
            operation_valid =
                (status == NX_STATUS_BUSY) ||
                (status == NX_STATUS_NOT_FOUND &&
                 result.status == NX_STATUS_NOT_FOUND);
        } else {
            request.count_output = &count;
            status = nxreg_dispatch(&request);
            operation_valid =
                (status == NX_STATUS_BUSY) ||
                (status == NX_STATUS_OK && count == 1u);
        }
        if (!operation_valid || result.operation_id != operation_id ||
            result.status != status) {
            (void)fprintf(stderr,
                          "NXREG dispatch worker failure thread=%u opcode=0x%llx status=%s result=%s\\n",
                          (unsigned int)argument->thread_index,
                          (unsigned long long)operation_id,
                          nx_status_name(status), nx_status_name(result.status));
            (void)atomic_store_explicit(&shared->failed, 1u,
                                        memory_order_release);
            return NULL;
        }
        if (status == NX_STATUS_BUSY) {
            argument->retries++;
            (void)sched_yield();
            continue;
        }
        argument->operation_counts[selected]++;
        argument->completed++;
    }
    return NULL;
}

static int test_million_concurrent_binary_dispatches(void) {
    nx_state_store store;
    nx_state_slot slots[8];
    uint32_t index[16], rehash_index[16];
    const uint64_t key[2] = {UINT64_C(0x4e58524547444953),
                             UINT64_C(0x5041544348303031)};
    nx_state_node_spec spec = make_spec(UINT64_C(0x4e58524547524f4f));
    nx_object_ref root, child;
    nxreg_api_v1 api;
    nxreg_state_store_adapter adapter;
    nxreg_authorization_fuel_limits limits = {2u, 2u, 1u, 2u};
    nxreg_client client;
    nxreg_test_authority authority;
    nxreg_dispatch_stress_shared shared;
    nxreg_dispatch_stress_argument arguments[NXREG_STRESS_THREAD_COUNT];
    pthread_t threads[NXREG_STRESS_THREAD_COUNT];
    nx_result result;
    uint8_t encoded[8];
    uint64_t invalid_decoded = 0u;
    uint64_t completed = 0u, attempts = 0u, retries = 0u;
    uint64_t codec_round_trips = 0u;
    uint32_t created = 0u, joined = 0u, thread_index;
    memset(&authority, 0, sizeof(authority));
    authority.caller.marker = UINT64_C(0x4449535041544348);
    authority.fuel_to_consume = 1u;
    CHECK(nx_state_store_init(&store, slots, 8u, index, rehash_index, 16u,
                              UINT64_C(0x4e58524547444953), key,
                              &result) == NX_STATUS_OK);
    CHECK(nx_state_store_create(&store, &spec, &root, &result) == NX_STATUS_OK);
    spec.parent = root;
    spec.type_id++;
    CHECK(nx_state_store_create(&store, &spec, &child, &result) == NX_STATUS_OK);
    CHECK(nxreg_state_store_adapter_init(&api, &adapter, &store,
                                         authorize_test_call, &authority,
                                         &limits) == NX_STATUS_OK);
    CHECK(nxreg_client_bind(&client, &api, 0u, NXREG_FEATURE_KNOWN_MASK,
                            NXREG_FEATURE_KNOWN_MASK) == NX_STATUS_OK);
    memset(&shared, 0, sizeof(shared));
    shared.client = &client;
    shared.caller = &authority.caller;
    shared.root = root;
    shared.child = child;
    for (thread_index = 0u; thread_index < NXREG_STRESS_THREAD_COUNT;
         ++thread_index) {
        memset(&arguments[thread_index], 0, sizeof(arguments[thread_index]));
        arguments[thread_index].shared = &shared;
        arguments[thread_index].thread_index = thread_index;
        if (pthread_create(&threads[thread_index], NULL,
                           nxreg_dispatch_stress_worker,
                           &arguments[thread_index]) != 0)
            break;
        created++;
    }
    for (thread_index = 0u; thread_index < created; ++thread_index) {
        if (pthread_join(threads[thread_index], NULL) != 0)
            return 1;
        joined++;
        completed += arguments[thread_index].completed;
        attempts += arguments[thread_index].attempts;
        retries += arguments[thread_index].retries;
        codec_round_trips += arguments[thread_index].codec_round_trips;
    }
    CHECK(created == NXREG_STRESS_THREAD_COUNT);
    CHECK(joined == NXREG_STRESS_THREAD_COUNT);
    CHECK(atomic_load_explicit(&shared.failed, memory_order_acquire) == 0u);
    CHECK(completed == NXREG_STRESS_OPERATIONS);
    CHECK(atomic_load_explicit(&authority.authorization_count,
                               memory_order_relaxed) == attempts);
    CHECK(attempts == completed + retries);
    CHECK(codec_round_trips == attempts * 2u);
    {
        uint64_t operation_totals[4] = {0u, 0u, 0u, 0u};
        for (thread_index = 0u; thread_index < NXREG_STRESS_THREAD_COUNT;
             ++thread_index) {
            uint32_t operation_index;
            CHECK(arguments[thread_index].completed == NXREG_STRESS_PER_THREAD);
            for (operation_index = 0u; operation_index < 4u; ++operation_index)
                operation_totals[operation_index] +=
                    arguments[thread_index].operation_counts[operation_index];
        }
        CHECK(operation_totals[0] == NXREG_STRESS_OPERATIONS / 4u);
        CHECK(operation_totals[1] == NXREG_STRESS_OPERATIONS / 4u);
        CHECK(operation_totals[2] == NXREG_STRESS_OPERATIONS / 4u);
        CHECK(operation_totals[3] == NXREG_STRESS_OPERATIONS / 4u);
    }
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(nxreg_opcode_encode(UINT64_C(0x000b0001), encoded) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(encoded, (const uint8_t[8]){0u}, sizeof(encoded)) == 0);
    memset(encoded, 0xa5, sizeof(encoded));
    CHECK(nxreg_opcode_encode(UINT64_C(0x000d00ff), encoded) ==
          NX_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(encoded, (const uint8_t[8]){0u}, sizeof(encoded)) == 0);
    nx_store_le64(encoded, UINT64_C(0x000000010000d001));
    CHECK(nxreg_opcode_decode(encoded, &invalid_decoded) == NX_STATUS_NOT_SUPPORTED);
    CHECK(invalid_decoded == 0u);
    CHECK(nxreg_opcode_decode(encoded, (uint64_t *)(void *)encoded) ==
          NX_STATUS_INVALID_ARGUMENT);
    {
        _Alignas(uint64_t) uint8_t unaligned_storage[
            sizeof(uint64_t) + _Alignof(uint64_t)];
        CHECK(nxreg_opcode_decode(encoded,
              (uint64_t *)(void *)(unaligned_storage + 1u)) ==
              NX_STATUS_INVALID_ARGUMENT);
    }
    CHECK(nx_state_store_validate(&store, &result) == NX_STATUS_OK);
    printf("NXREG concurrent binary-dispatch: threads=%u per_thread=%llu completed=%llu attempts=%llu retries=%llu authorized=%llu (each opcode=250000; codec round-trips=%llu)\n",
           (unsigned int)joined,
           (unsigned long long)NXREG_STRESS_PER_THREAD,
           (unsigned long long)completed,
           (unsigned long long)attempts,
           (unsigned long long)retries,
           (unsigned long long)atomic_load_explicit(
               &authority.authorization_count, memory_order_relaxed),
           (unsigned long long)codec_round_trips);
    return 0;
}

int main(void) {
    CHECK(test_api_read_and_tree() == 0);
    CHECK(test_authority_and_feature_failures() == 0);
    CHECK(test_million_concurrent_direct_reads() == 0);
    CHECK(test_million_concurrent_binary_dispatches() == 0);
    puts("NXREG versioned API, binary codec/dispatcher and million-operation stress tests: PASS");
    return 0;
}
