/* NXOLMO registry algorithm port; one product-owned implementation. */
#include "nexs_nxreg.h"

#include "nexs_checked.h"
#include "nexs_budget.h"
#include "nexs_operation_catalog.h"
#include "nexs_state_store.h"

#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

static nx_status nxreg_result_error(nx_result *result, nx_status status,
                                    uint64_t operation_id) {
    if (result != NULL)
        (void)nx_result_init(
            result, status, status == NX_STATUS_ACCESS_DENIED
                ? NX_RESULT_DOMAIN_SECURITY : NX_RESULT_DOMAIN_REGISTRY,
            operation_id, 0u, 0u, NX_CYCLES_UNKNOWN, 0u);
    return status;
}

static uint64_t nxreg_authorization_limit(
    const nxreg_state_store_adapter *adapter, uint64_t operation_id) {
    switch (operation_id) {
    case NX_OP_NXREG_CLIENT_OBJECT_READ:
        return adapter->authorization_fuel_limits.object_read;
    case NX_OP_NXREG_CLIENT_CHILD_FIRST:
        return adapter->authorization_fuel_limits.child_first;
    case NX_OP_NXREG_CLIENT_CHILD_NEXT:
        return adapter->authorization_fuel_limits.child_next;
    case NX_OP_NXREG_CLIENT_CHILD_COUNT:
        return adapter->authorization_fuel_limits.child_count;
    default:
        return 0u;
    }
}

static nx_status nxreg_adapter_authorize(
    nxreg_state_store_adapter *adapter, const nxreg_auth_context *caller,
    uint64_t operation_id, nx_object_ref target, uint64_t rights,
    nx_operation_measurement *measurement, nx_result *result) {
    nx_operation_budget budget;
    uint64_t fuel_limit;
    nx_status status, finish_status;
    if (adapter == NULL ||
        ((uintptr_t)adapter % _Alignof(nxreg_state_store_adapter)) != 0u ||
        adapter->store == NULL || adapter->authorize == NULL)
        return nxreg_result_error(result, NX_STATUS_UNAVAILABLE, operation_id);
    fuel_limit = nxreg_authorization_limit(adapter, operation_id);
    if (fuel_limit == 0u)
        return nxreg_result_error(result, NX_STATUS_NOT_SUPPORTED, operation_id);
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED, operation_id);
    if (!nx_object_ref_is_valid(target) || measurement == NULL)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  operation_id);
    status = nx_operation_budget_init(&budget, operation_id,
                                      fuel_limit,
                                      UINT64_MAX);
    if (status != NX_STATUS_OK)
        return nxreg_result_error(result, status, operation_id);
    status = adapter->authorize(adapter->authorize_context, caller,
                                operation_id, target, rights, &budget);
    finish_status = nx_operation_budget_finish(&budget, measurement);
    if (finish_status != NX_STATUS_OK)
        status = finish_status;
    if (status == NX_STATUS_OK && measurement->fuel_used == 0u)
        status = NX_STATUS_CORRUPT;
    if (status != NX_STATUS_OK && result != NULL)
        (void)nx_result_init(result, status, NX_RESULT_DOMAIN_SECURITY,
                             operation_id, 0u, 0u, measurement->cycles_used,
                             measurement->fuel_used);
    return status;
}

static nx_status nxreg_merge_operation_result(
    nx_result *result, uint64_t operation_id, nx_status status,
    const nx_result *store_result,
    const nx_operation_measurement *authorization) {
    uint64_t fuel_used;
    uint64_t cycles_used = NX_CYCLES_UNKNOWN;
    if (nx_u64_add(authorization->fuel_used, store_result->fuel_used,
                   &fuel_used) != NX_STATUS_OK)
        return nxreg_result_error(result, NX_STATUS_OVERFLOW, operation_id);
    if (authorization->cycles_used != NX_CYCLES_UNKNOWN &&
        store_result->cycles_used != NX_CYCLES_UNKNOWN &&
        nx_u64_add(authorization->cycles_used, store_result->cycles_used,
                   &cycles_used) != NX_STATUS_OK)
        return nxreg_result_error(result, NX_STATUS_OVERFLOW, operation_id);
    {
        nx_status result_status = nx_result_init(
            result, status, status == NX_STATUS_ACCESS_DENIED
                ? NX_RESULT_DOMAIN_SECURITY : NX_RESULT_DOMAIN_REGISTRY,
            operation_id, store_result->completed, store_result->detail,
            cycles_used, fuel_used);
        return result_status == NX_STATUS_OK ? status : result_status;
    }
}

