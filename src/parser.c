// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "token.h"

#define EMPTY_TOKEN (token){.type = TOKEN_UNDEFINED, .attribute_index = -1, .line = 0}

typedef struct
{
    bool was_stored;
    token content;
} token_input_buffer;

static token_input_buffer new_token_input_buffer()
{
    return (token_input_buffer){.was_stored = false, .content = EMPTY_TOKEN};
}

static token peek_token(error* err, token_input_buffer* token_buffer)
{
    *err = NONE;

    if (token_buffer->was_stored) //if token is already in buffer
    {
        return token_buffer->content;
    }

    token_buffer->content = lexer_get_token(); //placeholder TODO: replace with actual lexer function
    if (*err != NONE)
    {
        return EMPTY_TOKEN;
    }
    token_buffer->was_stored = true;

    return token_buffer->content;
}

static token get_token(error* err, token_input_buffer* token_buffer)
{
    *err = NONE;

    if (!(token_buffer->was_stored))
    {
        return lexer_get_token(); //placeholder TODO: replace with actual lexer function
    }

    token token_to_return = token_buffer->content;

    token_buffer->content = EMPTY_TOKEN;
    token_buffer->was_stored = false;

    return token_to_return;
}

typedef enum
{
    ITEM_TOKEN,
    ITEM_AST_NODE,
    ITEM_MARKER
} expression_parser_stack_item_type;

typedef struct
{
    expression_parser_stack_item_type content_type;

    union
    {
        token item_token;
        ast_node* item_ast_node;
        void* item_marker;
    };
} expression_parser_stack_item_tagged_union;

typedef enum
{
    OP_PLUS,
    OP_MINUS,

    OP_MULTIPLY,
    OP_DIVIDE,

    OP_UNARY_MINUS,

    // comp operators
    OP_LESS, // <
    OP_LESS_EQUAL, // <=
    OP_GREATER, // >
    OP_GREATER_EQUAL, // >=

    OP_EQUALS,
    OP_NOT_EQUALS,

    LPAREN, // (
    RPAREN, // )

    OPERAND,

    EXPR_TERMINATOR
} precedence_table_member;

typedef enum
{
    LS, // less
    EQ, // equal
    GR, // greater

    IV, // invalid
} operator_priority;

static const operator_priority precedence_table[EXPR_TERMINATOR + 1][EXPR_TERMINATOR + 1] = {
    //
    { GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, EQ, LS, IV },
    { GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR },
    { GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR },
    { LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, LS, EQ },
};

ast_node* process_expression(error* err, FILE* src_stream, ast_node* ast_node);

ast_node* parser_run(error* err, FILE* src_stream, attribute_table* attr_tab); //TODO: implement
