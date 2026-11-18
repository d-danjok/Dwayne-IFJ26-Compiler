// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdbool.h>
#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "lexer.h"
#include "stack.h"
#include "token.h"
#include "utils.h"

#define EMPTY_TOKEN (token){.type = TOKEN_UNDEFINED, .attribute_index = -1, .line = 0}

typedef struct
{
    bool was_stored;
    token content;
} token_input_buffer;

// globals
static size_t current_paren_depth =
    0; // needed to track if we are inside of parentheses to ignore indents and new lines
static token_input_buffer token_buffer = {
    .was_stored = false,
    .content = {.type = TOKEN_UNDEFINED, .attribute_index = -1, .line = 0},
};
static lexer *lex;
static attribute_table *attribute_tab;

typedef enum
{
    PRECEDENTIAL_OP_PLUS,
    PRECEDENTIAL_OP_MINUS,

    PRECEDENTIAL_OP_MULTIPLY,
    PRECEDENTIAL_OP_DIVIDE,

    PRECEDENTIAL_OP_UNARY_MINUS,

    // comp operators
    PRECEDENTIAL_OP_LESS,          // <
    PRECEDENTIAL_OP_LESS_EQUAL,    // <=
    PRECEDENTIAL_OP_GREATER,       // >
    PRECEDENTIAL_OP_GREATER_EQUAL, // >=

    PRECEDENTIAL_OP_EQUALS,
    PRECEDENTIAL_OP_NOT_EQUALS,

    PRECEDENTIAL_OP_NOT, // !
    PRECEDENTIAL_OP_AND, // &&
    PRECEDENTIAL_OP_OR,  // ||

    PRECEDENTIAL_LPAREN, // (
    PRECEDENTIAL_RPAREN, // )

    PRECEDENTIAL_OPERAND,

    PRECEDENTIAL_EXPR_TERMINATOR,

    PRECEDENTIAL_NON_EVALUATABLE, // not actually an item, it just needs to be here for tech reason
} precedence_table_member;

typedef enum
{
    LS, // less    <
    EQ, // equal  ==
    GR, // greater >

    IV, // invalid
} operator_priority;

//                                              top of stack                      current
static const operator_priority precedence_table[PRECEDENTIAL_EXPR_TERMINATOR + 1][PRECEDENTIAL_EXPR_TERMINATOR + 1] = {
    //  +   -   *   /   u-  <   <=  >   >=  ==  !=  !   &&  ||  (   )   Op  $
    {GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // +
    {GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // -
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // *
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // /
    {GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // u-
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // <
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // <=
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // >
    {LS, LS, LS, LS, LS, IV, IV, IV, IV, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // >=
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // ==
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, IV, LS, GR, GR, LS, GR, LS, GR}, // !=
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, LS, GR, GR, LS, GR, LS, GR}, // !
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, GR, GR, LS, GR, LS, GR}, // &&
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, GR, LS, GR, LS, GR}, // ||
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, EQ, LS, IV}, // (
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR}, // )
    {GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR}, // Operand
    {LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, LS, EQ}, // $
};

typedef enum
{
    ITEM_NONE,

    ITEM_AST_NODE,
    ITEM_TERMINATOR,
    ITEM_PAREN,
} parser_stack_item_type;

typedef struct
{
    parser_stack_item_type item_type;

    precedence_table_member precedential_type;

    bool is_marked; // represents '<' mark before symbol (fromm presentation ifj08-cz.pdf)

    ast_node *item_ast_node;

} parser_stack_item;

static parser_stack_item *new_parser_stack_item(error *err, const precedence_table_member precedential_type)
{
    *err = NONE;
    parser_stack_item *new_item = (parser_stack_item *)utils_safe_malloc(err, sizeof(parser_stack_item));
    if (*err != NONE)
    {
        return NULL;
    }

    new_item->precedential_type = precedential_type;

    new_item->item_type = ITEM_NONE;
    new_item->item_ast_node = NULL;
    new_item->is_marked = false;

    return new_item;
}

static void parser_stack_item_destruct(parser_stack_item **self)
{
    if (self == NULL || *self == NULL)
    {
        return;
    }

    switch ((*self)->item_type)
    {
    case ITEM_AST_NODE:
        ast_node_destruct_recursively(&(*self)->item_ast_node);
        /* fall through */
    default:
        UTILS_SAFE_FREE(self);
    }
}

// helpers
static token peek_token(error *err)
{
    *err = NONE;

    if (token_buffer.was_stored) // if token is already in buffer
    {
        return token_buffer.content;
    }

    token_buffer.content = lexer_get_next_token(lex, err);
    if (*err != NONE)
    {
        return EMPTY_TOKEN;
    }
    token_buffer.was_stored = true;

    return token_buffer.content;
}

static token get_token(error *err)
{
    *err = NONE;

    if (!(token_buffer.was_stored))
    {
        return lexer_get_next_token(lex, err);
    }

    const token token_to_return = token_buffer.content;

    token_buffer.content = EMPTY_TOKEN;
    token_buffer.was_stored = false;

    return token_to_return;
}

static ast_node *id_node_from_token(error *err, const token tok)
{
    *err = NONE;

    ast_node *id_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    id_node->type = AST_NODE_IDENTIFIER;
    id_node->src_line_number = tok.line;
    id_node->attribute_index = tok.attribute_index;
    id_node->literal_type = AST_TYPE_NONE;

    return id_node;
}

static parser_stack_item *literal_stack_item_from_token(error *err, const token tok)
{
    *err = NONE;

    ast_node *node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    node->type = AST_NODE_LITERAL;
    node->src_line_number = tok.line;
    node->attribute_index = tok.attribute_index;

    switch (tok.type)
    {
    case TOKEN_INT:
        node->literal_type = AST_TYPE_INT;

        break;

    case TOKEN_DOUBLE:
        node->literal_type = AST_TYPE_DOUBLE;

        break;

    case TOKEN_STRING:
        node->literal_type = AST_TYPE_STRING;

        break;

    case TOKEN_UNIT:
        node->literal_type = AST_TYPE_UNIT;

        break;

    case TOKEN_BOOLEAN:
        node->literal_type = AST_TYPE_BOOLEAN;

        break;

    default:
        *err = GENERAL_INTERNAL;
        ast_node_destruct_recursively(&node);
        return NULL;
    }

    parser_stack_item *new_item = new_parser_stack_item(err, PRECEDENTIAL_OPERAND);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&node);
        return NULL;
    }
    new_item->item_ast_node = node;
    new_item->item_type = ITEM_AST_NODE;

    return new_item;
}

static bool is_expression_member(const token tok)
{
    switch (tok.type)
    {
    case TOKEN_IDENTIFIER:
    case TOKEN_INT:
    case TOKEN_DOUBLE:
    case TOKEN_STRING:
    case TOKEN_BOOLEAN:
    case TOKEN_UNIT:
    case TOKEN_OPERATOR:
    case TOKEN_OPEN_PAREN:
    case TOKEN_CLOSE_PAREN:
        return true;

    case TOKEN_EOL:
        if (current_paren_depth > 0)
        {
            return true;
        }
        break;

    default:
        return false;
    }

    return false;
}

