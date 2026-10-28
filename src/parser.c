// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "stack.h"
#include "token.h"

#define EMPTY_TOKEN                                                                                                    \
    (token){.type = TOKEN_UNDEFINED, .attribute_index = -1, .line = 0}                                                 \
    // TODO: is adapt for actual implementation on lexer branch

typedef struct
{
    bool was_stored;
    token content;
} token_input_buffer;

// globals
static bool last_token_was_operand = false; // needed for identifying if minus is or is not unary
static size_t current_paren_depth =
    0; // needed to track if we are inside of parentheses to ignore indents and new lines
static token_input_buffer token_buffer = {.was_stored = false, .content = EMPTY_TOKEN};

typedef enum
{
    ITEM_TOKEN,
    ITEM_AST_NODE,
    ITEM_MARKER,
    ITEM_TERMINATOR
} expression_parser_stack_item_type;

typedef struct
{
    expression_parser_stack_item_type content_type;

    union {
        token item_token;
        ast_node *item_ast_node;
        void *item_marker;
    };
} expression_parser_stack_item;

static expression_parser_stack_item *new_expression_parser_stack_item(error *err)
{
    *err = NONE;
    expression_parser_stack_item *new_item =
        (expression_parser_stack_item *)utils_safe_malloc(err, sizeof(expression_parser_stack_item));
    if (*err != NONE)
    {
        return NULL;
    }
    return new_item;
}

typedef enum
{
    OP_PLUS,
    OP_MINUS,

    OP_MULTIPLY,
    OP_DIVIDE,

    OP_UNARY_MINUS,

    // comp operators
    OP_LESS,          // <
    OP_LESS_EQUAL,    // <=
    OP_GREATER,       // >
    OP_GREATER_EQUAL, // >=

    OP_EQUALS,
    OP_NOT_EQUALS,

    OP_NOT, // !
    OP_AND, // &&
    OP_OR,  // ||

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
    //  +   -   *   /   u-  <   <=  >   >=  ==  !=  !   &&  ||  (   )   Op  $
    {GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // +
    {GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // -
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // *
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // /
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // u-
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // <
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // <=
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // >
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // >=
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // ==
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // !=
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // !
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, GR, GR, LS, GR, LS, GR}, // &&
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, GR, LS, GR, LS, GR}, // ||
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, EQ, LS, IV}, // (
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR}, // )
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR}, // Operand
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, LS, EQ}, // $
};

// helpers
static token peek_token(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    if (token_buffer.was_stored) // if token is already in buffer
    {
        return token_buffer.content;
    }

    token_buffer.content = lexer_get_token(); // placeholder TODO: replace with actual lexer function
    if (*err != NONE)
    {
        return EMPTY_TOKEN;
    }
    token_buffer.was_stored = true;

    return token_buffer.content;
}

static token get_token(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    if (!(token_buffer.was_stored))
    {
        return lexer_get_token(); // placeholder TODO: replace with actual lexer function
    }

    const token token_to_return = token_buffer.content;

    token_buffer.content = EMPTY_TOKEN;
    token_buffer.was_stored = false;

    return token_to_return;
}

static bool is_expression_member(const token tok)
{
    // TODO: adapt for actual token implementation
    switch (tok.type)
    {
    case TOKEN_IDENTIFIER:
    case TOKEN_INT:
    case TOKEN_DOUBLE:
    case TOKEN_STRING:
    case TOKEN_BOOLEAN:
    case TOKEN_OPEN_PAREN:
    case TOKEN_CLOSE_PAREN:
        return true;
        break;

    case TOKEN_EOL:
        if (current_paren_depth > 0)
        {
            return true;
        }

    default:
        return false;
    }
}

// LL-rule function representations declaration
static ast_node *node_program(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_def_list(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_val_def(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_var_def(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_def_body(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_type_notation(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_func_def(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_main_func(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_func_start(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_func_head(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_func_name(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_param_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_param_list_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_param(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_func_body(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_block(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_stat_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_stat_list_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_statement(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_definition(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_expression(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_while_loop(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_return_statement(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_assign_val(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_if_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_if_block(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_else_block(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_func_call(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_params(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_params_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_param(error *err, FILE *src_stream, attribute_table *attr_tab);

// globals
ast_node *parser_run(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    return node_program(err, src_stream, attr_tab);
}

static ast_node *node_expression(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    stack *expression_parse_stack = new_stack(err);
    if (*err != NONE)
    {
        return NULL;
    }

    // preparing stack
    expression_parser_stack_item *terminator = new_expression_parser_stack_item(err);
    if (*err != NONE)
    {
        stack_destruct(&expression_parse_stack);
        return NULL;
    }
    terminator->content_type = ITEM_TERMINATOR;
    stack_push(expression_parse_stack, err, (void **)&terminator);

    token current_token = EMPTY_TOKEN;
    token next_token = peek_token(err, src_stream, attr_tab);

    while (is_expression_member(next_token))
    {
        current_token = get_token(err, src_stream, attr_tab);

        // TODO: implement actual algorithm
    }

    return NULL; // tmp
}