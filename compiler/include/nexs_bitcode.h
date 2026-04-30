/*
 * compiler/include/nexs_bitcode.h — NEXS Bitcode format (.nxb)
 *
 * Binary AST encoding (NOT WASM, NOT C transpilation).
 *
 * Header (16B):
 *   "NXBC" | version(2) | flags(2) | n_consts(4) | n_nodes(4) | entry(4)
 *
 * ConstPool[n_consts]:
 *   type(1) | len(4) | data(len)
 *
 * Node[n_nodes]:
 *   kind(1) | flags(1) | left(4) | right(4) | extra(8)
 */
#ifndef NEXS_BITCODE_H
#define NEXS_BITCODE_H
#pragma once
#include <stdint.h>
#include <stddef.h>

#define NXB_MAGIC        "NXBC"
#define NXB_MAGIC_LEN    4
#define NXB_VERSION      1
#define NXB_FLAG_NONE    0

/* Const types */
#define NXB_CONST_NIL    0
#define NXB_CONST_INT    1
#define NXB_CONST_FLOAT  2
#define NXB_CONST_STR    3

/* Node kinds */
#define NXB_NODE_NOP     0
#define NXB_NODE_CONST   1   /* extra=const_pool_idx */
#define NXB_NODE_VAR     2   /* extra=name_const_idx */
#define NXB_NODE_ASSIGN  3   /* left=var_node, right=expr_node */
#define NXB_NODE_BINOP   4   /* flags=op, left, right */
#define NXB_NODE_CALL    5   /* extra=fn_const_idx, left=arg_list_start */
#define NXB_NODE_IF      6   /* left=cond, right=body */
#define NXB_NODE_LOOP    7   /* right=body */
#define NXB_NODE_RET     8   /* left=val */
#define NXB_NODE_OUT     9   /* left=val */
#define NXB_NODE_FN_DEF  10  /* extra=name_const_idx, left=params, right=body */
#define NXB_NODE_EXEC    11  /* extra=path_const_idx */
#define NXB_NODE_REG_GET 12  /* extra=path_const_idx */
#define NXB_NODE_REG_SET 13  /* extra=path_const_idx, left=val */
#define NXB_NODE_BREAK   14
#define NXB_NODE_SEQ     15  /* left=first, right=rest (singly-linked) */

typedef struct {
    uint8_t  kind;
    uint8_t  flags;
    uint32_t left;
    uint32_t right;
    uint64_t extra;
} NxbNode;

typedef struct {
    uint8_t  type;
    uint32_t len;
    uint8_t *data;   /* heap-allocated by decode */
} NxbConst;

typedef struct {
    uint32_t  n_consts;
    NxbConst *consts;
    uint32_t  n_nodes;
    NxbNode  *nodes;
    uint32_t  entry;
} NxbProgram;

/* Encode the AST of src_path to out_path (.nxb file).
 * Returns 0 on success. */
int nexs_encode_bc(const char *src_path, const char *out_path);

/* Decode a .nxb file into a NxbProgram.
 * Caller must call nexs_free_bc() when done. */
int nexs_decode_bc(const char *path, NxbProgram *out);

/* Free all memory in a decoded NxbProgram. */
void nexs_free_bc(NxbProgram *prog);

#endif
