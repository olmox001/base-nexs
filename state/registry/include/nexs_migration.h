/* Canonical NXOLMO state model, source-qualified product port. */
#ifndef NEXS_REGISTRY_MIGRATION_H
#define NEXS_REGISTRY_MIGRATION_H

#include "nexs_checked.h"
#include "nexs_schema.h"
#include "nexs_operation_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NX_STATE_MIGRATION_FORMAT_VERSION UINT16_C(1)
#define NX_STATE_MIGRATION_HEADER_SIZE UINT16_C(60)
#define NX_STATE_MIGRATION_INSTRUCTION_SIZE UINT32_C(16)
#define NX_STATE_MIGRATION_MAGIC_0 UINT8_C('N')
#define NX_STATE_MIGRATION_MAGIC_1 UINT8_C('X')
#define NX_STATE_MIGRATION_MAGIC_2 UINT8_C('M')
#define NX_STATE_MIGRATION_MAGIC_3 UINT8_C('T')

enum {
  NX_STATE_MIGRATION_OP_COPY = 1,
  NX_STATE_MIGRATION_OP_ZERO = 2,
  NX_STATE_MIGRATION_OP_LITERAL = 3
};

/*
 * Executes a verified, straight-line payload transformation.
 *
 * The descriptor must be immutable and obtained from a validated sealed
 * schema registry. Its program_digest is included in the canonical schema
 * manifest; an all-zero digest marks an unbound, non-executable descriptor.
 * The manifest is not signed/authenticated by this API: callers must trust its
 * provenance. Scratch and output must be disjoint from program and input.
 * Output and output_digest are written only after verification, execution,
 * fuel accounting and output hashing all succeed. This is not a transaction.
 */
nx_status nx_state_migration_execute(
    const nx_state_schema_migration *migration, nx_const_span program,
    nx_const_span input, nx_mut_span scratch, nx_mut_span output,
    nx_digest256 *output_digest);

#ifdef __cplusplus
}
#endif

#endif
