// @file token.h
//
// @author Daniil Didenko (xdidend00)

#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>

typedef enum
{
	// TODO: complete list
	TOKEN_UNDEFINED,

	TOKEN_INT,     // 1,2,3...
	TOKEN_DOUBLE,  // 2.2,122.05e2,21e435
	TOKEN_STRING,  // "str", """str_multiline"""
	TOKEN_UNIT,    // unit
	TOKEN_BOOLEAN, // true, false

	TOKEN_IDENTIFIER, // var_name, func_name...
	TOKEN_KEYWORD,    // if, else, while...

	TOKEN_DOT,           // .
	TOKEN_COMMA,         // ,
	TOKEN_OPEN_PAREN,    // (
	TOKEN_CLOSE_PAREN,   // )
	TOKEN_OPEN_BRACKET,  // [
	TOKEN_CLOSE_BRACKET, // ]
	TOKEN_ASSIGN,        // =
	TOKEN_PLUS,          // +
	TOKEN_MINUS,         // -
	TOKEN_MULTIPLY,      // *
	TOKEN_DIVISION,      // /
	TOKEN_EQUAL,         // ==
	TOKEN_NOT_EQUAL,     // !=
	TOKEN_GREATER,       // >
	TOKEN_EQUAL_GREATER, // >=
	TOKEN_LESS,          // <
	TOKEN_EQUAL_LESS,    // <=
	//TOKEN_EOF,           // EOF
	TOKEN_EOL,           // \n

	TOKEN_INDENT, // ADD INDENTATION
	TOKEN_DEDENT, // DECREASE INDENTATION
} token_type;

typedef struct
{
	token_type type;
	int attribute_index; // Index of item in type-scoped attribute table
	int line;            // only for error messages
} token;

#define TOKEN_GET_VAL(token) //TODO: implement this using attribute_table_get_...

#endif
