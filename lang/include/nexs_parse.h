/*
 * nexs_parse.h — NEXS Parser API
 * =================================
 * Public parse entry points.  The full Parser and ASTNode types are
 * defined in nexs_ast.h which this header includes.
 */

#ifndef NEXS_PARSE_H
#define NEXS_PARSE_H
#pragma once

#include "nexs_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

NEXS_API void     parser_init(Parser *p, Lexer *lex);
NEXS_API ASTNode *parse_program(Parser *p);
NEXS_API ASTNode *parse_stmt(Parser *p);

#ifdef __cplusplus
}
#endif

#endif /* NEXS_PARSE_H */
