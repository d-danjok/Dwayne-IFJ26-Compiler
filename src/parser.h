// @file 	parser.h
//
// @brief	parser function declaration
// @author 	Daniil Didenko (xdidend00)

#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "error.h"
#include "symtable.h"

// @brief run parser (implicitly also runs lexer)
//
// @param src_stream - source code stream
// @param attr_tab - attribute table which will be used internally and by other functions afterwards (no ownership
// transition)
// @param symb_tab - symbolic table which will be used internally and by other functions afterwards (no ownership
// transition)
// @return syntax tree represented as a pointer to root of tree build with ast_node structs
ast_node *parser_run(error *err, FILE *src_stream, attribute_table *attr_tab);

#endif