static void log_token_type(const token_type type)
{
    switch (type)
    {
    case TOKEN_EOL:
        log_printf("end of line");
        break;
    case TOKEN_EOF:
        log_printf("end of file");
        break;
    case TOKEN_INT:
        log_printf("int literal");
        break;
    case TOKEN_DOUBLE:
        log_printf("double literal");
        break;
    case TOKEN_STRING:
        log_printf("string literal");
        break;
    case TOKEN_UNIT:
        log_printf("unit");
        break;
    case TOKEN_BOOLEAN:
        log_printf("boolean literal");
        break;
    case TOKEN_ANNOTATION:
        log_printf("@");
        break;
    case TOKEN_IDENTIFIER:
        log_printf("identifier");
        break;
    case TOKEN_KEYWORD:
        log_printf("keyword");
        break;
    case TOKEN_OPERATOR:
        log_printf("operator");
        break;
    case TOKEN_DOT:
        log_printf("dot");
        break;
    case TOKEN_COMMA:
        log_printf("comma");
        break;
    case TOKEN_UNDERSCORE:
        log_printf("_");
        break;
    case TOKEN_COLON:
        log_printf("colon (:)");
        break;
    case TOKEN_SEMICOLON:
        log_printf("semicolon (;)");
        break;

    case TOKEN_OPEN_PAREN:
        log_printf("open paren");
        break;
    case TOKEN_CLOSE_PAREN:
        log_printf("close paren");
        break;
    case TOKEN_OPEN_BRACKET:
        log_printf("open bracket");
        break;
    case TOKEN_CLOSE_BRACKET:
        log_printf("close bracket");
        break;
    case TOKEN_INDENT:
        log_printf("indent");
        break;
    case TOKEN_DEDENT:
        log_printf("dedent");
        break;
    default:
        log_printf("unknown");
    }
}

static void log_error_expected(const token_type expected, const token_type got, const size_t line)
{
    log_printf("[%lu] expected: ", line);
    log_token_type(expected);

    log_printf(" got: ");
    log_token_type(got);

    log_printf("\n");
}

static bool is_type(const token tok)
{
    if (tok.type != TOKEN_KEYWORD)
    {
        return false;
    }

    switch (tok.attribute_index)
    {
    case KW_BOOLEAN:
    case KW_INT:
    case KW_DOUBLE:
    case KW_STRING:
    case KW_UNIT:
        return true;

    default:
        return false;
    }

    return false;
}

static bool is_unary_op_node(const ast_node *node)
{
    return node->type == AST_NODE_UNARY_OPERATION;
}

static bool is_binary_op_node(const ast_node *node)
{
    return node->type == AST_NODE_BINARY_OPERATION;
}

static bool is_valid_operand(const ast_node *node)
{
    return node->type == AST_NODE_LITERAL || node->type == AST_NODE_IDENTIFIER || node->type == AST_NODE_FUNC_CALL ||
           node->type == AST_NODE_UNARY_OPERATION || node->type == AST_NODE_BINARY_OPERATION;
}

static void parse_stack_append(stack *parse_stack, error *err, parser_stack_item **item_to_append,
                               const size_t current_line)
{
    *err = NONE;

    int current_peek_depth = 0;
    parser_stack_item virtual_terminator = {.item_type = ITEM_TERMINATOR,
                                            .precedential_type = PRECEDENTIAL_EXPR_TERMINATOR};
    bool uses_virtual_terminator = false;

    parser_stack_item *current_item = stack_peek(parse_stack, err, current_peek_depth);
    if (*err != NONE)
    {
        if (*err != INTERNAL_STACK_IS_EMPTY && *err != INTERNAL_INDEX_OUT_OF_BOUNDS)
        {
            return;
        }
        current_item = &virtual_terminator;
        uses_virtual_terminator = true;
        *err = NONE;
    }

    while (!uses_virtual_terminator && current_item->precedential_type == PRECEDENTIAL_NON_EVALUATABLE)
    {
        current_peek_depth++;

        current_item = stack_peek(parse_stack, err, current_peek_depth);
        if (*err != NONE)
        {
            if (*err != INTERNAL_STACK_IS_EMPTY && *err != INTERNAL_INDEX_OUT_OF_BOUNDS)
            {
                return;
            }
            current_item = &virtual_terminator;
            uses_virtual_terminator = true;
            *err = NONE;
        }
    }

    if (current_item->precedential_type == PRECEDENTIAL_LPAREN &&
        (*item_to_append)->precedential_type == PRECEDENTIAL_RPAREN)
    {
        if (current_peek_depth == 0)
        {
            parser_stack_item *lparen_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }

            parser_stack_item_destruct(&lparen_item);
            parser_stack_item_destruct(item_to_append);

            parser_stack_item *unit_item = new_parser_stack_item(err, PRECEDENTIAL_OPERAND);
            if (*err != NONE)
            {
                return;
            }

            ast_node *unit_node = new_ast_node(err);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&unit_item);
                return;
            }
            unit_node->type = AST_NODE_LITERAL;
            unit_node->literal_type = AST_TYPE_UNIT;
            unit_node->src_line_number = current_line;

            unit_item->item_ast_node = unit_node;
            unit_item->item_type = ITEM_AST_NODE;
            unit_item->is_marked = true;

            stack_push(parse_stack, err, (void **)&unit_item);
            return;
        }

        if (current_peek_depth == 1)
        {
            parser_stack_item *expression_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            parser_stack_item *lparen_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&expression_item);
                return;
            }
            parser_stack_item_destruct(&lparen_item);
            parser_stack_item_destruct(item_to_append);

            expression_item->precedential_type = PRECEDENTIAL_OPERAND;
            expression_item->is_marked = true;
            stack_push(parse_stack, err, (void **)&expression_item);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&expression_item);
            }
            return;
        }

        *err = GENERAL_SYNTAX;
        parser_stack_item_destruct(item_to_append);
        log_printf("[%lu] invalid character combination inside of an expression", current_line);
        return;
    }

    switch (precedence_table[current_item->precedential_type][(*item_to_append)->precedential_type])
    {
    case LS:
        (*item_to_append)->is_marked = true;
        /* fall through */
    case EQ:
        stack_push(parse_stack, err, (void **)item_to_append);
        if (*err != NONE)
        {
            return;
        }
        break;

    case GR:
        if (!(current_item->is_marked) && current_item->item_type != ITEM_PAREN)
        {
            *err = GENERAL_SYNTAX;
            log_printf("[%lu] invalid character combination inside of an expression", current_line);
            return;
        }

        if (current_item->item_type == ITEM_AST_NODE && current_item->precedential_type != PRECEDENTIAL_OPERAND &&
            is_unary_op_node(current_item->item_ast_node))
        {
            parser_stack_item *operand_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            if (operand_item == NULL || operand_item->item_ast_node == NULL)
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                parser_stack_item_destruct(&operand_item);
                return;
            }
            ast_node *operand = operand_item->item_ast_node;
            UTILS_SAFE_FREE(&operand_item);

            if (!is_valid_operand(operand))
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);
                ast_node_destruct_recursively(&operand);
                return;
            }

            parser_stack_item *operator_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            if (operator_item == NULL || operator_item->item_ast_node == NULL)
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                parser_stack_item_destruct(&operator_item);
                return;
            }

            operator_item->item_ast_node->left_child = operand;
            operator_item->is_marked = false;
            operator_item->precedential_type = PRECEDENTIAL_NON_EVALUATABLE;

            stack_push(parse_stack, err, (void **)&operator_item);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&operator_item);
                return;
            }
        }
        else if (current_item->item_type == ITEM_AST_NODE && current_item->precedential_type != PRECEDENTIAL_OPERAND &&
                 is_binary_op_node(current_item->item_ast_node))
        {
            parser_stack_item *right_operand_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            if (right_operand_item == NULL || right_operand_item->item_ast_node == NULL)
            {
                parser_stack_item_destruct(&right_operand_item);
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);
                return;
            }
            ast_node *right_operand = right_operand_item->item_ast_node;
            UTILS_SAFE_FREE(&right_operand_item);

            if (!is_valid_operand(right_operand))
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                ast_node_destruct_recursively(&right_operand);
                return;
            }

            parser_stack_item *operator_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            if (operator_item == NULL || operator_item->item_ast_node == NULL)
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                parser_stack_item_destruct(&operator_item);
                return;
            }

            parser_stack_item *left_operand_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                // error can only happen here if there is no left operand on stack,
                // that can only happen is expression was invalid
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                return;
            }
            if (left_operand_item == NULL || left_operand_item->item_ast_node == NULL)
            {
                *err = GENERAL_SYNTAX;
                log_printf("[%lu] invalid character combination inside of an expression", current_line);

                parser_stack_item_destruct(&left_operand_item);
                parser_stack_item_destruct(&operator_item);

                return;
            }
            ast_node *left_operand = left_operand_item->item_ast_node;
            UTILS_SAFE_FREE(&left_operand_item);

            if (!is_valid_operand(left_operand))
            {
                *err = GENERAL_SYNTAX;

                log_printf("[%lu] invalid character combination inside of an expression", current_line);
                ast_node_destruct_recursively(&left_operand);
                ast_node_destruct_recursively(&right_operand);
                ast_node_destruct_recursively(&operator_item->item_ast_node);
                operator_item->item_ast_node = NULL;
                parser_stack_item_destruct(&operator_item);

                return;
            }

            operator_item->item_ast_node->left_child = left_operand;
            operator_item->item_ast_node->right_child = right_operand;

            operator_item->is_marked = false;
            operator_item->precedential_type = PRECEDENTIAL_NON_EVALUATABLE;

            stack_push(parse_stack, err, (void **)&operator_item);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&operator_item);
                return;
            }
        }
        else if (current_item->item_type == ITEM_PAREN)
        {
            // we can not legally get here is there is more than 1 non-marked element not top of the stack
            parser_stack_item *expression_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }

            parser_stack_item *right_paren_item = stack_pop(parse_stack, err);
            if (*err != NONE)
            {
                return;
            }
            UTILS_SAFE_FREE(&right_paren_item);

            stack_push(parse_stack, err, (void **)&expression_item);
            if (*err != NONE)
            {
                parser_stack_item_destruct(&expression_item);
                return;
            }
        }
        else
        {
            current_item->is_marked = false;
            current_item->precedential_type = PRECEDENTIAL_NON_EVALUATABLE;
        }

        parse_stack_append(parse_stack, err, item_to_append, current_line); // trying till we can append an item

        break;

        // case GR end

    case IV:
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] invalid character combination inside of an expression", current_line);

        break;

    default:
        *err = INTERNAL_FATAL;

        log_printf("[%lu] FATAL: invalid data value in variable of type: operator_priority\n", current_line);
    } // switch end
}

