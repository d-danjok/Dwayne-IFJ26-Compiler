// @file lexer.h
//
// @author 	Maksym Sereda (xseredmd00)
#ifndef LEXER_H
#define LEXER_H

#include "attribute_table.h"
#include "error.h"
#include "stack.h"
#include "token.h"

#include <stdbool.h>
#include <stddef.h>

#define BUFFER_SIZE 128

typedef struct
{
    FILE *input;
    attribute_table *attr_table;
    char *buffer;
    size_t buffer_index;
    size_t buffer_capacity;
    int coment_depth;
    bool comment_sline;
    stack *indent_size_s;
    int indent_cnt;
    size_t current_line;
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
    STATE_MULTIPLE_STR,
    STATE_QUOTE_1,
    STATE_QUOTE_2,
    STATE_ESCAPE,
    STATE_STR,

    // Integer and double
    STATE_INT,
    STATE_UINT,
    STATE_ZERO,
    STATE_UZERO,
    STATE_DOT,
    STATE_DOT_OR_DOUBLE,
    STATE_DOUBLE,
    STATE_UDOUBLE,
    STATE_D_SUFF,
    STATE_EXP,
    STATE_EXP_ZERO,
    STATE_UEXP_ZERO,
    STATE_EXP_SIGN,
    STATE_EXP_PART,
    STATE_UEXP_PART,

    STATE_S_COMMENT,
    STATE_M_COMMENT,
    STATE_WAITING_EOL,

    STATE_EOL, // to detect indentation and dedentation, but not to return it as a
    STATE_INDENT_DIV,
    // token
} state;

// @brief creates new attribute_table
//
// @param err - pointer to error variable(INTERNAL_MEMORY_ALLOCATION if creation
// fails)
// @param input - stream from which lexer will read characters
// @param attr_table - attribute table for storing token attributes
//
// @result - pointer to newly created lexer structure or NULL if creation fails
lexer *new_lexer(error *err, FILE *input, attribute_table *attr_table);

// @brief frees lexer and its internal buffers (does not close input or free attr_table)
//
// @param self - pointer to lexer pointer, set to NULL after freeing
void lexer_destruct(lexer **self);

// @brief gets next token from input stream and returns it
//
// @param self - pointer to lexer structure
// @param err - pointer to error variable
//     (if error occurs, token.type will be TOKEN_UNDEFINED)
// @return next token from input stream
token lexer_get_next_token(lexer *self, error *err, state current_state);

#endif
