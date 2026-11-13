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
static size_t current_paren_depth =
    0; // needed to track if we are inside of parentheses to ignore indents and new lines
static token_input_buffer token_buffer = {.was_stored = false, .content = EMPTY_TOKEN};

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

    precedence_table_member precedential_type;

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
    id_node->literal_type = (ast_literal_type)tok.type;

    return id_node;
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
static ast_node *node_if_list_(error *err, FILE *src_stream, attribute_table *attr_tab);
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

    // TODO : init_lexer

    ast_node *program = node_program(err, src_stream, attr_tab);
    // error check makes no sense here, if it happened it will simply propagate and return will be NULL anyway

    // TODO : destroy_lexer
    return program;
}

// implementations --------------------------------------------------------------------------------------------------
static ast_node *node_program(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;
    token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    if (current_token.type != TOKEN_KEYWORD || current_token.attribute_index != KW_IMPORT)
    {
        *err = PROLOGUE_MISSING;
        log_printf("FATAL: [%lu] program should start with prologue\r\n");

        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    const int prologue_string_attribute_index = current_token.attribute_index;
    const char *const prologue_string = attribute_table_get_string(attr_tab, err, prologue_string_attribute_index);
    if (*err != NONE)
    {
        return NULL;
    }

    if (strcmp(prologue_string, "ifj26") != 0)
    {
        *err = PROLOGUE_MISSING;
        log_printf("[%lu] %s can not be used as prologue\n", prologue_string);

        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
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

    current_token = get_token(err, src_stream, attr_tab);
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

    ast_node *node_to_append = NULL; // added NULL initialisation here in order to be consistent
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
        return NULL;
    }
    func_def_list_node->type = AST_NODE_DEF_LIST;
    func_def_list_node->src_line_number = next_token.line;

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

    token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    ast_node *main_func_def_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    main_func_def_node->type = AST_NODE_MAIN_FUNC_DEF;

    main_func_def_node->src_line_number = current_token.line;
    if (current_token.type != TOKEN_ANNOTATION)
    {
        *err = GENERAL_SYNTAX;
        ast_node_destruct_recursively(&main_func_def_node);

        log_error_expected(TOKEN_ANNOTATION, current_token.type, current_token.line);
        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }

    const int annotated_string_attribute_index = current_token.attribute_index;
    const char *const annotated_string = attribute_table_get_string(attr_tab, err, annotated_string_attribute_index);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }

    if (strcmp(annotated_string, "main") != 0)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] @ can not be used else that in @main annotation, got @%s\n", annotated_string);

        ast_node_destruct_recursively(&main_func_def_node);
        return NULL;
    }

    const token next_token = peek_token(err, src_stream, attr_tab);
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

    ast_node *func_name_node = node_func_name(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    ast_node *func_head_node = node_func_head(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name_node);
        return NULL;
    }

    ast_node *func_start_node = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    func_start_node->type = AST_NODE_FUNC_START;
    func_start_node->src_line_number = next_token.line;

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

    if (next_token.type != TOKEN_OPEN_PAREN)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_OPEN_PAREN, next_token.type, next_token.line);

        return NULL;
    }
    current_paren_depth++;

    ast_node *param_list_node = node_param_list(err, src_stream, attr_tab);
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

    token current_token = get_token(err, src_stream, attr_tab);
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

    current_token = get_token(err, src_stream, attr_tab);
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

    func_name->left_child = id_node_from_token(err, current_token);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&func_name);
        return NULL;
    }

    return func_name;
}

static ast_node *node_param_list(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *param_list = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    param_list->left_child = node_param(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list);
        return NULL;
    }
    param_list->right_child = node_param_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list);
        return NULL;
    }
    param_list->type = AST_NODE_PARAM_LIST;
    param_list->src_line_number = param_list->left_child->src_line_number;

    return param_list;
}
static ast_node *node_param_list_(error *err, FILE *src_stream, attribute_table *attr_tab)
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

    if (current_token.type == TOKEN_CLOSE_PAREN) // not and error, just list end
    {
        return NULL;
    }
    if (current_token.type != TOKEN_COMMA)
    {
        *err = GENERAL_SYNTAX;

        log_printf("[%lu] expected: '(' or ',' got: ", current_token.line);
        log_token_type(current_token.type);
        return NULL;
    }

    if (next_token.type == TOKEN_EOL) // just skipping
    {
        get_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            return NULL;
        }
    }

    ast_node *param_list_ = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    param_list_->left_child = node_param(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list_);
        return NULL;
    }
    param_list_->right_child = node_param_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&param_list_);
        return NULL;
    }
    param_list_->type = AST_NODE_PARAM_LIST_;
    param_list_->src_line_number = param_list_->left_child->src_line_number;

    return param_list_;
}