static void parser_stack_destruct(stack **self, error *err)
{
    const error prev_err = *err;

    while (true)
    {
        parser_stack_item *item_to_free = stack_pop(*self, err);
        if (*err != NONE)
        {
            if (*err != INTERNAL_STACK_IS_EMPTY && *err != INTERNAL_INDEX_OUT_OF_BOUNDS)
            {
                if (prev_err != NONE) // if there was error previously it should be returned
                {
                    *err = prev_err;
                }
                return;
            }
            break;
        }
        parser_stack_item_destruct(&item_to_free);
    }
    stack_destruct(self);

    *err = prev_err;
}

// LL-rule function representations declaration
static ast_node *node_program(error *err);
static ast_node *node_def_list(error *err);

static ast_node *node_val_def(error *err);
static ast_node *node_var_def(error *err);
static ast_node *node_def_body(error *err);
static ast_node *node_type_notation(error *err);

static ast_node *node_func_def(error *err);
static ast_node *node_main_func(error *err);
static ast_node *node_func_start(error *err);
static ast_node *node_func_head(error *err);
static ast_node *node_func_name(error *err);
static ast_node *node_param_list(error *err);
static ast_node *node_param_list_(error *err);
static ast_node *node_param(error *err);
static ast_node *node_func_body(error *err);

static ast_node *node_block(error *err);
static ast_node *node_stat_list(error *err);
static ast_node *node_stat_list_(error *err, bool newline_consumed_by_caller);
static ast_node *node_statement(error *err);
static ast_node *node_definition(error *err);

static ast_node *node_expression_or_assignment_to_id(error *err, bool can_be_assignment);
static ast_node *node_while_loop(error *err);
static ast_node *node_return_statement(error *err);
static ast_node *node_assign_val(error *err);

static ast_node *node_if_list(error *err);
static ast_node *node_if_list_(error *err);
static ast_node *node_if_block(error *err);
static ast_node *node_else_block(error *err);

static ast_node *node_func_call(error *err);
// also used for identifiers inside expressions

static ast_node *node_call_params(error *err);
static ast_node *node_call_params_(error *err);
static ast_node *node_call_param(error *err);

// globals ------------------------------------------------------------------------------------------------------------
ast_node *parser_run(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    current_paren_depth = 0;
    token_buffer.was_stored = false;
    token_buffer.content = EMPTY_TOKEN;
    attribute_tab = attr_tab;

    lex = new_lexer(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *program = node_program(err);
    // error check makes no sense here, if it happened it will simply propagate and return will be NULL anyway

    lexer_destruct(&lex);

    return program;
}

// implementations --------------------------------------------------------------------------------------------------
static ast_node *node_program(error *err)
{
    *err = NONE;
    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_IMPORT)
    {
        *err = PROLOGUE_MISSING;
        log_printf("FATAL: [%lu] program should start with prologue\r\n", current_token.line);

        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = PROLOGUE_MISSING;
        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
        return NULL;
    }

    const int prologue_identifier_attribute_index = current_token.attribute_index;
    const char *const prologue_string =
        attribute_table_get_identifier(attribute_tab, err, prologue_identifier_attribute_index);
    if (*err != NONE)
    {
        return NULL;
    }

    if (strcmp(prologue_string, "ifj26") != 0)
    {
        *err = PROLOGUE_MISSING;
        log_printf("[%lu] %s can not be used as prologue\n", current_token.line, prologue_string);

        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_DOT)
    {
        *err = PROLOGUE_MISSING;
        log_error_expected(TOKEN_DOT, current_token.type, current_token.line);

        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_OPERATOR || current_token.attribute_index != OP_MULTIPLY)
    {
        *err = PROLOGUE_MISSING;

        log_printf("[%lu] expected: '*', got: ", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");

        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_EOL)
    {
        *err = PROLOGUE_MISSING;
        log_error_expected(TOKEN_EOL, current_token.type, current_token.line);
        return NULL;
    }

    return node_def_list(err);
}

static ast_node *node_def_list(error *err)
{
    *err = NONE;
    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *node_to_append = NULL; // added NULL initialisation here in order to be consistent
    if (next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF)
    {
        node_to_append = node_func_def(err);
    }
    else if (next_token.type == TOKEN_ANNOTATION)
    {
        node_to_append = node_main_func(err);
    }
    else if (next_token.type == TOKEN_EOF)
    {
        return NULL;
    }
    else
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_def_list_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&node_to_append);
        return NULL;
    }
    func_def_list_node->type = AST_NODE_DEF_LIST;
    func_def_list_node->src_line_number = next_token.line;

    func_def_list_node->left_child = node_to_append;

    node_to_append = node_def_list(err);
    func_def_list_node->right_child = node_to_append;
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_list_node);
        return NULL;
    }

    return func_def_list_node;
}

