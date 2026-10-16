// @file 	ast.h
//
// @brief	ast_node structure and few helping methods
// @author 	Daniil Didenko (xdidend00)

#ifndef AST_H
#define AST_H
#include "error.h"

typedef enum
{
    PROGRAM,
} ast_node_type; // TODO: add rest of node types

typedef struct ast_node_s
{
    ast_node_type type;

    struct ast_node_s *left_child;
    struct ast_node_s *right_child;
} ast_node; // TODO: implement this fully, mb add missing fields

// @brief creating new dynamically allocated ast_node
//
// @return if function succeed: pointer to created ast_node, otherwise: NULL
ast_node *new_ast_node(error *err);

// @brief frees node and all it's children
void ast_node_destruct_recursively(ast_node *self);

#endif
