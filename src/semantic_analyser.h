// @file    semantic_analyser.h
//
// @brief   semantic analysis of the syntax tree
// @author  Vladyslav Doroshenko (xdorosv00)

#ifndef SEMANTIC_ANALYSER_H
#define SEMANTIC_ANALYSER_H

#include "ast.h"
#include "attribute_table.h"
#include "error.h"
#include "stack.h"
#include "symtable.h"
#include "utils.h"

// @brief run semantic analysis on the syntax tree from the parser
//
// @param parser_tree - tree from parser_run (ownership transition, *parser_tree is set to NULL)
// @param attr_tab - attribute table filled by the lexer (no ownership transition)
// @return decorated syntax tree, NULL after an error (the tree is freed then)
ast_node *semantic_analyser_run(error *err, ast_node **parser_tree, attribute_table *attr_tab);

#endif // SEMANTIC_ANALYSER_H