static nx_status nxreg_store_object_read(
    void *context, const nxreg_auth_context *caller, nx_object_ref object,
    nx_state_node *node, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement authorization;
    nx_result store_result;
    nx_status status;
    if (result == NULL || node == NULL)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_OBJECT_READ);
    status = nxreg_adapter_authorize(
        adapter, caller, NX_OP_NXREG_CLIENT_OBJECT_READ, object, NXREG_RIGHT_READ,
        &authorization, result);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_read(adapter->store, object, node, &store_result);
    return nxreg_merge_operation_result(result, NX_OP_NXREG_CLIENT_OBJECT_READ, status,
                                        &store_result, &authorization);
}

static nx_status nxreg_store_child_first(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    nx_object_ref *child, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement authorization;
    nx_result store_result;
    nx_status status;
    if (result == NULL || child == NULL)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_FIRST);
    status = nxreg_adapter_authorize(
        adapter, caller, NX_OP_NXREG_CLIENT_CHILD_FIRST, parent, NXREG_RIGHT_ENUMERATE,
        &authorization, result);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_child_first(adapter->store, parent, child,
                                        &store_result);
    return nxreg_merge_operation_result(result, NX_OP_NXREG_CLIENT_CHILD_FIRST, status,
                                        &store_result, &authorization);
}

static nx_status nxreg_store_child_next(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    nx_object_ref current, nx_object_ref *next, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement authorization;
    nx_result store_result;
    nx_status status;
    if (result == NULL || next == NULL || !nx_object_ref_is_valid(current))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_NEXT);
    status = nxreg_adapter_authorize(
        adapter, caller, NX_OP_NXREG_CLIENT_CHILD_NEXT, parent, NXREG_RIGHT_ENUMERATE,
        &authorization, result);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_child_next(adapter->store, parent, current, next,
                                       &store_result);
    return nxreg_merge_operation_result(result, NX_OP_NXREG_CLIENT_CHILD_NEXT, status,
                                        &store_result, &authorization);
}

static nx_status nxreg_store_child_count(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    uint32_t *count, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement authorization;
    nx_result store_result;
    nx_status status;
    if (result == NULL || count == NULL)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_COUNT);
    status = nxreg_adapter_authorize(
        adapter, caller, NX_OP_NXREG_CLIENT_CHILD_COUNT, parent, NXREG_RIGHT_ENUMERATE,
        &authorization, result);
    if (status != NX_STATUS_OK)
        return status;
    status = nx_state_store_child_count(adapter->store, parent, count,
                                        &store_result);
    return nxreg_merge_operation_result(result, NX_OP_NXREG_CLIENT_CHILD_COUNT, status,
                                        &store_result, &authorization);
}

static int nxreg_opcode_is_assigned(uint64_t operation_id) {
    const nx_operation_contract *contract;
    if ((operation_id >> 32u) != 0u ||
        ((operation_id >> 16u) & UINT64_C(0xffff)) != UINT64_C(0x000d) ||
        (operation_id & UINT64_C(0xffff)) == 0u)
        return 0;
    contract = nx_operation_catalog_find(operation_id);
    return contract != NULL;
}