static ast_node *node_val_def(error *err)
{
    *err = NONE;
    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_VAL))
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *val_def = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    val_def->type = AST_NODE_VAL_DEF;
    val_def->src_line_number = current_token.line;

    val_def->left_child = node_def_body(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&val_def);
        return NULL;
    }

    return val_def;
}

static ast_node *node_var_def(error *err)
{
    *err = NONE;
    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_VAR))
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *var_def = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    var_def->type = AST_NODE_VAR_DEF;
    var_def->src_line_number = current_token.line;

    var_def->left_child = node_def_body(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&var_def);
        return NULL;
    }

    return var_def;
}

static ast_node *node_def_body(error *err)
{
    *err = NONE;
    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *def_body = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    def_body->type = AST_NODE_DEF_BODY;
    def_body->src_line_number = current_token.line;

    def_body->left_child = id_node_from_token(err, current_token);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&def_body);
        return NULL;
    }

    if (next_token.type != TOKEN_COLON)
    {
        return def_body;
    }

    def_body->right_child = node_type_notation(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&def_body);
        return NULL;
    }

    return def_body;
}

static ast_node *node_type_notation(error *err)
{
    *err = NONE;
    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *type_notation = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    type_notation->type = AST_NODE_TYPE_NOTATION;
    type_notation->src_line_number = current_token.line;

    ast_node *type = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&type_notation);
        return NULL;
    }
    type->type = AST_NODE_TYPE;
    type->src_line_number = next_token.line;

    if (!is_type(next_token))
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected: type got: ", next_token.line);
        log_token_type(next_token.type);
        log_printf("\n");

        ast_node_destruct_recursively(&type_notation);
        ast_node_destruct_recursively(&type);
        return NULL;
    }

    switch (next_token.attribute_index)
    {
    case KW_BOOLEAN:
        type->literal_type = AST_TYPE_BOOLEAN;
        break;
    case KW_INT:
        type->literal_type = AST_TYPE_INT;
        break;
    case KW_DOUBLE:
        type->literal_type = AST_TYPE_DOUBLE;
        break;
    case KW_STRING:
        type->literal_type = AST_TYPE_STRING;
        break;
    case KW_UNIT:
        type->literal_type = AST_TYPE_UNIT;
        break;
    default:
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected: type got: ", next_token.line);
        log_token_type(next_token.type);
        log_printf("\n");

        ast_node_destruct_recursively(&type_notation);
        ast_node_destruct_recursively(&type);
        return NULL;
    }
    type_notation->left_child = type;

    return type_notation;
}

static ast_node *node_func_def(error *err)
{
    *err = NONE;
    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (!(next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    ast_node *func_def_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_def_node->type = AST_NODE_FUNC_DEF;
    func_def_node->src_line_number = next_token.line;

    func_def_node->left_child = node_func_start(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_node);
        return NULL;
    }
    func_def_node->right_child = node_func_body(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_node);
        return NULL;
    }

    return func_def_node;
}

static ast_node *node_main_func(error *err)
{
    *err = NONE;

    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_ANNOTATION)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_ANNOTATION, current_token.type, current_token.line);

        return NULL;
    }
    // TODO: resolve how @main is processed in lexer

    // current_token = get_token(err);
    // if (*err != NONE)
    // {
    //     ast_node_destruct_recursively(&main_func_def_node);
    //     return NULL;
    // }
    //
    // if (current_token.type != TOKEN_IDENTIFIER)
    // {
    //     *err = GENERAL_SYNTAX;
    //     log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
    //     ast_node_destruct_recursively(&main_func_def_node);
    //     return NULL;
    // }
    //
    // const int annotated_string_attribute_index = current_token.attribute_index;
    // const char *const annotated_string =
    //     attribute_table_get_identifier(attr_tab, err, annotated_string_attribute_index);
    // if (*err != NONE)
    // {
    //     ast_node_destruct_recursively(&main_func_def_node);
    //     return NULL;
    // }
    //
    // if (strcmp(annotated_string, "main") != 0)
    // {
    //     *err = GENERAL_SYNTAX;
    //     log_printf("[%lu] @ can not be used else that in @main annotation, got @%s\n", current_token.line,
    //                annotated_string);
    //
    //     ast_node_destruct_recursively(&main_func_def_node);
    //     return NULL;
    // }

    token next_token = peek_token(err); // drop const
    if (*err != NONE)
    {
        return NULL;
    }

    while (next_token.type == TOKEN_EOL) // TODO: check if more then 1 \n is allowed, if not remove this loop
    {
        get_token(err);
        if (*err != NONE)
        {
            return NULL;
        }

        next_token = peek_token(err);
        if (*err != NONE)
        {
            return NULL;
        }
    }

    if (!(next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    ast_node *main_func_def_node = node_func_def(err);
    if (*err != NONE)
    {
        return NULL;
    }
    main_func_def_node->type = AST_NODE_MAIN_FUNC_DEF;

    return main_func_def_node;
}

static ast_node *node_func_start(error *err)
{
    *err = NONE;
    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_name_node = node_func_name(err);
    if (*err != NONE)
    {
        return NULL;
    }
    ast_node *func_head_node = node_func_head(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name_node);
        return NULL;
    }

    ast_node *func_start_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name_node);
        ast_node_destruct_recursively(&func_head_node);

        return NULL;
    }
    func_start_node->type = AST_NODE_FUNC_START;
    func_start_node->src_line_number = next_token.line;

    func_start_node->left_child = func_name_node;
    func_start_node->right_child = func_head_node;

    return func_start_node;
}

static ast_node *node_func_head(error *err)
{
    *err = NONE;
    token next_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (next_token.type != TOKEN_OPEN_PAREN)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_OPEN_PAREN, next_token.type, next_token.line);

        return NULL;
    }
    current_paren_depth++;

    ast_node *param_list_node = node_param_list(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_head_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list_node);
        return NULL;
    }
    func_head_node->type = AST_NODE_FUNC_HEAD;
    func_head_node->src_line_number = next_token.line;

    func_head_node->left_child = param_list_node;

    next_token = peek_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }

    if (next_token.type != TOKEN_COLON)
    {
        return func_head_node;
    }

    get_token(err); // just consuming
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }
    next_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }
    if (!is_type(next_token))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_head_node);

        log_printf("[%lu] expected: TYPE got :", next_token.line);
        log_token_type(next_token.type);
        log_printf("\n");

        return NULL;
    }

    ast_node *func_return_type = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }
    func_return_type->type = AST_NODE_TYPE;
    func_return_type->src_line_number = next_token.line;
    switch (next_token.attribute_index)
    {
    case KW_BOOLEAN:
        func_return_type->literal_type = AST_TYPE_BOOLEAN;
        break;
    case KW_INT:
        func_return_type->literal_type = AST_TYPE_INT;
        break;
    case KW_DOUBLE:
        func_return_type->literal_type = AST_TYPE_DOUBLE;
        break;
    case KW_STRING:
        func_return_type->literal_type = AST_TYPE_STRING;
        break;
    case KW_UNIT:
        func_return_type->literal_type = AST_TYPE_UNIT;
        break;
    default:
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] ", next_token.line);
        log_token_type(next_token.type);
        log_printf(" can not be used as type\n");

        ast_node_destruct_recursively(&func_head_node);
        ast_node_destruct_recursively(&func_return_type);
        return NULL;
    }
    func_head_node->right_child = func_return_type;

    return func_head_node;
}

