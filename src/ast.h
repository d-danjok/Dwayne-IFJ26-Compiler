// @file 	ast.h
//
// @brief	ast_node structure and few helping methods
// @author 	Daniil Didenko (xdidend00)

#ifndef AST_H
#define AST_H
#include <stddef.h>

#include "error.h"
#include "token.h"

typedef enum
{
    AST_NODE_UNDEFINED,

    AST_NODE_PROGRAM,
    AST_NODE_DEF_LIST,
    AST_NODE_VAL_DEF,
    AST_NODE_VAR_DEF,
    AST_NODE_DEF_BODY,
    AST_NODE_FUNC_DEF,
    AST_NODE_MAIN_FUNC_DEF, // main_func
    AST_NODE_FUNC_START,
    AST_NODE_FUNC_NAME,
    AST_NODE_FUNC_HEAD,
    AST_NODE_PARAM_LIST,
    AST_NODE_PARAM_LIST_, // param_list'
    AST_NODE_PARAM,
    AST_NODE_FUNC_BODY,
    AST_NODE_BLOCK,
    AST_NODE_STAT_LIST,
    AST_NODE_STAT_LIST_, // stat_list'
    AST_NODE_STATEMENT,
    AST_NODE_DEFINITION,
    AST_NODE_ASSIGN_VAL,
    AST_NODE_WHILE,

    AST_NODE_IF_LIST,
    AST_NODE_IF_LIST_, // if_list'
    AST_NODE_IF,
    AST_NODE_ELSE,

    AST_NODE_RETURN,

    // used for func call
    AST_NODE_FUNC_CALL,
    AST_NODE_CALL_PARAMS,
    AST_NODE_CALL_PARAMS_, // call_params'
    AST_NODE_CALL_PARAM,

    AST_NODE_UNARY_OPERATION,
    AST_NODE_BINARY_OPERATION,

    AST_NODE_LITERAL, // Int, Double, Bool, Unit, String
    AST_NODE_IDENTIFIER,

    // nodes for semantic analyser output
    AST_NODE_INT_TO_DOUBLE_CONVERSION, // specification page 4, section 1.3
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
    AST_OP_MULTIPLY,
    AST_OP_DIV,
    AST_OP_GREATER,       //	>
    AST_OP_GREATER_EQUAL, //	>=
    AST_OP_LESS,          //	<
    AST_OP_LESS_EQUAL,    //	<=
    AST_OP_EQUAL,         //	==
    AST_OP_NOT_EQUAL,     //	!=
                          //    BOOLTHEN extension operations :
    AST_OP_AND,           //    &&
    AST_OP_OR,            //    ||
    AST_OP_NOT,           //    !

    AST_OP_CONCAT, // generated on semantic analysis stage from AST_OP_PLUS if both operands have String type
} ast_operator;

typedef struct ast_node_s
{
    ast_node_type type;

    struct ast_node_s *left_child; // for unary is only child
    struct ast_node_s *right_child;

    // fields for literal/identifier
    ast_literal_type literal_type; // also represents function return type
    int attribute_index;           // for literals

    long
        block_index; // represents block index for blocks, and index of block where variable was created for identifiers

    ast_operator operator; // for operations

    size_t src_line_number; // token line representation for compiler error
} ast_node;

// @brief creating new dynamically allocated ast_node
//
// @return if function succeed: pointer to created ast_node, otherwise: NULL
ast_node *new_ast_node(error *err);

// @brief frees node and all it's children
void ast_node_destruct_recursively(ast_node **self);

#endif
