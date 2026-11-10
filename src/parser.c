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

#define CREATE_ID_NODE(id_node, do_before_error_return)                                                                \
    ast_node *id_node = new_ast_node(err);                                                                             \
    if (*err != NONE)                                                                                                  \
    {                                                                                                                  \
        do_before_error_return;                                                                                        \
        return NULL;                                                                                                   \
    }                                                                                                                  \
    id_node->type = AST_NODE_IDENTIFIER;                                                                               \
    id_node->src_line_number = current_token.line;                                                                     \
    id_node->attribute_index = current_token.attribute_index;                                                          \
    id_node->literal_type = (ast_literal_type)current_token.type;

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

    PRECEDENTIAL_EXPR_TERMINATOR
} precedence_table_member;

typedef enum
{
    LS, // less
    EQ, // equal
    GR, // greater

    IV, // invalid
} operator_priority;

static const operator_priority precedence_table[PRECEDENTIAL_EXPR_TERMINATOR + 1][PRECEDENTIAL_EXPR_TERMINATOR + 1] = {
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

static void log_error_expected(const token_type expected, token_type got, const size_t line)
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
        break;

    default:
        return false;
    }

    return false;
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

static ast_node *node_expression_or_assignment_to_id(error *err, FILE *src_stream, attribute_table *attr_tab,
                                                     bool can_be_assignment);
static ast_node *node_while_loop(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_return_statement(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_assign_val(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_if_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_if_block(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_else_block(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_func_call(error *err, FILE *src_stream, attribute_table *attr_tab);
// also used for identifiers inside expressions

static ast_node *node_call_params(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_params_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_param(error *err, FILE *src_stream, attribute_table *attr_tab);

// globals ------------------------------------------------------------------------------------------------------------
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

// implementations --------------------------------------------------------------------------------------------------
static ast_node *node_program(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    // TODO: create prologue check
    if (prologue_missing)
    {
        *err = PROLOGUE_MISSING;

        log_printf("ERROR: prologue is missing\n");
        return NULL;
    }

    return node_def_list(err, src_stream, attr_tab);
}

static ast_node *node_def_list(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_def_list_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_def_list_node->type = AST_NODE_DEF_LIST;
    func_def_list_node->src_line_number = next_token.line;

    ast_node *node_to_append = NULL;
    if (next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF)
    {
        node_to_append = node_func_def(err, src_stream, attr_tab);
    }
    else if (next_token.type == TOKEN_ANNOTATION)
    {
        node_to_append = node_main_func(err, src_stream, attr_tab);
    }
    else if (next_token.type == TOKEN_EOF)
    {
        ast_node_destruct_recursively(&func_def_list_node);
        return NULL;
    }
    else
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_def_list_node);

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_list_node);
        return NULL;
    }

    func_def_list_node->left_child = node_to_append;

    node_to_append = node_def_list(err, src_stream, attr_tab);
    func_def_list_node->right_child = node_to_append;
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_list_node);
        return NULL;
    }

    return func_def_list_node;
}

static ast_node *node_val_def(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *val_def = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    val_def->type = AST_NODE_VAL_DEF;
    val_def->src_line_number = current_token.line;

    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_VAL))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&val_def);

        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);
        return NULL;
    }

    val_def->left_child = node_def_body(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&val_def);
        return NULL;
    }

    return val_def;
}

static ast_node *node_var_def(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *var_def = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    var_def->type = AST_NODE_VAR_DEF;
    var_def->src_line_number = current_token.line;

    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_VAR))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&var_def);

        log_error_expected(TOKEN_KEYWORD, current_token.type, current_token.line);
        return NULL;
    }

    var_def->left_child = node_def_body(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&var_def);
        return NULL;
    }

    return var_def;
}

static ast_node *node_def_body(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *def_body = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    def_body->type = AST_NODE_DEF_BODY;
    def_body->src_line_number = current_token.line;

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&def_body);

        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
        return NULL;
    }

    ast_node *id = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&def_body);
        return NULL;
    }
    id->type = AST_NODE_IDENTIFIER;
    id->src_line_number = current_token.line;
    id->attribute_index = current_token.attribute_index;

    def_body->left_child = id;

    if (next_token.type != TOKEN_COLON)
    {
        return def_body;
    }

    def_body->right_child = node_type_notation(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&def_body);
        return NULL;
    }

    return def_body;
}

