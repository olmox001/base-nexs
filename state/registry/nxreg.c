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
    case NX_OP_NXREG_CLIENT_PARENT_READ:
        return adapter->authorization_fuel_limits.parent_read;
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
    /* Object READ is not permission to observe the object's unrelated
     * owner/parent references or raw backing extent/digest. These require
     * their own typed relationship/buffer capabilities; do not leak them
     * through this broad metadata projection. */
    nx_state_node projected = {0};
    status = nx_state_store_read(adapter->store, object, &projected,
                                 &store_result);
    if (status == NX_STATUS_OK) {
        memset(&projected.spec.parent, 0, sizeof(projected.spec.parent));
        memset(&projected.spec.owner, 0, sizeof(projected.spec.owner));
        projected.spec.payload_offset = 0u;
        memset(&projected.spec.payload_digest, 0,
               sizeof(projected.spec.payload_digest));
        *node = projected;
    }
    return nxreg_merge_operation_result(result, NX_OP_NXREG_CLIENT_OBJECT_READ, status,
                                        &store_result, &authorization);
}

/* A parent's ENUMERATE grant does not authorize exposing a child's object
 * ID, its existence or the exact number of hidden descendants. Every visible
 * child is rechecked through the same trusted NXREG authority as object_read.
 * Iterate canonical StateStore refs: no RegKey pointer traversal or second
 * namespace/index is constructed. Failed scans never expose partial output.
 * Remaining work: resumable versioned namespace cursors, commit-capable
 * relation updates and a timing-safe redaction policy for hardware views. */
enum nxreg_child_query {
    NXREG_CHILD_FIRST = 1,
    NXREG_CHILD_NEXT = 2,
    NXREG_CHILD_COUNT = 3
};

static nx_status nxreg_child_step_work(uint64_t *total,
                                       uint64_t step) {
    uint64_t combined;
    nx_status status = nx_u64_add(*total, step, &combined);
    if (status == NX_STATUS_OK)
        *total = combined;
    return status;
}

static nx_status nxreg_child_finish(nx_result *result, uint64_t operation,
                                    nx_status status, uint64_t fuel,
                                    uint32_t visible) {
    nx_status recorded = nx_result_init(
        result, status,
        status == NX_STATUS_ACCESS_DENIED ? NX_RESULT_DOMAIN_SECURITY
                                          : NX_RESULT_DOMAIN_REGISTRY,
        operation, status == NX_STATUS_OK ? 1u : 0u,
        status == NX_STATUS_OK ? visible : 0u,
        NX_CYCLES_UNKNOWN, fuel);
    return recorded == NX_STATUS_OK ? status : recorded;
}

/* A multi-call child walk must be one committed-epoch observation,
 * even though each StateStore read takes its own short internal lock.
 * Return BUSY on any concurrent structural/value revision instead of
 * exposing mixed old/new relationships or partial counts. */
static nx_status nxreg_child_epoch_read(
    nxreg_state_store_adapter *adapter, uint64_t *fuel,
    uint64_t *epoch) {
    nx_state_store_stats stats;
    nx_result result = {0};
    nx_status status = nx_state_store_stats_read(
        adapter->store, &stats, &result);
    if (nxreg_child_step_work(fuel, result.fuel_used) != NX_STATUS_OK)
        return NX_STATUS_OVERFLOW;
    if (status == NX_STATUS_OK)
        *epoch = stats.epoch;
    return status;
}

static nx_status nxreg_child_epoch_validate(
    nxreg_state_store_adapter *adapter, uint64_t *fuel,
    uint64_t initial_epoch) {
    uint64_t final_epoch = 0u;
    nx_status status = nxreg_child_epoch_read(adapter, fuel, &final_epoch);
    if (status != NX_STATUS_OK)
        return status;
    return final_epoch == initial_epoch ? NX_STATUS_OK : NX_STATUS_BUSY;
}

