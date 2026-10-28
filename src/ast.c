// @file 	ast.c
//
// @author 	Daniil Didenko (xdidend00)

#include "ast.h"
#include "utils.h"

ast_node *new_ast_node(error *err)
{
    *err = NONE;
    ast_node *node = (ast_node *)utils_safe_malloc(err, sizeof(ast_node));
    if (*err != NONE)
    {
        return NULL;
    }

    node->type = AST_NODE_UNDEFINED;
    node->left_child = NULL;
    node->right_child = NULL;
    node->literal_type = AST_TYPE_NONE;
    node->attribute_index = -1; // sentinel value
    node->operator = AST_OP_NONE;

    return node;
}

void ast_node_destruct_recursively(ast_node **self)
{
    if (self == NULL || *self == NULL)
    {
        return;
    }

    ast_node_destruct_recursively(&(*self)->left_child);
    ast_node_destruct_recursively(&(*self)->right_child);

    UTILS_SAFE_FREE(self);
}