static ast_node *node_func_name(error *err)
{
    *err = NONE;

    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] expected: def got :", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");

        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *func_name = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    func_name->type = AST_NODE_FUNC_NAME;
    func_name->left_child = id_node_from_token(err, current_token);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name);
        return NULL;
    }
    func_name->src_line_number = current_token.line;

    return func_name;
}

static ast_node *node_param_list(error *err)
{
    *err = NONE;

    ast_node *param_list = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    token next_token = peek_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list);
        return NULL;
    }

    if (next_token.type == TOKEN_CLOSE_PAREN)
    {
        get_token(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&param_list);
            return NULL;
        }
        current_paren_depth--;
        param_list->type = AST_NODE_PARAM_LIST;
        param_list->src_line_number = next_token.line;
        return param_list;
    }

    param_list->left_child = node_param(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list);
        return NULL;
    }
    param_list->right_child = node_param_list_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list);
        return NULL;
    }
    param_list->type = AST_NODE_PARAM_LIST;
    param_list->src_line_number = param_list->left_child->src_line_number;

    return param_list;
}
static ast_node *node_param_list_(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type == TOKEN_CLOSE_PAREN) // not and error, just list end
    {
        current_paren_depth--;
        return NULL;
    }
    if (current_token.type != TOKEN_COMMA)
    {
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] expected: ')' or ',' got: ", current_token.line);
        log_token_type(current_token.type);
        return NULL;
    }

    if (next_token.type == TOKEN_EOL) // just skipping
    {
        get_token(err);
        if (*err != NONE)
        {
            return NULL;
        }
    }
    if (next_token.type == TOKEN_CLOSE_PAREN && next_token.line > current_token.line)
    {
        get_token(err);
        if (*err != NONE)
        {
            return NULL;
        }
        current_paren_depth--;
        return NULL;
    }

    ast_node *param_list_ = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    param_list_->left_child = node_param(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list_);
        return NULL;
    }
    param_list_->right_child = node_param_list_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list_);
        return NULL;
    }
    param_list_->type = AST_NODE_PARAM_LIST_;
    param_list_->src_line_number = param_list_->left_child->src_line_number;

    return param_list_;
}

static ast_node *node_param(error *err)
{
    *err = NONE;

    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);

        return NULL;
    }

    ast_node *id_node = id_node_from_token(err, current_token);
    if (*err != NONE)
    {
        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }
    if (current_token.type != TOKEN_COLON)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_COLON, current_token.type, current_token.line);

        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    if (!is_type(current_token))
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected a type, got: ", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    ast_node *param_type = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }
    param_type->type = AST_NODE_TYPE;
    param_type->src_line_number = current_token.line;
    switch (current_token.attribute_index)
    {
    case KW_BOOLEAN:
        param_type->literal_type = AST_TYPE_BOOLEAN;
        break;
    case KW_INT:
        param_type->literal_type = AST_TYPE_INT;
        break;
    case KW_DOUBLE:
        param_type->literal_type = AST_TYPE_DOUBLE;
        break;
    case KW_STRING:
        param_type->literal_type = AST_TYPE_STRING;
        break;
    case KW_UNIT:
        param_type->literal_type = AST_TYPE_UNIT;
        break;
    default:
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] ", current_token.line);
        log_token_type(current_token.type);
        log_printf(" can not be used as type\n");

        ast_node_destruct_recursively(&param_type);
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    ast_node *param = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_type);
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    param->type = AST_NODE_PARAM;
    param->src_line_number = id_node->src_line_number;
    param->left_child = id_node;
    param->right_child = param_type;

    return param;
}

static ast_node *node_func_body(error *err)
{
    *err = NONE;

    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (!(current_token.type == TOKEN_OPERATOR && current_token.attribute_index == OP_ASSIGN))
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected: '=' got: ", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");

        return NULL;
    }

    ast_node *func_body = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    func_body->type = AST_NODE_FUNC_BODY;
    func_body->src_line_number = current_token.line;

    if (next_token.type == TOKEN_EOL)
    {
        func_body->left_child = node_block(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&func_body);
            return NULL;
        }
    }
    else if (is_expression_member(next_token))
    {
        func_body->left_child = node_expression_or_assignment_to_id(err, false);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&func_body);
            return NULL;
        }

        current_token = get_token(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&func_body);
            return NULL;
        }
        if (current_token.type != TOKEN_EOL)
        {
            *err = GENERAL_SYNTAX;
            log_error_expected(TOKEN_EOL, current_token.type, current_token.line);

            ast_node_destruct_recursively(&func_body);
            return NULL;
        }
    }
    else
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected block start or an expression, got none of those", next_token.line);

        ast_node_destruct_recursively(&func_body);
        return NULL;
    }

    return func_body;
}

static ast_node *node_block(error *err)
{
    static long block_index = 0;
    block_index++;

    *err = NONE;

    token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_EOL)
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_EOL, current_token.type, current_token.line);
        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_INDENT)
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_INDENT, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *block = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    block->type = AST_NODE_BLOCK;
    block->src_line_number = current_token.line;
    block->block_index = block_index;

    block->left_child = node_stat_list(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&block);
        return NULL;
    }

    current_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&block);
        return NULL;
    }
    if (current_token.type != TOKEN_DEDENT)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_DEDENT, current_token.type, current_token.line);

        ast_node_destruct_recursively(&block);
        return NULL;
    }

    return block;
}
static ast_node *node_stat_list(error *err)
{
    *err = NONE;

    ast_node *statement_node = node_statement(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *stat_list = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&statement_node);
        return NULL;
    }
    stat_list->type = AST_NODE_STAT_LIST;
    stat_list->src_line_number = statement_node->src_line_number;
    stat_list->left_child = statement_node;

    const bool newline_was_consumed =
        statement_node->left_child->type == AST_NODE_IF_LIST || statement_node->left_child->type == AST_NODE_WHILE;
    stat_list->right_child = node_stat_list_(err, newline_was_consumed);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&stat_list);
        return NULL;
    }
    return stat_list;
}