nx_status nxreg_opcode_encode(uint64_t operation_id, uint8_t encoded[8]) {
    if (encoded == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    memset(encoded, 0, 8u);
    if (!nxreg_opcode_is_assigned(operation_id))
        return NX_STATUS_INVALID_ARGUMENT;
    nx_store_le64(encoded, operation_id);
    return NX_STATUS_OK;
}

nx_status nxreg_opcode_decode(const uint8_t encoded[8],
                              uint64_t *operation_id) {
    uint64_t decoded;
    if (operation_id == NULL || encoded == NULL ||
        ((uintptr_t)operation_id % _Alignof(uint64_t)) != 0u ||
        nx_pointer_ranges_overlap(encoded, 8u, operation_id,
                                  sizeof(*operation_id)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    *operation_id = 0u;
    decoded = nx_load_le64(encoded);
    if (!nxreg_opcode_is_assigned(decoded))
        return NX_STATUS_NOT_SUPPORTED;
    *operation_id = decoded;
    return NX_STATUS_OK;
}

nx_status nxreg_dispatch(const nxreg_dispatch_request *request) {
    if (request == NULL ||
        ((uintptr_t)request % _Alignof(nxreg_dispatch_request)) != 0u ||
        request->client == NULL || request->caller == NULL ||
        request->result == NULL)
        return NX_STATUS_INVALID_ARGUMENT;
    switch (request->operation_id) {
    case NX_OP_NXREG_CLIENT_OBJECT_READ:
        if (request->node == NULL)
            return nxreg_result_error(request->result,
                                      NX_STATUS_INVALID_ARGUMENT,
                                      request->operation_id);
        return nxreg_client_object_read(request->client, request->caller,
                                        request->object, request->node,
                                        request->result);
    case NX_OP_NXREG_CLIENT_CHILD_FIRST:
        if (request->reference_output == NULL)
            return nxreg_result_error(request->result,
                                      NX_STATUS_INVALID_ARGUMENT,
                                      request->operation_id);
        return nxreg_client_child_first(request->client, request->caller,
                                        request->parent,
                                        request->reference_output,
                                        request->result);
    case NX_OP_NXREG_CLIENT_CHILD_NEXT:
        if (request->reference_output == NULL)
            return nxreg_result_error(request->result,
                                      NX_STATUS_INVALID_ARGUMENT,
                                      request->operation_id);
        return nxreg_client_child_next(request->client, request->caller,
                                       request->parent, request->current,
                                       request->reference_output,
                                       request->result);
    case NX_OP_NXREG_CLIENT_CHILD_COUNT:
        if (request->count_output == NULL)
            return nxreg_result_error(request->result,
                                      NX_STATUS_INVALID_ARGUMENT,
                                      request->operation_id);
        return nxreg_client_child_count(request->client, request->caller,
                                        request->parent,
                                        request->count_output,
                                        request->result);
    default:
        return nxreg_result_error(request->result, NX_STATUS_NOT_SUPPORTED,
                                  request->operation_id);
    }
}

nx_status nxreg_api_validate(const nxreg_api_v1 *api,
                             uint16_t minimum_minor) {
    uint64_t features;
    if (api == NULL ||
        ((uintptr_t)api % _Alignof(nxreg_api_v1)) != 0u ||
        api->struct_size < NXREG_API_V1_MIN_SIZE ||
        api->abi_major != NXREG_ABI_MAJOR || api->abi_minor < minimum_minor)
        return NX_STATUS_VERSION_MISMATCH;
    if (api->reserved != 0u ||
        (api->required_feature_bits & ~api->feature_bits) != 0u)
        return NX_STATUS_MALFORMED;
    if ((api->required_feature_bits & ~NXREG_FEATURE_KNOWN_MASK) != 0u)
        return NX_STATUS_NOT_SUPPORTED;
    features = api->feature_bits;
    if ((features & NXREG_FEATURE_OBJECT_READ) != 0u &&
        api->object_read == NULL)
        return NX_STATUS_MALFORMED;
    if ((features & NXREG_FEATURE_TREE_RELATIONSHIPS) != 0u &&
        (api->child_first == NULL || api->child_next == NULL ||
         api->child_count == NULL ||
         (features & NXREG_FEATURE_OBJECT_READ) == 0u))
        return NX_STATUS_MALFORMED;
    return NX_STATUS_OK;
}

nx_status nxreg_client_bind(nxreg_client *client, const nxreg_api_v1 *api,
                            uint16_t minimum_minor,
                            uint64_t supported_features,
                            uint64_t required_features) {
    nxreg_client candidate;
    nx_status status;
    int overlap;
    if (client == NULL || api == NULL ||
        ((uintptr_t)client % _Alignof(nxreg_client)) != 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    overlap = nx_pointer_ranges_overlap(client, sizeof(*client), api,
                                        sizeof(*api));
    if (overlap != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    status = nxreg_api_validate(api, minimum_minor);
    if (status != NX_STATUS_OK)
        return status;
    supported_features &= NXREG_FEATURE_KNOWN_MASK;
    if ((required_features & ~NXREG_FEATURE_KNOWN_MASK) != 0u ||
        (required_features & ~supported_features) != 0u ||
        (api->required_feature_bits & ~supported_features) != 0u)
        return NX_STATUS_NOT_SUPPORTED;
    if ((required_features & ~api->feature_bits) != 0u)
        return NX_STATUS_UNAVAILABLE;
    candidate.api = api;
    candidate.feature_bits = api->feature_bits & supported_features &
                             NXREG_FEATURE_KNOWN_MASK;
    candidate.abi_major = api->abi_major;
    candidate.abi_minor = api->abi_minor;
    candidate.reserved = 0u;
    *client = candidate;
    return NX_STATUS_OK;
}

static nx_status nxreg_state_store_adapter_validate_storage(
    const nxreg_api_v1 *api, const nxreg_state_store_adapter *adapter,
    const nx_state_store *store,
    const nxreg_authorization_fuel_limits *authorization_fuel_limits,
    nxreg_authorize_fn authorize) {
    if (api == NULL || adapter == NULL || store == NULL || authorize == NULL ||
        authorization_fuel_limits == NULL ||
        ((uintptr_t)authorization_fuel_limits %
         _Alignof(nxreg_authorization_fuel_limits)) != 0u ||
        authorization_fuel_limits->object_read == 0u ||
        authorization_fuel_limits->child_first == 0u ||
        authorization_fuel_limits->child_next == 0u ||
        authorization_fuel_limits->child_count == 0u ||
        ((uintptr_t)api % _Alignof(nxreg_api_v1)) != 0u ||
        ((uintptr_t)adapter % _Alignof(nxreg_state_store_adapter)) != 0u ||
        ((uintptr_t)store % _Alignof(nx_state_store)) != 0u ||
        atomic_load_explicit(&store->initialized, memory_order_acquire) == 0u)
        return NX_STATUS_INVALID_ARGUMENT;
    if (nx_pointer_ranges_overlap(api, sizeof(*api), adapter,
                                  sizeof(*adapter)) != 0 ||
        nx_pointer_ranges_overlap(api, sizeof(*api), store, sizeof(*store)) != 0 ||
        nx_pointer_ranges_overlap(adapter, sizeof(*adapter), store,
                                  sizeof(*store)) != 0 ||
        nx_pointer_ranges_overlap(api, sizeof(*api), authorization_fuel_limits,
                                  sizeof(*authorization_fuel_limits)) != 0 ||
        nx_pointer_ranges_overlap(adapter, sizeof(*adapter),
                                  authorization_fuel_limits,
                                  sizeof(*authorization_fuel_limits)) != 0 ||
        nx_pointer_ranges_overlap(store, sizeof(*store), authorization_fuel_limits,
                                  sizeof(*authorization_fuel_limits)) != 0)
        return NX_STATUS_INVALID_ARGUMENT;
    return NX_STATUS_OK;
}

static void nxreg_state_store_adapter_publish_api(
    nxreg_api_v1 *api, nxreg_state_store_adapter *adapter) {
    nxreg_api_v1 candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = (uint32_t)sizeof(candidate);
    candidate.abi_major = NXREG_ABI_MAJOR;
    candidate.abi_minor = NXREG_ABI_MINOR;
    candidate.feature_bits = NXREG_FEATURE_OBJECT_READ |
                             NXREG_FEATURE_TREE_RELATIONSHIPS;
    candidate.context = adapter;
    candidate.object_read = nxreg_store_object_read;
    candidate.child_first = nxreg_store_child_first;
    candidate.child_next = nxreg_store_child_next;
    candidate.child_count = nxreg_store_child_count;
    *api = candidate;
}

nx_status nxreg_state_store_adapter_init(
    nxreg_api_v1 *api, nxreg_state_store_adapter *adapter,
    nx_state_store *store, nxreg_authorize_fn authorize,
    void *authorize_context,
    const nxreg_authorization_fuel_limits *authorization_fuel_limits) {
    nx_status status = nxreg_state_store_adapter_validate_storage(
        api, adapter, store, authorization_fuel_limits, authorize);
    if (status != NX_STATUS_OK)
        return status;
    adapter->store = store;
    adapter->authorize = authorize;
    adapter->authorize_context = authorize_context;
    adapter->authorization_fuel_limits = *authorization_fuel_limits;
    nxreg_state_store_adapter_publish_api(api, adapter);
    return NX_STATUS_OK;
}

static nx_status nxreg_client_validate_feature(
    const nxreg_client *client, uint64_t feature, uint64_t operation_id,
    nx_result *result) {
    if (client == NULL ||
        ((uintptr_t)client % _Alignof(nxreg_client)) != 0u ||
        client->api == NULL || client->abi_major != NXREG_ABI_MAJOR ||
        client->reserved != 0u)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  operation_id);
    {
        nx_status status = nxreg_api_validate(client->api, client->abi_minor);
        if (status != NX_STATUS_OK)
            return nxreg_result_error(result, status, operation_id);
    }
    if ((client->feature_bits & ~NXREG_FEATURE_KNOWN_MASK) != 0u ||
        (client->feature_bits & ~client->api->feature_bits) != 0u ||
        (client->feature_bits & feature) != feature)
        return nxreg_result_error(result, NX_STATUS_UNAVAILABLE, operation_id);
    return NX_STATUS_OK;
}

nx_status nxreg_client_object_read(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref object, nx_state_node *node, nx_result *result) {
    nx_status status = nxreg_client_validate_feature(
        client, NXREG_FEATURE_OBJECT_READ, NX_OP_NXREG_CLIENT_OBJECT_READ, result);
    if (status != NX_STATUS_OK)
        return status;
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED,
                                  NX_OP_NXREG_CLIENT_OBJECT_READ);
    if (result == NULL || node == NULL || !nx_object_ref_is_valid(object))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_OBJECT_READ);
    return client->api->object_read(client->api->context, caller, object,
                                    node, result);
}

nx_status nxreg_client_child_first(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref *child, nx_result *result) {
    nx_status status = nxreg_client_validate_feature(
        client, NXREG_FEATURE_TREE_RELATIONSHIPS, NX_OP_NXREG_CLIENT_CHILD_FIRST, result);
    if (child != NULL)
        memset(child, 0, sizeof(*child));
    if (status != NX_STATUS_OK)
        return status;
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED,
                                  NX_OP_NXREG_CLIENT_CHILD_FIRST);
    if (result == NULL || child == NULL || !nx_object_ref_is_valid(parent))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_FIRST);
    return client->api->child_first(client->api->context, caller, parent,
                                    child, result);
}

