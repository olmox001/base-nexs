/* Actual single-engine NXOLMO/RegKey integration: object generations,
 * namespace topology, transparent capacity growth and transactional updates.
 * This test calls the real production sources, never a fake registry.
 */
#include "nexs_registry.h"
#include "nexs_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void verify(int ok, const char *description) {
    ++checks;
    if (!ok) {
        ++failures;
        fprintf(stderr, "NXOLMO_REGKEY_FAIL %s\n", description);
    }
}

static nx_state_node read_node(const char *path) {
    nx_state_node node;
    nx_result result;
    memset(&node, 0, sizeof(node));
    verify(reg_object_read(path, &node, &result) == NX_STATUS_OK,
           "canonical path resolves a real generation-checked state node");
    return node;
}

static void test_identity_and_move(void) {
    nx_state_node root = read_node("/");
    verify(nx_object_ref_is_valid(root.ref),
           "root has nonzero store identity and generation");
    verify(reg_set("/objects/live", val_int(10), RK_ALL) == 0,
           "creates typed object under canonical namespace");
    nx_state_node before = read_node("/objects/live");
    nx_state_node parent = read_node("/objects");
    verify(nx_object_ref_equal(before.spec.parent, parent.ref),
           "StateStore owns actual parent relation");
    verify(before.spec.type_id == (uint64_t)TYPE_INT + 1U,
           "StateStore holds typed object schema");

    verify(reg_set("/objects/live", val_str("payload"), RK_ALL) == 0,
           "replace value through same object state transition");
    nx_state_node updated = read_node("/objects/live");
    verify(nx_object_ref_equal(before.ref, updated.ref) &&
           updated.revision == before.revision + 1U,
           "value replacement preserves identity and increments revision");
    verify(updated.spec.type_id == (uint64_t)TYPE_STR + 1U,
           "typed state revised before value publication");

    verify(reg_move("/objects/live", "/relocated/live") == 0,
           "reparent object across real NXOLMO namespace");
    nx_state_node moved = read_node("/relocated/live");
    nx_state_node new_parent = read_node("/relocated");
    verify(nx_object_ref_equal(moved.ref, before.ref) &&
           nx_object_ref_equal(moved.spec.parent, new_parent.ref),
           "move preserves identity and changes canonical parent");
    verify(moved.revision == updated.revision + 1U,
           "reparent increments object revision");
    verify(reg_lookup("/objects/live") == NULL,
           "retired path not reachable through name projection");

    verify(reg_delete("/relocated") == 0,
           "subtree deletion clears generational records");
    nx_result result;
    nx_state_node stale;
    nx_status rc = nx_state_store_read(&g_registry.state, before.ref,
                                       &stale, &result);
    verify(rc == NX_STATUS_NOT_FOUND || rc == NX_STATUS_STALE_REFERENCE,
           "deleted object reference cannot be reused");
}

static void test_growth_and_consistency(void) {
    const uint32_t before = g_registry.state.slot_capacity;
    char path[96];
    for (uint32_t i = 0U; i < 384U; ++i) {
        int written = snprintf(path, sizeof(path), "/nodes/item_%u", i);
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            verify(0, "test path generated without truncation");
            return;
        }
        if (reg_set(path, val_int((int64_t)i), RK_ALL) != 0) {
            verify(0, "every growth admission publishes one object");
            return;
        }
    }
    verify(g_registry.state.slot_capacity > before,
           "provider-backed StateStore actually grew at runtime");
    nx_result result;
    nx_state_store_stats stats;
    verify(nx_state_store_stats_read(&g_registry.state, &stats, &result)
               == NX_STATUS_OK,
           "canonical StateStore statistics available");
    verify(stats.count == g_registry.total_keys,
           "one state node per RegKey projection, no shadow identity");
    verify(nx_state_store_validate(&g_registry.state, &result)
               == NX_STATUS_OK,
           "StateStore index and parent/child invariants survive growth");
    nx_state_node last = read_node("/nodes/item_383");
    verify(last.spec.type_id == (uint64_t)TYPE_INT + 1U,
           "grown table still resolves latest typed object");
    nx_state_node first = read_node("/nodes/item_0");
    verify(nx_object_ref_is_valid(first.ref) &&
           !nx_object_ref_equal(last.ref, first.ref),
           "earliest identity survives multiple growth cycles");
    verify(nx_state_store_grow(&g_registry.state, g_registry.slots,
                               g_registry.state.slot_capacity,
                               g_registry.index, g_registry.rehash_index,
                               g_registry.state.index_capacity)
               == NX_STATUS_INVALID_ARGUMENT,
           "invalid provider growth rejected without partial publication");
    verify(nx_state_store_grow(
               &g_registry.state, g_registry.slots,
               g_registry.state.slot_capacity * 2U,
               g_registry.index, g_registry.rehash_index,
               g_registry.state.index_capacity * 2U)
               == NX_STATUS_INVALID_ARGUMENT,
           "cross-alias of existing memory rejected before mutating buffers");
    verify(nx_state_store_read(&g_registry.state, first.ref, &first, &result)
               == NX_STATUS_OK,
           "failed growth leaves live generational references valid");
    verify(reg_delete("/nodes") == 0,
           "deep subtree retirement does not leave shadow state nodes");
    verify(nx_state_store_stats_read(&g_registry.state, &stats, &result)
               == NX_STATUS_OK &&
           stats.count == g_registry.total_keys,
           "retirement reconciles canonical state count and path projection");
    verify(nx_state_store_validate(&g_registry.state, &result)
               == NX_STATUS_OK,
           "canonical StateStore remains valid after mass retirement");
}

int main(void) {
    verify(reg_init() == 0, "NXOLMO-backed registry initializes");
    if (g_registry.root != NULL) {
        test_identity_and_move();
        test_growth_and_consistency();
    }
    printf("NXOLMO_REGKEY_INTEGRATION %s checks=%u failures=%u\n",
           failures == 0U ? "PASS" : "FAIL", checks, failures);
    return failures == 0U ? 0 : 1;
}