static ast_node *node_stat_list_(error *err, const bool newline_consumed_by_caller)
{
    *err = NONE;

    token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (next_token.type == TOKEN_DEDENT)
    {
        return NULL;
    }
    if (next_token.type != TOKEN_EOL && next_token.type != TOKEN_SEMICOLON && !newline_consumed_by_caller)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected end of statement\n", next_token.line);
        return NULL;
    }

    while (next_token.type == TOKEN_EOL || next_token.type == TOKEN_SEMICOLON)
    {
        const token prev_token = next_token;

        get_token(err);
        if (*err != NONE)
        {
            return NULL;
        }

        next_token = peek_token(err);
        if (*err != NONE)
        {
            return NULL;
        }
        if (next_token.type == TOKEN_DEDENT)
        {
            return NULL;
        }
    }

    ast_node *statement_node = node_statement(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *stat_list_ = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&statement_node);
        return NULL;
    }
    stat_list_->type = AST_NODE_STAT_LIST_;
    stat_list_->src_line_number = statement_node->src_line_number;
    stat_list_->left_child = statement_node;

    const bool newline_was_consumed =
        statement_node->left_child->type == AST_NODE_IF_LIST || statement_node->left_child->type == AST_NODE_WHILE;
    stat_list_->right_child = node_stat_list_(err, newline_was_consumed);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&stat_list_);
        return NULL;
    }
    return stat_list_;
}

static ast_node *node_statement(error *err)
{
    *err = NONE;

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    if (next_token.type == TOKEN_KEYWORD)
    {
        ast_node *node_to_append = NULL;
        switch (next_token.attribute_index)
        {
        case KW_VAL:
        case KW_VAR:
            node_to_append = node_definition(err);
            break;

        case KW_WHILE:
            node_to_append = node_while_loop(err);
            break;

        case KW_IF:
            node_to_append = node_if_list(err);
            break;

        case KW_RETURN:
            node_to_append = node_return_statement(err);
            break;

        default:
            node_to_append = NULL;
        }
        if (*err != NONE)
        {
            return NULL;
        }

        if (node_to_append != NULL)
        {
            ast_node *statement_node = new_ast_node(err);
            if (*err != NONE)
            {
                ast_node_destruct_recursively(&node_to_append);
                return NULL;
            }
            statement_node->type = AST_NODE_STATEMENT;
            statement_node->src_line_number = node_to_append->src_line_number;
            statement_node->left_child = node_to_append;

            return statement_node;
        }
    }
    if (!(next_token.type == TOKEN_IDENTIFIER || is_expression_member(next_token)))
    {
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] expected an expression, got: ", next_token.line);
        log_token_type(next_token.type);
        log_printf("\n");
        return NULL;
    }

    ast_node *expression_or_assignment = node_expression_or_assignment_to_id(err, true);
    if (*err != NONE)
    {
        return NULL;
    }
    if (expression_or_assignment->type == AST_NODE_STATEMENT)
    {
        return expression_or_assignment;
    }

    ast_node *statement_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&expression_or_assignment);
        return NULL;
    }
    statement_node->type = AST_NODE_STATEMENT;
    statement_node->src_line_number = expression_or_assignment->src_line_number;
    statement_node->left_child = expression_or_assignment;

    return statement_node;
}

static ast_node *node_definition(error *err)
{
    *err = NONE;

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    ast_node *definition = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    definition->type = AST_NODE_DEFINITION;
    definition->src_line_number = next_token.line;

    if (next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_VAL)
    {
        definition->left_child = node_val_def(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&definition);
            return NULL;
        }
    }
    else if (next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_VAR)
    {
        definition->left_child = node_var_def(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&definition);
            return NULL;
        }
    }
    else
    {
        *err = GENERAL_SYNTAX;

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        ast_node_destruct_recursively(&definition);

        return NULL;
    }

    const token assign_id = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&definition);
        return NULL;
    }
    if (assign_id.type != TOKEN_OPERATOR || assign_id.attribute_index != OP_ASSIGN)
    {
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] expected assignment operator\n", assign_id.line);
        ast_node_destruct_recursively(&definition);

        return NULL;
    }

    definition->right_child = node_assign_val(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&definition);
        return NULL;
    }
    return definition;
}