static ast_node *node_param(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    token current_token = get_token(err, src_stream, attr_tab);
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

    get_token(err, src_stream, attr_tab); // we dont need info, only capture err
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_COLON)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_COLON, current_token.type, current_token.line);

        ast_node_destruct_recursively(&id_node);
        return NULL;
    }

    current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
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

        log_printf("[%lu] ");
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

static ast_node *node_func_body(error *err, FILE *src_stream, attribute_table *attr_tab)
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

    if (!(current_token.type == TOKEN_OPERATOR && current_token.attribute_index == OP_ASSIGN))
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected: '=' god: ", current_token.line);
        log_token_type(current_token.type);

        return NULL;
    }

    ast_node *func_body = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }

    func_body->type = AST_NODE_PARAM;
    func_body->src_line_number = current_token.line;

    if (next_token.type == TOKEN_EOL)
    {
        func_body->left_child = node_block(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&func_body);
            return NULL;
        }
    }
    else if (is_expression_member(next_token))
    {
        func_body->left_child = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
        if (*err != NONE)
        {
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
static ast_node *node_stat_list(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *statement_node = node_statement(err, src_stream, attr_tab);
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

    stat_list->right_child = node_stat_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&stat_list);
        return NULL;
    }
    return stat_list;
}

static ast_node *node_stat_list_(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    if (current_token.type != TOKEN_EOL && current_token.type != TOKEN_SEMICOLON)
    {
        *err = GENERAL_SYNTAX;
        log_printf("[%lu] expected end of statement\n", current_token.line);
        return NULL;
    }

    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    if (next_token.type == TOKEN_DEDENT)
    {
        return NULL;
    }

    ast_node *statement_node = node_statement(err, src_stream, attr_tab);
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

    stat_list_->right_child = node_stat_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&stat_list_);
        return NULL;
    }
    return stat_list_;
}

static ast_node *node_statement(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    if (next_token.type == TOKEN_KEYWORD)
    {
        switch (next_token.attribute_index)
        {
        case KW_VAL:
        case KW_VAR:
            return node_definition(err, src_stream, attr_tab);
        case KW_WHILE:
            return node_while_loop(err, src_stream, attr_tab);
        case KW_IF:
            return node_if_list(err, src_stream, attr_tab);
        case KW_RETURN:
            return node_return_statement(err, src_stream, attr_tab);
        default:
            break;
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

    ast_node *expression_or_assignment = node_expression_or_assignment_to_id(err, src_stream, attr_tab, true);
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

static ast_node *node_definition(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token next_token = peek_token(err, src_stream, attr_tab);
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
        definition->left_child = node_val_def(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&definition);
            return NULL;
        }
    }
    else if (next_token.type == TOKEN_KEYWORD && next_token.attribute_index == KW_VAR)
    {
        definition->left_child = node_var_def(err, src_stream, attr_tab);
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

    const token assign_id = get_token(err, src_stream, attr_tab);
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

    definition->right_child = node_assign_val(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&definition);
        return NULL;
    }
    return definition;
}

static ast_node *node_expression_or_assignment_to_id(error *err, FILE *src_stream, attribute_table *attr_tab,
                                                     const bool can_be_assignment)
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
    if (*err != NONE)
    {
        return NULL;
    }

    token current_token = EMPTY_TOKEN;
    token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        stack_destruct(&expression_parse_stack);
        return NULL;
    }

    ast_node *tmp_node = NULL;
    bool is_first_cycle = can_be_assignment;

    while (is_expression_member(next_token))
    {
        // expression_parser_stack_item *new_item = new_expression_parser_stack_item(err);
        // if (*err != NONE)
        // {
        //     stack_destruct(&expression_parse_stack);
        //     return NULL;
        // }

        // TODO: implement actual algorithm
        switch (next_token.type)
        {
        case TOKEN_IDENTIFIER:
            tmp_node = node_func_call(err, src_stream, attr_tab);
            if (*err != NONE)
            {
                stack_destruct(&expression_parse_stack);
                return NULL;
            }

            if (tmp_node->type == AST_NODE_IDENTIFIER)
            {
                // there is no way to distinguish
                // id = ...       which is assignment and
                // id + - * / ... which is expression, so I decided to use this trick
                next_token = peek_token(err, src_stream, attr_tab);
                if (*err != NONE)
                {
                    stack_destruct(&expression_parse_stack);
                    ast_node_destruct_recursively(&tmp_node);
                    return NULL;
                }
                if (next_token.type == TOKEN_OPERATOR && next_token.attribute_index == OP_ASSIGN)
                {
                    stack_destruct(&expression_parse_stack);

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

static ast_node *node_while_loop(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
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

    while_node->left_child = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }

    const token do_token = get_token(err, src_stream, attr_tab);
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

    while_node->right_child = node_block(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&while_node);
        return NULL;
    }
    return while_node;
}

static ast_node *node_return_statement(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
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

    const token next_token = peek_token(err, src_stream, attr_tab);
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

    return_node->left_child = node_assign_val(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&return_node);
        return NULL;
    }

    return return_node;
}

static ast_node *node_assign_val(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *expression_node = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
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

static ast_node *node_if_list(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token next_token = peek_token(err, src_stream, attr_tab);
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

    ast_node *if_node = node_if_block(err, src_stream, attr_tab);
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

    if_list->right_child = node_if_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_list);
        return NULL;
    }

    return if_list;
}

