/* Canonical NXOLMO state model, source-qualified product port. */
#ifndef NEXS_REGISTRY_NXREG_H
#define NEXS_REGISTRY_NXREG_H

#include "nexs_result.h"
#include "nexs_types.h"
#include "nexs_budget.h"
#include "nexs_operation_ids.h"
#include "nexs_endian.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NXREG_ABI_MAJOR UINT16_C(1)
#define NXREG_ABI_MINOR UINT16_C(0)
#define NXREG_API_V1_MIN_SIZE \
    (offsetof(nxreg_api_v1, child_count) + sizeof(((nxreg_api_v1 *)0)->child_count))

typedef struct nxreg_auth_context nxreg_auth_context;
typedef struct nx_state_store nx_state_store;
typedef struct nx_state_node nx_state_node;

#define NXREG_FEATURE_OBJECT_READ (UINT64_C(1) << 0)
#define NXREG_FEATURE_TREE_RELATIONSHIPS (UINT64_C(1) << 1)

#define NXREG_FEATURE_KNOWN_MASK \
    (NXREG_FEATURE_OBJECT_READ | NXREG_FEATURE_TREE_RELATIONSHIPS)

#define NXREG_RIGHT_READ UINT64_C(1)
#define NXREG_RIGHT_ENUMERATE UINT64_C(2)


/* Authorization must consume exact deterministic work from the supplied budget. */
typedef nx_status (*nxreg_authorize_fn)(
    void *context, const nxreg_auth_context *caller,
    uint64_t operation_id, nx_object_ref target, uint64_t required_rights,
    nx_operation_budget *budget);

typedef nx_status (*nxreg_object_read_fn)(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref object, nx_state_node *node, nx_result *result);
typedef nx_status (*nxreg_child_first_fn)(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref *child, nx_result *result);
typedef nx_status (*nxreg_child_next_fn)(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref current,
    nx_object_ref *next, nx_result *result);
typedef nx_status (*nxreg_child_count_fn)(
    void *context, const nxreg_auth_context *caller,
    nx_object_ref parent, uint32_t *count, nx_result *result);

typedef struct nxreg_api_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t reserved;
    uint64_t feature_bits;
    uint64_t required_feature_bits;
    void *context;
    nxreg_object_read_fn object_read;
    nxreg_child_first_fn child_first;
    nxreg_child_next_fn child_next;
    nxreg_child_count_fn child_count;
} nxreg_api_v1;

typedef struct nxreg_client {
    const nxreg_api_v1 *api;
    uint64_t feature_bits;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t reserved;
} nxreg_client;


typedef struct nxreg_dispatch_request {
    uint64_t operation_id;
    const nxreg_client *client;
    const nxreg_auth_context *caller;
    nx_object_ref object;
    nx_object_ref parent;
    nx_object_ref current;
    nx_state_node *node;
    nx_object_ref *reference_output;
    uint32_t *count_output;
    nx_result *result;
} nxreg_dispatch_request;

/* Encode/decode the canonical 64-bit NXREG opcode as eight LE bytes.
 * The codec accepts only assigned, active NXREG operation IDs. */
nx_status nxreg_opcode_encode(uint64_t operation_id, uint8_t encoded[8]);
nx_status nxreg_opcode_decode(const uint8_t encoded[8], uint64_t *operation_id);
/* Dispatch a typed in-process request through the canonical NXREG opcode switch. */
nx_status nxreg_dispatch(const nxreg_dispatch_request *request);

typedef struct nxreg_authorization_fuel_limits {
    uint64_t object_read;
    uint64_t child_first;
    uint64_t child_next;
    uint64_t child_count;
} nxreg_authorization_fuel_limits;

typedef struct nxreg_state_store_adapter {
    nx_state_store *store;
    nxreg_authorize_fn authorize;
    void *authorize_context;
    nxreg_authorization_fuel_limits authorization_fuel_limits;
} nxreg_state_store_adapter;

/* API tables are immutable after publication. The adapter and store are
 * caller-owned and must outlive every bound client. Caller context is opaque
 * and must originate at a trusted endpoint/kernel boundary. The authorization
 * per-operation fuel limits must be reviewed upper bounds derived from the
 * corresponding authority path. */
/* Validate the known ABI prefix and all advertised feature entry points. */
nx_status nxreg_api_validate(const nxreg_api_v1 *api,
                             uint16_t minimum_minor);
/* Bind only when all required features and version constraints are met. */
nx_status nxreg_client_bind(nxreg_client *client, const nxreg_api_v1 *api,
                            uint16_t minimum_minor,
                            uint64_t supported_features,
                            uint64_t required_features);
/* Publish the read-only H0 adapter; mutations require the transaction ABI. */
nx_status nxreg_state_store_adapter_init(
    nxreg_api_v1 *api, nxreg_state_store_adapter *adapter,
    nx_state_store *store, nxreg_authorize_fn authorize,
    void *authorize_context,
    const nxreg_authorization_fuel_limits *authorization_fuel_limits);

/* Read a generation-checked object through the negotiated NXREG API. */
nx_status nxreg_client_object_read(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref object, nx_state_node *node, nx_result *result);
/* Begin direct-child enumeration with a generation-safe object reference. */
nx_status nxreg_client_child_first(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref *child, nx_result *result);
/* Continue direct-child enumeration after the supplied current reference. */
nx_status nxreg_client_child_next(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, nx_object_ref current,
    nx_object_ref *next, nx_result *result);
/* Return the exact direct-child count for a parent reference. */
nx_status nxreg_client_child_count(
    const nxreg_client *client, const nxreg_auth_context *caller,
    nx_object_ref parent, uint32_t *count, nx_result *result);

#ifdef __cplusplus
}
#endif

#endif