static ast_node *node_expression_or_assignment_to_id(error *err, const bool can_be_assignment)
{
    *err = NONE;
    const size_t expression_paren_depth = current_paren_depth;
    stack *expression_parse_stack = new_stack(err);
    if (*err != NONE)
    {
        return NULL;
    }

    // token current_token = EMPTY_TOKEN;
    token next_token = peek_token(err);
    if (*err != NONE)
    {
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }

    ast_node *tmp_node = NULL;
    bool is_first_cycle = can_be_assignment;
    bool prev_symbol_was_operand = false;

    parser_stack_item *item; // item to append to stack;

    while (is_expression_member(next_token))
    {
        bool token_consumed_by_called_func = false;

        if (next_token.type == TOKEN_CLOSE_PAREN && current_paren_depth <= expression_paren_depth)
        {
            break;
        }

        switch (next_token.type)
        {
        case TOKEN_IDENTIFIER:
            tmp_node = node_func_call(err);
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }

            if (tmp_node->type == AST_NODE_IDENTIFIER)
            {
                // there is no way to distinguish
                // id = ...       which is assignment and
                // id + - * / ... which is expression, so I decided to use this trick
                next_token = peek_token(err);
                if (*err != NONE)
                {
                    parser_stack_destruct(&expression_parse_stack, err);
                    ast_node_destruct_recursively(&tmp_node);
                    return NULL;
                }
                if (next_token.type == TOKEN_OPERATOR && next_token.attribute_index == OP_ASSIGN)
                {
                    parser_stack_destruct(&expression_parse_stack, err);

                    if (!is_first_cycle)
                    {
                        *err = GENERAL_SYNTAX;
                        ast_node_destruct_recursively(&tmp_node);

                        log_printf("[%lu] assignment sign \"=\" can not be used inside of an expression\n",
                                   next_token.line);
                        return NULL;
                    }

                    get_token(err); // skipping "="
                    if (*err != NONE)
                    {
                        ast_node_destruct_recursively(&tmp_node);
                        return NULL;
                    }

                    ast_node *statement_node = new_ast_node(err);
                    if (*err != NONE)
                    {
                        ast_node_destruct_recursively(&tmp_node);
                        return NULL;
                    }
                    statement_node->type = AST_NODE_STATEMENT;
                    statement_node->src_line_number = next_token.line;
                    // using previous's token line parameter, but this does not matter that much

                    statement_node->left_child = tmp_node;
                    statement_node->right_child = node_assign_val(err);
                    if (*err != NONE)
                    {
                        ast_node_destruct_recursively(&statement_node);
                        return NULL;
                    }

                    return statement_node;
                }
            }
            item = new_parser_stack_item(err, PRECEDENTIAL_OPERAND);
            if (*err != NONE)
            {
                ast_node_destruct_recursively(&tmp_node);
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
            item->item_type = ITEM_AST_NODE;
            item->item_ast_node = tmp_node;

            token_consumed_by_called_func = true;
            prev_symbol_was_operand = true;
            break;

        case TOKEN_INT:
        case TOKEN_DOUBLE:
        case TOKEN_STRING:
        case TOKEN_UNIT:
        case TOKEN_BOOLEAN:
            item = literal_stack_item_from_token(err, next_token);
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }

            prev_symbol_was_operand = true;
            break;

        case TOKEN_OPERATOR: {
            precedence_table_member operator_precedential_type = PRECEDENTIAL_NON_EVALUATABLE;
            ast_operator operator_ast_type = AST_OP_NONE;

            switch (next_token.attribute_index)
            {
            case OP_PLUS:
                operator_precedential_type = PRECEDENTIAL_OP_PLUS;
                operator_ast_type = AST_OP_PLUS;

                break;

            case OP_MINUS:

                if (!prev_symbol_was_operand)
                {
                    operator_precedential_type = PRECEDENTIAL_OP_UNARY_MINUS;
                    operator_ast_type = AST_OP_MINUS_UNARY;
                }
                else
                {
                    operator_precedential_type = PRECEDENTIAL_OP_MINUS;
                    operator_ast_type = AST_OP_MINUS;
                }
                break;

            case OP_MULTIPLY:
                operator_precedential_type = PRECEDENTIAL_OP_MULTIPLY;
                operator_ast_type = AST_OP_MULTIPLY;

                break;

            case OP_DIVISION:
                operator_precedential_type = PRECEDENTIAL_OP_DIVIDE;
                operator_ast_type = AST_OP_DIV;

                break;

            case OP_GREATER:
                operator_precedential_type = PRECEDENTIAL_OP_GREATER;
                operator_ast_type = AST_OP_GREATER;

                break;

            case OP_EQUAL_GREATER:
                operator_precedential_type = PRECEDENTIAL_OP_GREATER_EQUAL;
                operator_ast_type = AST_OP_GREATER_EQUAL;

                break;

            case OP_LESS:
                operator_precedential_type = PRECEDENTIAL_OP_LESS;
                operator_ast_type = AST_OP_LESS;

                break;

            case OP_EQUAL_LESS:
                operator_precedential_type = PRECEDENTIAL_OP_LESS_EQUAL;
                operator_ast_type = AST_OP_LESS_EQUAL;

                break;

            case OP_EQUAL:
                operator_precedential_type = PRECEDENTIAL_OP_EQUALS;
                operator_ast_type = AST_OP_EQUAL;

                break;

            case OP_NOT_EQUAL:
                operator_precedential_type = PRECEDENTIAL_OP_NOT_EQUALS;
                operator_ast_type = AST_OP_NOT_EQUAL;

                break;

            case OP_NOT:
                operator_precedential_type = PRECEDENTIAL_OP_NOT;
                operator_ast_type = AST_OP_NOT;

                break;

            case OP_AND:
                operator_precedential_type = PRECEDENTIAL_OP_AND;
                operator_ast_type = AST_OP_AND;

                break;

            case OP_OR:
                operator_precedential_type = PRECEDENTIAL_OP_OR;
                operator_ast_type = AST_OP_OR;

                break;

            case OP_ASSIGN:
                *err = GENERAL_SYNTAX;

                log_printf("[%lu] assignment sign \"=\" can not be used inside of an expression\n", next_token.line);
                parser_stack_destruct(&expression_parse_stack, err);

                return NULL;

            default:
                *err = GENERAL_SYNTAX;

                log_printf("[%lu] unsupported operator inside of an expression\n", next_token.line);
                parser_stack_destruct(&expression_parse_stack, err);

                return NULL;
            }

            ast_node *operator_node = new_ast_node(err);
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }

            operator_node->type = AST_NODE_BINARY_OPERATION;

            if (operator_ast_type == AST_OP_NOT || operator_ast_type == AST_OP_MINUS_UNARY)
            {
                operator_node->type = AST_NODE_UNARY_OPERATION;
            }

            operator_node->src_line_number = next_token.line;
            operator_node->operator = operator_ast_type;

            item = new_parser_stack_item(err, operator_precedential_type);
            if (*err != NONE)
            {
                ast_node_destruct_recursively(&operator_node);
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
            item->item_type = ITEM_AST_NODE;
            item->item_ast_node = operator_node;

            prev_symbol_was_operand = false;
            break;
        }

        case TOKEN_OPEN_PAREN:
            current_paren_depth++;

            item = new_parser_stack_item(err, PRECEDENTIAL_LPAREN);
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
            item->item_type = ITEM_PAREN;

            prev_symbol_was_operand = false;
            break;

        case TOKEN_CLOSE_PAREN:
            if (current_paren_depth <= expression_paren_depth)
            {
                *err = GENERAL_SYNTAX;
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
            current_paren_depth--;

            item = new_parser_stack_item(err, PRECEDENTIAL_RPAREN);
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
            item->item_type = ITEM_PAREN;

            prev_symbol_was_operand = true;
            break;

        default:
            *err = GENERAL_SYNTAX;
            log_printf("[%lu] ", next_token.line);
            log_token_type(next_token.type);
            log_printf(" cannot be an expression member\n");

            parser_stack_destruct(&expression_parse_stack, err);
            return NULL;
        }

        parse_stack_append(expression_parse_stack, err, &item, next_token.line);
        if (*err != NONE)
        {
            parser_stack_item_destruct(&item);
            parser_stack_destruct(&expression_parse_stack, err);
            return NULL;
        }

        if (is_first_cycle)
        {
            is_first_cycle = false;
        }

        if (!token_consumed_by_called_func)
        {
            get_token(err); // just dropping items
            if (*err != NONE)
            {
                parser_stack_destruct(&expression_parse_stack, err);
                return NULL;
            }
        }

        next_token = peek_token(err);
        if (*err != NONE)
        {
            parser_stack_destruct(&expression_parse_stack, err);
            return NULL;
        }
    } // processing loop end

    if (!prev_symbol_was_operand)
    {
        *err = GENERAL_SYNTAX;
        parser_stack_destruct(&expression_parse_stack, err);
        log_printf("[%lu] last expression item was not operand\n", next_token.line);
        return NULL;
    }

    if (current_paren_depth != expression_paren_depth)
    {
        *err = GENERAL_SYNTAX;
        parser_stack_destruct(&expression_parse_stack, err);
        log_printf("[%lu] unmatched opening parenthesis in expression\n", next_token.line);
        return NULL;
    }

    parser_stack_item *terminator_item = new_parser_stack_item(err, PRECEDENTIAL_EXPR_TERMINATOR);
    if (*err != NONE)
    {
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }
    terminator_item->item_type = ITEM_TERMINATOR;

    parse_stack_append(expression_parse_stack, err, &terminator_item, next_token.line);
    if (*err != NONE)
    {
        parser_stack_item_destruct(&terminator_item);
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }

    // on this point the only operator node should be second from an end :
    // $ <- stack top
    // E
    // $ <- this is basically virtual, since it is not by fact on stack

    parser_stack_item *terminator = stack_pop(expression_parse_stack, err);
    if (*err != NONE)
    {
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }
    if (terminator == NULL || terminator->item_type != ITEM_TERMINATOR ||
        terminator->precedential_type != PRECEDENTIAL_EXPR_TERMINATOR)
    {
        parser_stack_item_destruct(&terminator);
        *err = GENERAL_SYNTAX;
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }
    parser_stack_item_destruct(&terminator);

    parser_stack_item *expression_item = stack_pop(expression_parse_stack, err);
    if (*err != NONE)
    {
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }

    if (expression_item == NULL || expression_item->item_type != ITEM_AST_NODE ||
        expression_item->item_ast_node == NULL || expression_item->precedential_type != PRECEDENTIAL_NON_EVALUATABLE ||
        stack_get_item_count(expression_parse_stack) != 0)
    {
        parser_stack_item_destruct(&expression_item);
        *err = GENERAL_SYNTAX;
        parser_stack_destruct(&expression_parse_stack, err);
        return NULL;
    }

    ast_node *expression = expression_item->item_ast_node;
    expression_item->item_ast_node = NULL;
    parser_stack_item_destruct(&expression_item);
    stack_destruct(&expression_parse_stack);

    return expression;
}

