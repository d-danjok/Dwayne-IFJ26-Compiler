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

    TOKEN_WHITESPACE, // ' ', '\t', '\r', '\n'
    TOKEN_EOL,        // \n
    TOKEN_EOF,        // EOF

    TOKEN_INT,     // 1,2,3...
    TOKEN_DOUBLE,  // 2.2,122.05e2,21e435
    TOKEN_STRING,  // "str", """str_multiline"""
    TOKEN_UNIT,    // unit
    TOKEN_BOOLEAN, // true, false

    TOKEN_ANNOTATION, // @annotation
    TOKEN_IDENTIFIER, // var_name, func_name...
    TOKEN_KEYWORD,    // if, else, while...
    TOKEN_OPERATOR,   // +, -, *, /, =, ==, !=, >= ...

    TOKEN_DOT,         // .
    TOKEN_COMMA,       // ,
    TOKEN_UNDERSCORE,  // _
    TOKEN_SEPARATOR,   // |
    TOKEN_OPEN_PAREN,  // (
    TOKEN_CLOSE_PAREN, // )
    TOKEN_COLON,       // :

    TOKEN_INDENT, // ADD INDENTATION
    TOKEN_DEDENT, // DECREASE INDENTATION
} token_type;

typedef enum
{
    OP_ASSIGN,        // =
    OP_PLUS,          // +
    OP_MINUS,         // -
    OP_MULTIPLY,      // *
    OP_DIVISION,      // /
    OP_EQUAL,         // ==
    OP_NOT_EQUAL,     // !=
    OP_GREATER,       // >
    OP_EQUAL_GREATER, // >=
    OP_LESS,          // <
    OP_EQUAL_LESS,    // <=
} operator_list;

typedef struct
{
    token_type type;
    int attribute_index; // Index of item in type-scoped attribute table
    // Notes:
    // 1) in case of bool: 0 - false , 1 - true
    // 2) in case of keyword: value of type of keyword enum
    // 3) in case of operator: value of type of operator enum

    int line; // only for error messages
} token;

#define TOKEN_GET_VAL(token) // TODO: implement this using attribute_table_get_...

#endif