static nx_status nxreg_store_visible_children(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref current,
    enum nxreg_child_query query, nx_object_ref *output,
    uint32_t *count, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement parent_auth = {0};
    nx_result state_result = {0};
    nx_result hidden_result = {0};
    uint64_t operation = query == NXREG_CHILD_FIRST
        ? NX_OP_NXREG_CLIENT_CHILD_FIRST
        : query == NXREG_CHILD_NEXT
        ? NX_OP_NXREG_CLIENT_CHILD_NEXT
        : NX_OP_NXREG_CLIENT_CHILD_COUNT;
    uint64_t fuel = 0u;
    uint64_t initial_epoch = 0u;
    uint32_t raw_count = 0u;
    uint32_t visible = 0u;
    nx_object_ref cursor = {0};
    nx_status status;
    if (result == NULL || (query != NXREG_CHILD_COUNT && output == NULL) ||
        (query == NXREG_CHILD_COUNT && count == NULL))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  operation);
    status = nxreg_adapter_authorize(
        adapter, caller, operation, parent, NXREG_RIGHT_ENUMERATE,
        &parent_auth, result);
    if (status != NX_STATUS_OK)
        return status;
    fuel = parent_auth.fuel_used;
    /* The position passed to child_next is also an object reference.
     * Knowing an inaccessible ref must not allow probing its parent or
     * neighboring sibling identities through a cursor. */
    if (query == NXREG_CHILD_NEXT) {
        nx_operation_measurement cursor_auth = {0};
        status = nxreg_adapter_authorize(
            adapter, caller, NX_OP_NXREG_CLIENT_OBJECT_READ, current,
            NXREG_RIGHT_READ, &cursor_auth, &hidden_result);
        if (nxreg_child_step_work(&fuel, cursor_auth.fuel_used)
            != NX_STATUS_OK)
            return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                      fuel, 0u);
        if (status != NX_STATUS_OK)
            return nxreg_child_finish(result, operation, status, fuel, 0u);
    }
    status = nxreg_child_epoch_read(adapter, &fuel, &initial_epoch);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, status, fuel, 0u);
    status = nx_state_store_child_count(adapter->store, parent, &raw_count,
                                        &state_result);
    if (nxreg_child_step_work(&fuel, state_result.fuel_used) != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                  fuel, 0u);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, status, fuel, 0u);

    /* At most the actual, currently committed child count is visited. A
     * concurrent parent mutation may invalidate the cursor and abort the
     * read, never yielding a child that was not independently authorized. */
    for (uint32_t examined = 0u; examined < raw_count; ++examined) {
        nx_object_ref candidate = {0};
        nx_operation_measurement child_auth = {0};
        if (examined == 0u && query != NXREG_CHILD_NEXT)
            status = nx_state_store_child_first(adapter->store, parent,
                                                 &candidate, &state_result);
        else
            status = nx_state_store_child_next(
                adapter->store, parent,
                examined == 0u ? current : cursor,
                &candidate, &state_result);
        if (nxreg_child_step_work(&fuel, state_result.fuel_used)
            != NX_STATUS_OK)
            return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                      fuel, 0u);
        /* After skipped masked siblings, reaching the end is a normal
         * child_next miss, not a corrupted or BUSY namespace. */
        if (status == NX_STATUS_NOT_FOUND && query == NXREG_CHILD_NEXT) {
            nx_status epoch_status = nxreg_child_epoch_validate(
                adapter, &fuel, initial_epoch);
            return nxreg_child_finish(result, operation,
                epoch_status == NX_STATUS_OK ? status : epoch_status,
                fuel, 0u);
        }
        if (status != NX_STATUS_OK)
            return nxreg_child_finish(result, operation,
                status == NX_STATUS_NOT_FOUND ? NX_STATUS_BUSY : status,
                fuel, 0u);
        cursor = candidate;
        status = nxreg_adapter_authorize(
            adapter, caller, NX_OP_NXREG_CLIENT_OBJECT_READ, candidate,
            NXREG_RIGHT_READ, &child_auth, &hidden_result);
        if (nxreg_child_step_work(&fuel, child_auth.fuel_used) != NX_STATUS_OK)
            return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                      fuel, 0u);
        if (status == NX_STATUS_ACCESS_DENIED)
            continue;
        if (status != NX_STATUS_OK)
            return nxreg_child_finish(result, operation, status, fuel, 0u);
        if (query != NXREG_CHILD_COUNT) {
            nx_status epoch_status = nxreg_child_epoch_validate(
                adapter, &fuel, initial_epoch);
            if (epoch_status != NX_STATUS_OK)
                return nxreg_child_finish(result, operation, epoch_status,
                                          fuel, 0u);
            *output = candidate;
            return nxreg_child_finish(result, operation, NX_STATUS_OK,
                                      fuel, 1u);
        }
        if (visible == UINT32_MAX)
            return nxreg_child_finish(result, operation,
                                      NX_STATUS_RESOURCE_EXHAUSTED,
                                      fuel, 0u);
        ++visible;
    }
    status = nxreg_child_epoch_validate(adapter, &fuel, initial_epoch);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, status, fuel, 0u);
    if (query == NXREG_CHILD_COUNT) {
        *count = visible;
        return nxreg_child_finish(result, operation, NX_STATUS_OK,
                                  fuel, visible);
    }
    return nxreg_child_finish(result, operation, NX_STATUS_NOT_FOUND,
                              fuel, 0u);
}

static nx_status nxreg_store_child_first(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    nx_object_ref *child, nx_result *result) {
    return nxreg_store_visible_children(
        context, caller, parent, (nx_object_ref){0}, NXREG_CHILD_FIRST,
        child, NULL, result);
}

static nx_status nxreg_store_child_next(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    nx_object_ref current, nx_object_ref *next, nx_result *result) {
    return nxreg_store_visible_children(
        context, caller, parent, current, NXREG_CHILD_NEXT,
        next, NULL, result);
}

static nx_status nxreg_store_child_count(
    void *context, const nxreg_auth_context *caller, nx_object_ref parent,
    uint32_t *count, nx_result *result) {
    return nxreg_store_visible_children(
        context, caller, parent, (nx_object_ref){0}, NXREG_CHILD_COUNT,
        NULL, count, result);
}

