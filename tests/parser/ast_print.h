// @file ast_print.h
//
// @brief debugging/testing utility that prints an AST as a tree

#ifndef AST_PRINT_H
#define AST_PRINT_H

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"

// @brief prints the tree one node per line, children marked L (left) and R (right)
//
// Each line is: NODE_TYPE [operator] [type and value] [block=N] [@source_line]
// Fields that still hold their "unset" value are not printed.
//
// @param out - stream to print to
// @param root - root of the (sub)tree, NULL prints "(empty tree)"
// @param attr_tab - table used to resolve identifier names and literal values
void ast_print(FILE *out, const ast_node *root, const attribute_table *attr_tab);

#endif
