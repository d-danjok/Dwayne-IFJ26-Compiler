// @file 	ast.h
//
// @brief	ast_node structure and few helping methods
// @author 	Daniil Didenko (xdidend00)

#ifndef AST_H
#define AST_H
#include "error.h"
#include "token.h"

typedef enum
{
    UNDEFINED,

    PROGRAM,
    DEF_LIST,
    VAL_DEF,
    VAR_DEF,
    DEF_BODY,
    FUNC_DEF,
    MAIN_FUNC_DEF, // main_func
    FUNC_START,
    FUNC_NAME,
    FUNC_HEAD,
    PARAM_LIST,
    PARAM_LIST_, // param_list'
    PARAM,
    FUNC_BODY,
    BLOCK,
    STAT_LIST,
    STAT_LIST_, // stat_list'
    STATEMENT,
    DEFINITION,
    ASSIGN_VAL,
    FUNC_CALL,
    CALL_PARAMS,
    CALL_PARAMS_, // call_params'
    CALL_PARAM,
    WHILE,
    IF_ELSE,
    RETURN,

    UNARY_OPERATION,
    BINARY_OPERATION,

    LITERAL, // Int, Double, Bool, Unit, String
    IDENTIFIER,
} ast_node_type;

typedef enum
{
    AST_TYPE_NONE, // for not literal
    AST_TYPE_INT = TOKEN_INT,
    AST_TYPE_DOUBLE = TOKEN_DOUBLE,
    AST_TYPE_STRING = TOKEN_STRING,
    AST_TYPE_UNIT = TOKEN_UNIT,
    AST_TYPE_BOOLEAN = TOKEN_BOOLEAN,
} ast_literal_type;

typedef enum
{
    AST_OP_NONE, // not operation
    AST_OP_PLUS,
    AST_OP_MINUS,
    AST_OP_MINUS_UNARY,
    AST_OP_MULT,
    AST_OP_DIV,
    AST_OP_GREATER,       //	>
    AST_OP_GREATER_EQUAL, //	>=
    AST_OP_LESS,          //	<
    AST_OP_LESS_EQUAL,    //	<=
    AST_OP_EQUAL,         //	==
    AST_OP_NOT_EQUAL,     //	!=
} ast_operator;

typedef struct ast_node_s
{
    ast_node_type type;

    struct ast_node_s *left_child; // for unary is only child
    struct ast_node_s *right_child;

    // fields for literal/identifier
    ast_literal_type literal_type; // also represents function return type
    int attribute_index;           // for literals

    ast_operator operator; // for operations

    int src_line_number; // token line representation for compiler error
} ast_node;

// @brief creating new dynamically allocated ast_node
//
// @return if function succeed: pointer to created ast_node, otherwise: NULL
ast_node *new_ast_node(error *err);

// @brief frees node and all it's children
void ast_node_destruct_recursively(ast_node **self);

#endif
