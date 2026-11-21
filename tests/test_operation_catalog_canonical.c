/* NXOLMO operation runtime regression against Base-NEXS single ABI. */
#include "nexs_operation_catalog.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                      __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void) {
    size_t index;
    CHECK(nx_operation_catalog_validate() == NX_STATUS_OK);
    CHECK(nx_operation_catalog_count() == 198u);
    CHECK(nx_operation_catalog_find_name("nx_buffer_create") != NULL);
    CHECK(nx_operation_catalog_find_name("nx_vm_allocate") != NULL);
    CHECK(nx_operation_catalog_find_name("nx_capability_verify") != NULL);
    CHECK(nx_operation_catalog_find_name("nx_operation_cost_evaluate") != NULL);
    CHECK(NX_OP_BUFFER_CREATE ==
          nx_operation_catalog_find_name("nx_buffer_create")->operation_id);
    CHECK(NX_OP_STATE_STORE_INIT ==
          nx_operation_catalog_find_name("nx_state_store_init")->operation_id);
    CHECK(NX_OP_STATE_MIGRATION_EXECUTE ==
          nx_operation_catalog_find_name("nx_state_migration_execute")->operation_id);
    CHECK(NX_OP_STATE_STORE_CHILD_FIRST != NX_OP_STATE_MIGRATION_EXECUTE);
    CHECK(NX_OP_NXREG_CLIENT_OBJECT_READ == UINT64_C(0x000d0001));
    CHECK(NX_OP_NXREG_CLIENT_CHILD_FIRST == UINT64_C(0x000d0002));
    CHECK(NX_OP_NXREG_CLIENT_CHILD_NEXT == UINT64_C(0x000d0003));
    CHECK(NX_OP_NXREG_CLIENT_CHILD_COUNT == UINT64_C(0x000d0004));
    CHECK(NX_OP_NXREG_API_VALIDATE == UINT64_C(0x000d0005));
    CHECK(NX_OP_NXREG_CLIENT_BIND == UINT64_C(0x000d0006));
    CHECK(NX_OP_NXREG_STATE_STORE_ADAPTER_INIT == UINT64_C(0x000d0007));
    CHECK(NX_OP_NXREG_DISPATCH == UINT64_C(0x000d0008));
    CHECK(NX_OP_NXREG_OPCODE_ENCODE == UINT64_C(0x000d0009));
    CHECK(NX_OP_NXREG_OPCODE_DECODE == UINT64_C(0x000d000a));
    CHECK(NX_OP_NXREG_DISPATCH == UINT64_C(0x000d0008));
    CHECK(NX_OP_NXREG_OPCODE_ENCODE == UINT64_C(0x000d0009));
    CHECK(NX_OP_NXREG_OPCODE_DECODE == UINT64_C(0x000d000a));
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_CLIENT_OBJECT_READ) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_CLIENT_CHILD_FIRST) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_CLIENT_CHILD_NEXT) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_CLIENT_CHILD_COUNT) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_API_VALIDATE) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_CLIENT_BIND) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_STATE_STORE_ADAPTER_INIT) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_DISPATCH) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_OPCODE_ENCODE) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_OPCODE_DECODE) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_DISPATCH) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_OPCODE_ENCODE) != NULL);
    CHECK(nx_operation_catalog_find(NX_OP_NXREG_OPCODE_DECODE) != NULL);
    CHECK(NX_OP_NXREG_CLIENT_OBJECT_READ != NX_OP_BUFFER_MANAGER_INIT);
    CHECK(NX_OP_BUFFER_MANAGER_INIT == UINT64_C(0x00020001));
    CHECK(NX_OP_BUFFER_VIEW_COMMIT == UINT64_C(0x00020004));
    CHECK(NX_OP_BUFFER_VIEW_RELEASE == UINT64_C(0x00020005));
    CHECK(nx_operation_catalog_find(UINT64_C(0)) == NULL);
    {
        nx_operation_work_input input = {0u, 1024u, 0u, 0u};
        const nx_operation_contract *crc =
            nx_operation_catalog_find_name("nx_crc32c");
        const nx_operation_contract *bitwise =
            nx_operation_catalog_find_name("nx_crc64_ecma_bitwise");
        const nx_operation_contract *metered =
            nx_operation_catalog_find_name("nx_buffer_create");
        const nx_operation_contract *sha_update =
            nx_operation_catalog_find_name("nx_sha256_update");
        const nx_operation_contract *index_capacity =
            nx_operation_catalog_find_name("nx_state_store_required_index_capacity");
        const nx_operation_contract *catalog_validate =
            nx_operation_catalog_find_name("nx_operation_catalog_validate");
        nx_operation_contract forged;
        uint64_t work = 0u;
        CHECK(crc != NULL && bitwise != NULL && metered != NULL);
        CHECK(sha_update != NULL && index_capacity != NULL &&
              catalog_validate != NULL);
        CHECK(nx_operation_cost_evaluate(crc, &input, &work) == NX_STATUS_OK);
        CHECK(work == 1026u);
        input.bytes = 2u;
        CHECK(nx_operation_cost_evaluate(bitwise, &input, &work) == NX_STATUS_OK);
        CHECK(work == 18u);
        input = (nx_operation_work_input){2u, 3u, 4u, 5u};
        CHECK(nx_operation_cost_evaluate(sha_update, &input, &work) ==
              NX_STATUS_OK);
        CHECK(work == 387u);
        CHECK(nx_operation_cost_evaluate(index_capacity, &input, &work) ==
              NX_STATUS_OK);
        CHECK(work == 3u);
        CHECK(nx_operation_cost_evaluate(catalog_validate, &input, &work) ==
              NX_STATUS_OK);
        CHECK(work == 33u);
        input.bytes = UINT64_MAX;
        CHECK(nx_operation_cost_evaluate_id(crc->operation_id, &input, &work) ==
              NX_STATUS_OVERFLOW);
        CHECK(work == 0u);
        CHECK(nx_operation_cost_evaluate(metered, &input, &work) ==
              NX_STATUS_NOT_SUPPORTED);
        forged = *crc;
        forged.per_byte_work = 0u;
        CHECK(nx_operation_cost_evaluate(&forged, &input, &work) ==
              NX_STATUS_INVALID_ARGUMENT);
        {
            union work_output_alias {
                nx_operation_work_input input;
                uint64_t work;
            } alias;
            alias.input = input;
            CHECK(nx_operation_cost_evaluate(crc, &alias.input, &alias.work) ==
                  NX_STATUS_INVALID_ARGUMENT);
        }
        CHECK(nx_operation_cost_evaluate_id(UINT64_C(1), &input, &work) ==
              NX_STATUS_NOT_FOUND);
    }
    for (index = 0u; index < nx_operation_catalog_count(); ++index) {
        const nx_operation_contract *entry = nx_operation_catalog_at(index);
        CHECK(entry != NULL);
        CHECK((entry->operation_id >> 32u) == 0u);
        CHECK(((entry->operation_id >> 16u) & UINT64_C(0xffff)) != 0u);
        CHECK((entry->operation_id & UINT64_C(0xffff)) != 0u);
        CHECK(nx_operation_catalog_find(entry->operation_id) == entry);
        CHECK(nx_operation_catalog_find_name(entry->name) == entry);
        if (index != 0u)
            CHECK(nx_operation_catalog_at(index - 1u)->operation_id <
                  entry->operation_id);
    }
    (void)puts("NXOLMO operation catalog structure: PASS");
    return 0;
}
