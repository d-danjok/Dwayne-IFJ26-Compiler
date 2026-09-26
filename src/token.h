#ifndef TOKEN_H
#define TOKEN_H

typedef enum
{
    // TODO: complete list
    TOKEN_INT,     // 1,2,3...
    TOKEN_DOUBLE,  // 1234235,1245123456,21435
    TOKEN_STRING,  // "str", """str_multiline"""
    TOKEN_UNIT,    // unit
    TOKEN_BOOLEAN, // true, false

    TOKEN_IDENTIFIER, // var_name, func_name...
    TOKEN_KEYWORD,    // if, else, while...

    TOKEN_DOT,           // .
    TOKEN_COMMA,         // ,
    TOKEN_OPEN_PAREN,    // (
    TOKEN_CLOSE_PAREN,   // )
    TOKEN_OPEN_BRACE,    // {
    TOKEN_CLOSE_BRACE,   // }
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
    TOKEN_EOF,           // EOF
    TOKEN_EOL,           // \n
} token_type;

struct token_s;
typedef struct token_s token;

#endif