static ast_node *node_while_loop(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_WHILE)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *while_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    while_node->type = AST_NODE_WHILE;
    while_node->src_line_number = current_token.line;

    while_node->left_child = node_expression_or_assignment_to_id(err, false);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }

    const token do_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }
    if (do_token.type != TOKEN_KEYWORD || do_token.attribute_index != KW_DO)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected 'do'\n", do_token.line);
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }

    while_node->right_child = node_block(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }
    return while_node;
}

static ast_node *node_return_statement(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_RETURN)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);

        return NULL;
    }

    ast_node *return_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    return_node->type = AST_NODE_RETURN;
    return_node->src_line_number = current_token.line;

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&return_node);
        return NULL;
    }
    if (next_token.type == TOKEN_EOL || next_token.type == TOKEN_SEMICOLON || next_token.type == TOKEN_DEDENT ||
        next_token.type == TOKEN_EOF)
    {
        return return_node;
    }

    return_node->left_child = node_assign_val(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&return_node);
        return NULL;
    }

    return return_node;
}

static ast_node *node_assign_val(error *err)
{
    *err = NONE;

    ast_node *expression_node = node_expression_or_assignment_to_id(err, false);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *assign_val = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&expression_node);
        return NULL;
    }
    assign_val->type = AST_NODE_ASSIGN_VAL;
    assign_val->src_line_number = expression_node->src_line_number;
    assign_val->left_child = expression_node;

    return assign_val;
}

static ast_node *node_if_list(error *err)
{
    *err = NONE;

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (next_token.type != TOKEN_KEYWORD || next_token.attribute_index != KW_IF)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);

        return NULL;
    }

    ast_node *if_node = node_if_block(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *if_list = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }
    if_list->type = AST_NODE_IF_LIST;
    if_list->src_line_number = if_node->src_line_number;
    if_list->left_child = if_node;

    if_list->right_child = node_if_list_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_list);
        return NULL;
    }

    return if_list;
}

static ast_node *node_if_list_(error *err)
{
    *err = NONE;

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (next_token.type != TOKEN_KEYWORD || next_token.attribute_index != KW_ELSE)
    {
        return NULL;
    }

    ast_node *else_node = node_else_block(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *if_list_ = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&else_node);
        return NULL;
    }
    if_list_->type = AST_NODE_IF_LIST_;
    if_list_->src_line_number = else_node->src_line_number;
    if_list_->left_child = else_node;

    if_list_->right_child = node_if_list_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_list_);
        return NULL;
    }

    return if_list_;
}

static ast_node *node_if_block(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_IF)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);

        return NULL;
    }

    ast_node *if_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if_node->type = AST_NODE_IF;
    if_node->src_line_number = current_token.line;
    if_node->left_child = node_expression_or_assignment_to_id(err, false);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }

    const token then_token = get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }
    if (then_token.type != TOKEN_KEYWORD || then_token.attribute_index != KW_THEN)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected 'then'\n", then_token.line);
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }

    if_node->right_child = node_block(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }

    return if_node;
}

static ast_node *node_else_block(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_ELSE)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);

        return NULL;
    }

    ast_node *else_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    else_node->type = AST_NODE_ELSE;
    else_node->src_line_number = current_token.line;

    const token after_else = peek_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&else_node);
        return NULL;
    }

    if (after_else.type == TOKEN_KEYWORD && after_else.attribute_index == KW_IF)
    {
        else_node->left_child = node_if_block(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&else_node);
            return NULL;
        }

        return else_node;
    }

    else_node->left_child = node_block(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&else_node);
        return NULL;
    }
    return else_node;
}

static ast_node *node_func_call(error *err)
{
    *err = NONE;
    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *id_node = id_node_from_token(err, current_token);
    if (*err != NONE)
    {
        return NULL;
    }

    if (next_token.type != TOKEN_OPEN_PAREN)
    {
        // we are pretending this just being id
        return id_node;
    }
    current_paren_depth++;
    get_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    ast_node *func_call_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    func_call_node->src_line_number = current_token.line;
    func_call_node->type = AST_NODE_FUNC_CALL;
    func_call_node->left_child = id_node;
    func_call_node->right_child = node_call_params(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_call_node);
        return NULL;
    }

    return func_call_node;
}

static ast_node *node_call_params(error *err)
{
    *err = NONE;

    ast_node *call_params = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    call_params->type = AST_NODE_CALL_PARAMS;

    token next_token = peek_token(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }

    while (next_token.type == TOKEN_EOL)
    {
        get_token(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }

        next_token = peek_token(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }
    }

    if (next_token.type == TOKEN_CLOSE_PAREN)
    {
        get_token(err);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }
        current_paren_depth--;

        call_params->src_line_number = next_token.line;

        return call_params;
    }

    call_params->left_child = node_call_param(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }
    call_params->src_line_number = call_params->left_child->src_line_number;

    call_params->right_child = node_call_params_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }
    return call_params;
}

static ast_node *node_call_params_(error *err)
{
    *err = NONE;

    const token current_token = get_token(err);
    if (*err != NONE)
    {
        return NULL;
    }
    const size_t start_line = current_token.line;

    if (current_token.type == TOKEN_CLOSE_PAREN)
    {
        if (current_paren_depth > 0)
        {
            current_paren_depth--;
        }
        return NULL;
    }
    if (current_token.type != TOKEN_COMMA)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected ',' or ')', got: ", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");

        return NULL;
    }

    const token next_token = peek_token(err);
    if (*err != NONE)
    {
        return NULL;
    }

    // bool had_end_of_line = false;
    //
    // while (next_token.type == TOKEN_EOL)
    // {
    //     had_end_of_line = true;
    //     get_token(err);
    //     if (*err != NONE)
    //     {
    //         return NULL;
    //     }
    //     next_token = peek_token(err);
    //     if (*err != NONE)
    //     {
    //         return NULL;
    //     }
    // }

    if (next_token.type == TOKEN_CLOSE_PAREN)
    {
        if (next_token.line <= start_line)
        {
            *err = GENERAL_SYNTAX;

            log_printf("[%lu] expected call argument after ','\n", next_token.line);
            return NULL;
        }

        get_token(err);
        if (*err != NONE)
        {
            return NULL;
        }
        current_paren_depth--;

        return NULL;
    }

    ast_node *call_params_ = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    call_params_->type = AST_NODE_CALL_PARAMS_;

    call_params_->left_child = node_call_param(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params_);
        return NULL;
    }
    call_params_->right_child = node_call_params_(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params_);
        return NULL;
    }
    call_params_->src_line_number = call_params_->left_child->src_line_number;

    return call_params_;
}

static ast_node *node_call_param(error *err)
{
    *err = NONE;

    ast_node *expression_node = node_expression_or_assignment_to_id(err, false);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *call_param = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&expression_node);
        return NULL;
    }
    call_param->type = AST_NODE_CALL_PARAM;
    call_param->src_line_number = expression_node->src_line_number;
    call_param->left_child = expression_node;

    return call_param;
}