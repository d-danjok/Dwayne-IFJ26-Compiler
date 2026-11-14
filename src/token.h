// @file token.h
//
// @author Daniil Didenko (xdidend00)

#ifndef TOKEN_H
#define TOKEN_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    // TODO: complete list
    TOKEN_UNDEFINED,

    TOKEN_EOL, // \n
    TOKEN_EOF, // EOF

    TOKEN_INT,     // 1,2,3...
    TOKEN_DOUBLE,  // 2.2,122.05e2,21e435
    TOKEN_STRING,  // "str", """str_multiline"""
    TOKEN_UNIT,    // unit
    TOKEN_BOOLEAN, // true, false

    TOKEN_ANNOTATION, // @annotation
    TOKEN_IDENTIFIER, // var_name, func_name...
    TOKEN_KEYWORD,    // if, else, while...
    TOKEN_OPERATOR,   // +, -, *, /, =, ==, !=, >= ...

    TOKEN_DOT,        // .
    TOKEN_COMMA,      // ,
    TOKEN_UNDERSCORE, // _
    TOKEN_COLON,      // :
    TOKEN_SEMICOLON,  // ;

    TOKEN_OPEN_PAREN,    // (
    TOKEN_CLOSE_PAREN,   // )
    TOKEN_OPEN_BRACKET,  // [ (LISTS extension)
    TOKEN_CLOSE_BRACKET, // ]
    TOKEN_OPEN_BRACE,    // { (only inside ${...}(interpolated string extension))
    TOKEN_CLOSE_BRACE,   // }

    TOKEN_INTERP_BEGIN, // s" or s"""       (INTERPOLATEDS extension)
    TOKEN_INTERP_END,   // closing " or """
    // s"a$x${e}b" -> INTERP_BEGIN STRING("a") IDENTIFIER(x) OPEN_BRACE ... CLOSE_BRACE STRING("b") INTERP_END

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
    OP_NOT,           // !  (BOOLTHEN)
    OP_AND,           // &&
    OP_SEPARATOR,     // | (ALGEBDT extension)
    OP_OR,            // ||
    OP_CONCAT,        // ++
    OP_ARROW,         // => (LAMBDAS extension)
    OP_LEFT_ARROW,    // <- (CONTROL extension)
} operator_list;

typedef enum
{
    KW_DEF,
    KW_DO,
    KW_DOUBLE,
    KW_ELSE,
    KW_IF,
    KW_IMPORT,
    KW_INT,
    KW_RETURN,
    KW_STRING,
    KW_THEN,
    KW_UNIT,
    KW_VAL,
    KW_VAR,
    KW_WHILE,

    KW_BOOLEAN,
    KW_CASE,
    KW_CLASS,
    KW_EXTENSION,
    KW_FOR,
    KW_LIST,
    KW_MATCH,
    KW_TYPE
} keyword_list;

typedef struct
{
    token_type type;
    int attribute_index; // Index of item in type-scoped attribute table
    // Notes:
    // 1) in case of bool: 0 - false , 1 - true
    // 2) in case of keyword: value of type of keyword enum
    // 3) in case of operator: value of type of operator enum

    size_t line; // only for error messages(used size_t to avoid hypothetical overflow of int)
} token;

#define TOKEN_GET_VAL(token) // TODO: implement this using attribute_table_get_...

#endif