nx_status nxreg_client_child_next(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref current,
    nx_object_ref *next, nx_result *result) {
    nx_status status = nxreg_client_validate_feature(
        client, NXREG_FEATURE_TREE_RELATIONSHIPS, NX_OP_NXREG_CLIENT_CHILD_NEXT, result);
    if (next != NULL)
        memset(next, 0, sizeof(*next));
    if (status != NX_STATUS_OK)
        return status;
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED,
                                  NX_OP_NXREG_CLIENT_CHILD_NEXT);
    if (result == NULL || next == NULL || !nx_object_ref_is_valid(parent) ||
        !nx_object_ref_is_valid(current))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_NEXT);
    return client->api->child_next(client->api->context, caller, parent,
                                   current, next, result);
}

nx_status nxreg_client_child_count(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, uint32_t *count, nx_result *result) {
    nx_status status = nxreg_client_validate_feature(
        client, NXREG_FEATURE_TREE_RELATIONSHIPS, NX_OP_NXREG_CLIENT_CHILD_COUNT, result);
    if (count != NULL)
        *count = 0u;
    if (status != NX_STATUS_OK)
        return status;
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED,
                                  NX_OP_NXREG_CLIENT_CHILD_COUNT);
    if (result == NULL || count == NULL || !nx_object_ref_is_valid(parent))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  NX_OP_NXREG_CLIENT_CHILD_COUNT);
    return client->api->child_count(client->api->context, caller, parent,
                                    count, result);
}
