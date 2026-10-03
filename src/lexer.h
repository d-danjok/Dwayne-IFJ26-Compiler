// @file lexer.h
//
// @author 	Maksym Sereda (xseredmd00)
#ifndef LEXER_H
#define LEXER_H

#include "attribute_table.h"
#include "error.h"
#include "token.h"

#include <stdio.h>

typedef struct
{
    FILE *input;
    attribute_table *attr_table;
    int *current_line;
} lexer;

typedef enum 
{
    STATE_START,
    // Operators
    STATE_DIVIDE,
    STATE_EQUAL,
    STATE_NOT,
    STATE_MORE,
    STATE_LESS,

    // Identifiers
    STATE_IDENTIFIER,
    STATE_UNDERSCORE,

    STATE_ANNOTATION,
    // Literals
    STATE_STR_START,
    STATE_END_OR_MULTIPLE,
    STATE_ESCAPE,
    STATE_STR,

    // Integer and double
    STATE_INT,
    STATE_ZERO,
    STATE_DOT,
    STATE_DOT_OR_DOUBLE,
    STATE_DOUBLE,
    STATE_LITERAL,
    STATE_OPERATOR,
    STATE_COMMENT,
    STATE_WHITESPACE,
    STATE_EOL,
    STATE_S_COMMENT,
    STATE_M_COMMENT,
    STATE_EOF
} state;

// @brief gets next token from input stream and returns it
//
// @param self - pointer to lexer structure
// @param err - pointer to error variable
//     (if error occurs, token.type will be TOKEN_UNDEFINED)
// @return next token from input stream
token lexer_get_next_token(lexer *self, error *err, state current_state);

#endif