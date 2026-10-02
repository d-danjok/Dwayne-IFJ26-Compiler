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

// @brief gets next token from input stream and returns it
//
// @param self - pointer to lexer structure
// @param err - pointer to error variable
//     (if error occurs, token.type will be TOKEN_UNDEFINED)
// @return next token from input stream
token lexer_get_next_token(lexer *self, error *err);

#endif