static ast_node *node_type_notation(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    const token next_token = get_token(err, src_stream, attr_tab);
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
        log_printf("[%lu] expected: type got: ");
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
        log_printf("[%lu] expected: type got: ");
        log_token_type(next_token.type);
        log_printf("\n");

        ast_node_destruct_recursively(&type_notation);
        ast_node_destruct_recursively(&type);
        return NULL;
    }
    type_notation->left_child = type;

    return type_notation;
}

static ast_node *node_func_def(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_def_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_def_node->type = AST_NODE_FUNC_DEF;
    func_def_node->src_line_number = next_token.line;

    if (!(next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_def_node);

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    func_def_node->left_child = node_func_start(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_node);
        return NULL;
    }
    func_def_node->right_child = node_func_body(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_def_node);
        return NULL;
    }

    return func_def_node;
}

static ast_node *node_main_func(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *main_func_def_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    main_func_def_node->type = AST_NODE_MAIN_FUNC_DEF;

    token next_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }
    main_func_def_node->src_line_number = next_token.line;
    if (next_token.type != TOKEN_ANNOTATION)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }
    // TODO: implement main string consumption

    next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }
    if (!(next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&main_func_def_node);

        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);
        return NULL;
    }

    main_func_def_node->left_child = node_func_def(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }

    return main_func_def_node;
}

static ast_node *node_func_start(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_start_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_start_node->type = AST_NODE_FUNC_START;
    func_start_node->src_line_number = next_token.line;

    ast_node *func_name_node = node_func_name(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_start_node);
        return NULL;
    }
    ast_node *func_head_node = node_func_head(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_start_node);
        ast_node_destruct_recursively(&func_name_node);
        return NULL;
    }

    func_start_node->left_child = func_name_node;
    func_start_node->right_child = func_head_node;

    return func_start_node;
}

static ast_node *node_func_head(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    token next_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *func_head_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_head_node->type = AST_NODE_FUNC_HEAD;
    func_head_node->src_line_number = next_token.line;

    if (next_token.type != TOKEN_OPEN_PAREN)
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_head_node);
        log_error_expected(TOKEN_OPEN_PAREN, next_token.type, next_token.line);

        return NULL;
    }
    current_paren_depth++;

    ast_node *param_list_node = node_param_list(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }

    func_head_node->left_child = param_list_node;

    next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }

    if (next_token.type != TOKEN_COLON)
    {
        return func_head_node;
    }

    next_token = get_token(err, src_stream, attr_tab); // just consuming
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_head_node);
        return NULL;
    }
    next_token = get_token(err, src_stream, attr_tab);
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

        log_printf("[%lu] ");
        log_token_type(next_token.type);
        log_printf(" can not be used as type\n");

        ast_node_destruct_recursively(&func_head_node);
        ast_node_destruct_recursively(&func_return_type);
        return NULL;
    }
    func_head_node->right_child = func_return_type;

    return func_head_node;
}

static ast_node *node_func_name(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    ast_node *func_name = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name);

        return NULL;
    }
    if (!(current_token.type == TOKEN_KEYWORD && current_token.attribute_index == KW_DEF))
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_name);

        log_printf("[%lu] expected: def got :", current_token.line);
        log_token_type(current_token.type);
        log_printf("\n");

        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name);
        return NULL;
    }

    if (current_token.type != TOKEN_IDENTIFIER)
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&func_name);

        log_error_expected(TOKEN_IDENTIFIER, current_token.type, current_token.line);
        return NULL;
    }

    CREATE_ID_NODE(id_node, ast_node_destruct_recursively(&func_name));

    func_name->left_child = id_node;

    return func_name;
}

