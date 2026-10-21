// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "token.h"

#define EMPTY_TOKEN (token){.type = TOKEN_UNDEFINED, .attribute_index = -1, .line = 0}

#define TOKEN_BUFFER_SIZE 3

typedef struct
{
    int capacity;
    int last_stored_index; // -1 if is empty
    //       v
    // [ 0 , 1 , _ ]

    token content[];
} token_input_buffer;

static token_input_buffer* new_token_input_buffer(error* err)
{
    token_input_buffer* new_buff =
            (token_input_buffer*)utils_safe_malloc(err, sizeof(token_input_buffer) + TOKEN_BUFFER_SIZE * sizeof(token));
    if (*err != NONE)
    {
        return NULL;
    }

    new_buff->capacity = TOKEN_BUFFER_SIZE;
    new_buff->last_stored_index = -1;

    for (int i = 0; i < TOKEN_BUFFER_SIZE; i++)
    {
        new_buff->content[i] = EMPTY_TOKEN;
    }

    return new_buff;
}

static token peek_token(error* err, int depth, token_input_buffer* token_buffer)
{
    *err = NONE;
    //depth 0 is first item (next token)
    if (depth >= TOKEN_BUFFER_SIZE || depth < 0)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return EMPTY_TOKEN;
    }

    if (token_buffer->last_stored_index >= depth) //if token is already in buffer
    {
        return token_buffer->content[depth];
    }

    for (int i = token_buffer->last_stored_index + 1; i <= depth; i++)
    {
        token_buffer->content[i] = lexer_get_token(); //placeholder TODO: replace with actual lexer function
        if (*err != NONE)
        {
            return EMPTY_TOKEN;
        }
        token_buffer->last_stored_index = i;
    }

    return token_buffer->content[depth];
}

static token get_token(error* err, token_input_buffer* token_buffer)
{
    *err = NONE;

    if (token_buffer->last_stored_index == -1)
    {
        return lexer_get_token(); //placeholder TODO: replace with actual lexer function
    }

    token token_to_return =  token_buffer->content[0];

    for (int i = 0; i < token_buffer->last_stored_index; i++)
    {
        token_buffer->content[i] = token_buffer->content[i + 1];
    }
    token_buffer->content[token_buffer->last_stored_index] = EMPTY_TOKEN;
    token_buffer->last_stored_index--;

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
