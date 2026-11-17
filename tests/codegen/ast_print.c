// @file ast_print.c
//
// @brief debugging/testing utility that prints an AST as a tree

#include "ast_print.h"

#include <stdbool.h>

#define MAX_PRINT_DEPTH 512

static const char *node_type_name(const ast_node_type type)
{
#define NAME(value)                                                                                                    \
    case AST_NODE_##value:                                                                                             \
        return #value
    switch (type)
    {
        NAME(UNDEFINED);
        NAME(PROGRAM);
        NAME(DEF_LIST);
        NAME(VAL_DEF);
        NAME(VAR_DEF);
        NAME(DEF_BODY);
        NAME(FUNC_DEF);
        NAME(MAIN_FUNC_DEF);
        NAME(FUNC_START);
        NAME(FUNC_NAME);
        NAME(FUNC_HEAD);
        NAME(PARAM_LIST);
        NAME(PARAM_LIST_);
        NAME(PARAM);
        NAME(FUNC_BODY);
        NAME(BLOCK);
        NAME(STAT_LIST);
        NAME(STAT_LIST_);
        NAME(STATEMENT);
        NAME(DEFINITION);
        NAME(ASSIGN_VAL);
        NAME(WHILE);
        NAME(IF_LIST);
        NAME(IF_LIST_);
        NAME(IF);
        NAME(ELSE);
        NAME(RETURN);
        NAME(FUNC_CALL);
        NAME(CALL_PARAMS);
        NAME(CALL_PARAMS_);
        NAME(CALL_PARAM);
        NAME(UNARY_OPERATION);
        NAME(BINARY_OPERATION);
        NAME(LITERAL);
        NAME(IDENTIFIER);
        NAME(TYPE);
        NAME(INT_TO_DOUBLE_CONVERSION);
    }
#undef NAME
    return "INVALID_NODE_TYPE";
}

static const char *operator_symbol(const ast_operator op)
{
    switch (op)
    {
    case AST_OP_NONE:
        return NULL;
    case AST_OP_PLUS:
        return "+";
    case AST_OP_MINUS:
        return "-";
    case AST_OP_MINUS_UNARY:
        return "neg";
    case AST_OP_MULTIPLY:
        return "*";
    case AST_OP_DIV:
        return "/";
    case AST_OP_GREATER:
        return ">";
    case AST_OP_GREATER_EQUAL:
        return ">=";
    case AST_OP_LESS:
        return "<";
    case AST_OP_LESS_EQUAL:
        return "<=";
    case AST_OP_EQUAL:
        return "==";
    case AST_OP_NOT_EQUAL:
        return "!=";
    case AST_OP_AND:
        return "&&";
    case AST_OP_OR:
        return "||";
    case AST_OP_NOT:
        return "!";
    case AST_OP_CONCAT:
        return "concat";
    }
    return "INVALID_OPERATOR";
}

static const char *literal_type_name(const ast_literal_type type)
{
    switch (type)
    {
    case AST_TYPE_NONE:
        return NULL;
    case AST_TYPE_INT:
        return "Int";
    case AST_TYPE_DOUBLE:
        return "Double";
    case AST_TYPE_STRING:
        return "String";
    case AST_TYPE_UNIT:
        return "Unit";
    case AST_TYPE_BOOLEAN:
        return "Boolean";
    }
    return "INVALID_TYPE";
}

void ast_print_quoted(FILE *out, const char *text, const char quote)
{
    if (text == NULL)
    {
        fputs("<missing in attribute table>", out);
        return;
    }
    fputc(quote, out);
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '\n')
            fputs("\\n", out);
        else if (*cursor == '\t')
            fputs("\\t", out);
        else if (*cursor == '\r')
            fputs("\\r", out);
        else if (*cursor == '\\' || *cursor == quote)
            fprintf(out, "\\%c", *cursor);
        else if (*cursor < 0x20 || *cursor == 0x7f)
            fprintf(out, "\\x%02x", *cursor);
        else
            fputc(*cursor, out);
    }
    fputc(quote, out);
}

static void print_literal_value(FILE *out, const ast_node *node, const attribute_table *attr_tab)
{
    error err = NONE;
    switch (node->literal_type)
    {
    case AST_TYPE_INT:
        fprintf(out, " %d", attribute_table_get_integer(attr_tab, &err, node->attribute_index));
        break;
    case AST_TYPE_DOUBLE:
        fprintf(out, " %.17g", attribute_table_get_double(attr_tab, &err, node->attribute_index));
        break;
    case AST_TYPE_STRING:
        fputc(' ', out);
        ast_print_quoted(out, attribute_table_get_string(attr_tab, &err, node->attribute_index), '"');
        break;
    case AST_TYPE_BOOLEAN:
        fprintf(out, " %s", node->attribute_index ? "true" : "false");
        break;
    default:
        break;
    }
    if (err != NONE)
        fprintf(out, " <attribute error %d>", err);
}

static void print_node_line(FILE *out, const ast_node *node, const attribute_table *attr_tab)
{
    fputs(node_type_name(node->type), out);

    if (operator_symbol(node->operator) != NULL)
        fprintf(out, " %s", operator_symbol(node->operator));

    if (literal_type_name(node->literal_type) != NULL)
        fprintf(out, " %s", literal_type_name(node->literal_type));

    if (node->type == AST_NODE_LITERAL)
        print_literal_value(out, node, attr_tab);
    else if (node->type == AST_NODE_IDENTIFIER)
    {
        error err = NONE;
        fputc(' ', out);
        ast_print_quoted(out, attribute_table_get_identifier(attr_tab, &err, node->attribute_index), '\'');
    }
    else if (node->attribute_index != -1)
        fprintf(out, " attr=%d", node->attribute_index);

    if (node->block_index != -1)
        fprintf(out, " block=%ld", node->block_index);
    if (node->src_line_number != 0)
        fprintf(out, " @%zu", node->src_line_number);
    fputc('\n', out);
}

// is_last_child[level] tells whether the ancestor at that depth was the last child, which picks the indent drawing
static void print_subtree(FILE *out, const ast_node *node, const attribute_table *attr_tab, bool *is_last_child,
                          const size_t depth, const char side)
{
    for (size_t level = 1; level < depth; ++level)
        fputs(is_last_child[level] ? "   " : "│  ", out);
    if (depth > 0)
        fprintf(out, "%s%c ", is_last_child[depth] ? "└─" : "├─", side);

    print_node_line(out, node, attr_tab);

    if (depth + 1 >= MAX_PRINT_DEPTH)
    {
        if (node->left_child != NULL || node->right_child != NULL)
            fputs("... (tree is deeper than the printer supports)\n", out);
        return;
    }
    if (node->left_child != NULL)
    {
        is_last_child[depth + 1] = node->right_child == NULL;
        print_subtree(out, node->left_child, attr_tab, is_last_child, depth + 1, 'L');
    }
    if (node->right_child != NULL)
    {
        is_last_child[depth + 1] = true;
        print_subtree(out, node->right_child, attr_tab, is_last_child, depth + 1, 'R');
    }
}

void ast_print(FILE *out, const ast_node *root, const attribute_table *attr_tab)
{
    if (root == NULL)
    {
        fputs("(empty tree)\n", out);
        return;
    }
    bool is_last_child[MAX_PRINT_DEPTH] = {false};
    print_subtree(out, root, attr_tab, is_last_child, 0, ' ');
}