static ast_node *node_param_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_param_list_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_param(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_func_body(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_block(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    static long block_index = 0;
    block_index++;

    *err = NONE;

    token current_token = get_token(err, src_stream, attr_tab);
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

    current_token = get_token(err, src_stream, attr_tab);
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

    block->left_child = node_stat_list(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&block);
        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
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
static ast_node *node_stat_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_stat_list_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_statement(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_definition(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_expression_or_assignment_to_id(error *err, FILE *src_stream, attribute_table *attr_tab,
                                                     bool can_be_assignment)
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
    if (*err != NONE)
    {
        stack_destruct(&expression_parse_stack);
        return NULL;
    }

    ast_node *tmp_node = NULL;
    bool is_first_cycle = true && can_be_assignment;
    while (is_expression_member(next_token))
    {
        expression_parser_stack_item *new_item = new_expression_parser_stack_item(err);
        if (*err != NONE)
        {
            stack_destruct(&expression_parse_stack);
            return NULL;
        }

        // TODO: implement actual algorithm
        switch (next_token.type)
        {
        case TOKEN_IDENTIFIER:
            tmp_node = node_func_call(err, src_stream, attr_tab);
            if (*err != NONE)
            {
                UTILS_SAFE_FREE(&new_item);
                stack_destruct(&expression_parse_stack);
                return NULL;
            }
            new_item->content_type = ITEM_AST_NODE;
            new_item->item_ast_node = tmp_node;

            if (tmp_node->type == AST_NODE_IDENTIFIER)
            {
                // there is no way to distinguish
                // id = ...       which is assignment and
                // id + - * / ... which is expression, so I decided to use this trick
                next_token = peek_token(err, src_stream, attr_tab);
                if (next_token.type == TOKEN_OPERATOR && next_token.attribute_index == OP_ASSIGN)
                {
                    stack_destruct(&expression_parse_stack);
                    UTILS_SAFE_FREE(&new_item);

                    if (!is_first_cycle)
                    {
                        *err = GENERAL_SYNTAX;
                        ast_node_destruct_recursively(&tmp_node);

                        log_printf("[%lu] assignment sign \"=\" can not be used inside of an expression\n");
                        return NULL;
                    }

                    get_token(err, src_stream, attr_tab); // skipping "="
                    if (*err != NONE)
                    {
                        ast_node_destruct_recursively(&tmp_node);
                        return NULL;
                    }
                    next_token = peek_token(err, src_stream, attr_tab);
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

                    statement_node->left_child = tmp_node;
                    statement_node->right_child = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
                    if (*err != NONE)
                    {
                        ast_node_destruct_recursively(&statement_node);
                        return NULL;
                    }

                    return statement_node;
                }
                // TODO: logic for ID
            }
            // TODO: logic for func_call

            break;
        case TOKEN_INT:

            break;
        case TOKEN_DOUBLE:

            break;
        case TOKEN_STRING:

            break;
        case TOKEN_UNIT:

            break;
        case TOKEN_BOOLEAN:

            break;
        case TOKEN_OPERATOR:

            break;
        case TOKEN_OPEN_PAREN:
            current_paren_depth++;

            break;
        case TOKEN_CLOSE_PAREN:
            current_paren_depth--;

            break;
        default:
            log_printf("[%lu] ", current_token.line);
            log_token_type(current_token.type);
            log_printf(" cannon be an expression member\n");

            stack_destruct(&expression_parse_stack);
            return NULL;
        }

        if (is_first_cycle)
        {
            is_first_cycle = false;
        }
        next_token = peek_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
        }
    }

    return NULL; // tmp
}

static ast_node *node_while_loop(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_return_statement(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_assign_val(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_if_list(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_if_block(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_else_block(error *err, FILE *src_stream, attribute_table *attr_tab);

static ast_node *node_func_call(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    CREATE_ID_NODE(id_node, )

    if (next_token.type != TOKEN_OPEN_PAREN)
    {
        // we are pretending this just being id
        return id_node;
    }
    current_paren_depth++;
    get_token(err, src_stream, attr_tab);

    ast_node *func_call_node = new_ast_node(err);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&id_node);
        ast_node_destruct_recursively(&func_call_node);
        return NULL;
    }

    func_call_node->type = AST_NODE_FUNC_CALL;
    func_call_node->left_child = id_node;
    func_call_node->right_child = node_call_params(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_call_node);
        return NULL;
    }

    return func_call_node;
}

static ast_node *node_call_params(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_params_(error *err, FILE *src_stream, attribute_table *attr_tab);
static ast_node *node_call_param(error *err, FILE *src_stream, attribute_table *attr_tab);