static ast_node *node_if_list_(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }
    if (next_token.type != TOKEN_KEYWORD || next_token.attribute_index != KW_ELSE)
    {
        *err = GENERAL_SYNTAX;
        log_error_expected(TOKEN_KEYWORD, next_token.type, next_token.line);

        return NULL;
    }

    ast_node *else_node = node_else_block(err, src_stream, attr_tab);
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

    if_list_->right_child = node_if_list_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_list_);
        return NULL;
    }

    return if_list_;
}

static ast_node *node_if_block(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
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
    if_node->left_child = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }

    const token then_token = get_token(err, src_stream, attr_tab);
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

    if_node->right_child = node_block(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&if_node);
        return NULL;
    }

    return if_node;
}

static ast_node *node_else_block(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
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

    const token after_else = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&else_node);
        return NULL;
    }

    if (after_else.type == TOKEN_KEYWORD && after_else.attribute_index == KW_IF)
    {
        else_node->left_child = node_if_block(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&else_node);
            return NULL;
        }

        return else_node;
    }

    else_node->left_child = node_block(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&else_node);
        return NULL;
    }
    return else_node;
}

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
    get_token(err, src_stream, attr_tab);
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

static ast_node *node_call_params(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *call_params = new_ast_node(err);
    if (*err != NONE)
    {
        return NULL;
    }
    call_params->type = AST_NODE_CALL_PARAMS;

    token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }

    while (next_token.type == TOKEN_EOL)
    {
        get_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }

        next_token = peek_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }
    }

    if (next_token.type == TOKEN_CLOSE_PAREN)
    {
        get_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            ast_node_destruct_recursively(&call_params);
            return NULL;
        }
        current_paren_depth--;

        call_params->src_line_number = next_token.line;

        return call_params;
    }

    call_params->left_child = node_call_param(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }
    call_params->src_line_number = call_params->left_child->src_line_number;

    call_params->right_child = node_call_params_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params);
        return NULL;
    }
    return call_params;
}

static ast_node *node_call_params_(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    const token current_token = get_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

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

    token next_token = peek_token(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        return NULL;
    }

    bool had_end_of_line = false;

    while (next_token.type == TOKEN_EOL)
    {
        had_end_of_line = true;
        get_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            return NULL;
        }
        next_token = peek_token(err, src_stream, attr_tab);
        if (*err != NONE)
        {
            return NULL;
        }
    }

    if (next_token.type == TOKEN_CLOSE_PAREN)
    {
        if (!had_end_of_line)
        {
            *err = GENERAL_SYNTAX;

            log_printf("[%lu] expected call argument after ','\n", next_token.line);
            return NULL;
        }

        get_token(err, src_stream, attr_tab);
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

    call_params_->left_child = node_call_param(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params_);
        return NULL;
    }
    call_params_->right_child = node_call_params_(err, src_stream, attr_tab);
    if (*err != NONE)
    {
        ast_node_destruct_recursively(&call_params_);
        return NULL;
    }
    call_params_->src_line_number = call_params_->left_child->src_line_number;

    return call_params_;
}

static ast_node *node_call_param(error *err, FILE *src_stream, attribute_table *attr_tab)
{
    *err = NONE;

    ast_node *expression_node = node_expression_or_assignment_to_id(err, src_stream, attr_tab, false);
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