/* Every edge is read from the same canonical StateStore. A read grant on
 * the child alone cannot reveal its parent: the parent itself requires an
 * independently authenticated READ grant. Re-read the child after checking
 * the parent and reject a concurrent move, instead of returning a stale
 * topology relation that has already changed. */
static nx_status nxreg_store_parent_read(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref child, nx_object_ref *parent, nx_result *result) {
    nxreg_state_store_adapter *adapter = context;
    nx_operation_measurement child_auth = {0};
    nx_operation_measurement parent_auth = {0};
    nx_result store_result = {0};
    nx_result denied_result = {0};
    nx_state_node node;
    nx_state_node current;
    uint64_t fuel = 0u;
    const uint64_t operation = NX_OP_NXREG_CLIENT_PARENT_READ;
    nx_status status;
    if (result == NULL || parent == NULL)
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  operation);
    status = nxreg_adapter_authorize(adapter, caller, operation, child,
                                      NXREG_RIGHT_READ, &child_auth, result);
    if (status != NX_STATUS_OK)
        return status;
    fuel = child_auth.fuel_used;
    status = nx_state_store_read(adapter->store, child, &node, &store_result);
    if (nxreg_child_step_work(&fuel, store_result.fuel_used) != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                  fuel, 0u);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, status, fuel, 0u);
    if (!nx_object_ref_is_valid(node.spec.parent))
        return nxreg_child_finish(result, operation, NX_STATUS_NOT_FOUND,
                                  fuel, 0u);
    status = nxreg_adapter_authorize(
        adapter, caller, operation, node.spec.parent, NXREG_RIGHT_READ,
        &parent_auth, &denied_result);
    if (nxreg_child_step_work(&fuel, parent_auth.fuel_used) != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                  fuel, 0u);
    if (status == NX_STATUS_ACCESS_DENIED)
        return nxreg_child_finish(result, operation, NX_STATUS_NOT_FOUND,
                                  fuel, 0u);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, status, fuel, 0u);
    status = nx_state_store_read(adapter->store, node.spec.parent,
                                  &current, &store_result);
    if (nxreg_child_step_work(&fuel, store_result.fuel_used) != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                  fuel, 0u);
    if (status != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_BUSY,
                                  fuel, 0u);
    status = nx_state_store_read(adapter->store, child,
                                  &current, &store_result);
    if (nxreg_child_step_work(&fuel, store_result.fuel_used) != NX_STATUS_OK)
        return nxreg_child_finish(result, operation, NX_STATUS_OVERFLOW,
                                  fuel, 0u);
    if (status != NX_STATUS_OK || current.revision != node.revision ||
        !nx_object_ref_equal(current.spec.parent, node.spec.parent))
        return nxreg_child_finish(result, operation, NX_STATUS_BUSY,
                                  fuel, 0u);
    *parent = node.spec.parent;
    return nxreg_child_finish(result, operation, NX_STATUS_OK, fuel, 1u);
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
    case NX_OP_NXREG_CLIENT_PARENT_READ:
        if (request->reference_output == NULL)
            return nxreg_result_error(request->result,
                                      NX_STATUS_INVALID_ARGUMENT,
                                      request->operation_id);
        return nxreg_client_parent_read(request->client, request->caller,
                                        request->object,
                                        request->reference_output,
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
    if ((features & NXREG_FEATURE_PARENT_RELATIONSHIP) != 0u) {
        if (api->abi_minor < 1u ||
            api->struct_size < NXREG_API_V1_PARENT_MIN_SIZE)
            return NX_STATUS_VERSION_MISMATCH;
        if (api->parent_read == NULL ||
            (features & NXREG_FEATURE_TREE_RELATIONSHIPS) == 0u)
            return NX_STATUS_MALFORMED;
    }
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
    if (adapter->authorization_fuel_limits.parent_read != 0u) {
        candidate.feature_bits |= NXREG_FEATURE_PARENT_RELATIONSHIP;
        candidate.parent_read = nxreg_store_parent_read;
    }
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

nx_status nxreg_client_parent_read(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref child, nx_object_ref *parent, nx_result *result) {
    const uint64_t operation = NX_OP_NXREG_CLIENT_PARENT_READ;
    nx_status status;
    if (parent != NULL)
        memset(parent, 0, sizeof(*parent));
    status = nxreg_client_validate_feature(
        client, NXREG_FEATURE_PARENT_RELATIONSHIP, operation, result);
    if (status != NX_STATUS_OK)
        return status;
    if (caller == NULL)
        return nxreg_result_error(result, NX_STATUS_ACCESS_DENIED, operation);
    if (parent == NULL || result == NULL || !nx_object_ref_is_valid(child))
        return nxreg_result_error(result, NX_STATUS_INVALID_ARGUMENT,
                                  operation);
    return client->api->parent_read(client->api->context, caller,
                                    child, parent, result);
}
