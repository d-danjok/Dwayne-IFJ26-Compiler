// @file probe.c
//
// @brief runs lexer + parser on one source file and prints the error code and the AST
//
// usage: probe <source file>
// The report (error code, AST) goes to stdout. The compiler's own log messages go to stderr, and after
// an error also the sequence of tokens that the parser read.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "ast_print.h"
#include "attribute_table.h"
#include "lexer.h"
#include "parser.h"

static token *read_tokens = NULL;
static size_t read_token_count = 0;
static size_t read_token_capacity = 0;

// run.py compiles parser.c with -Dlexer_get_next_token=parser_test_next_token, so every token that the
// parser asks for passes through here and is recorded
token parser_test_next_token(lexer *self, error *err)
{
    const token next = lexer_get_next_token(self, err);
    if (read_token_count == read_token_capacity)
    {
        read_token_capacity = read_token_capacity ? read_token_capacity * 2 : 64;
        read_tokens = realloc(read_tokens, read_token_capacity * sizeof(token));
        if (read_tokens == NULL)
            exit(2);
    }
    read_tokens[read_token_count++] = next;
    return next;
}

static void print_token(FILE *out, const token tok, const attribute_table *attr_tab)
{
    static const char *const type_names[] = {
        [TOKEN_UNDEFINED] = "UNDEFINED",       [TOKEN_EOL] = "EOL",
        [TOKEN_EOF] = "EOF",                   [TOKEN_INT] = "INT",
        [TOKEN_DOUBLE] = "DOUBLE",             [TOKEN_STRING] = "STRING",
        [TOKEN_UNIT] = "UNIT",                 [TOKEN_BOOLEAN] = "BOOLEAN",
        [TOKEN_ANNOTATION] = "ANNOTATION",     [TOKEN_IDENTIFIER] = "IDENTIFIER",
        [TOKEN_KEYWORD] = "KEYWORD",           [TOKEN_OPERATOR] = "OPERATOR",
        [TOKEN_DOT] = "DOT",                   [TOKEN_COMMA] = "COMMA",
        [TOKEN_UNDERSCORE] = "UNDERSCORE",     [TOKEN_COLON] = "COLON",
        [TOKEN_SEMICOLON] = "SEMICOLON",       [TOKEN_OPEN_PAREN] = "OPEN_PAREN",
        [TOKEN_CLOSE_PAREN] = "CLOSE_PAREN",   [TOKEN_INDENT] = "INDENT",
        [TOKEN_DEDENT] = "DEDENT",
    };
    static const char *const keywords[] = {
        [KW_DEF] = "def",       [KW_DO] = "do",         [KW_DOUBLE] = "Double", [KW_ELSE] = "else",
        [KW_IF] = "if",         [KW_IMPORT] = "import", [KW_INT] = "Int",       [KW_RETURN] = "return",
        [KW_STRING] = "String", [KW_THEN] = "then",     [KW_UNIT] = "Unit",     [KW_VAL] = "val",
        [KW_VAR] = "var",       [KW_WHILE] = "while",   [KW_BOOLEAN] = "Boolean", [KW_CASE] = "case",
        [KW_CLASS] = "class",   [KW_EXTENSION] = "extension", [KW_FOR] = "for", [KW_LIST] = "List",
        [KW_MATCH] = "match",   [KW_TYPE] = "type",
    };
    static const char *const operators[] = {
        [OP_ASSIGN] = "=",         [OP_PLUS] = "+",        [OP_MINUS] = "-",    [OP_MULTIPLY] = "*",
        [OP_DIVISION] = "/",       [OP_EQUAL] = "==",      [OP_NOT_EQUAL] = "!=", [OP_GREATER] = ">",
        [OP_EQUAL_GREATER] = ">=", [OP_LESS] = "<",        [OP_EQUAL_LESS] = "<=", [OP_NOT] = "!",
        [OP_AND] = "&&",           [OP_OR] = "||",
    };
#define COUNT(array) ((int)(sizeof(array) / sizeof(*(array))))
#define LOOKUP(array, index) ((index) >= 0 && (index) < COUNT(array) && (array)[index] ? (array)[index] : "?")

    error err = NONE; // a wrong attribute index only makes the getters return NULL / 0
    fputs(LOOKUP(type_names, (int)tok.type), out);
    switch (tok.type)
    {
    case TOKEN_IDENTIFIER: {
        const char *name = attribute_table_get_identifier(attr_tab, &err, tok.attribute_index);
        fprintf(out, "(%s)", name ? name : "?");
        break;
    }
    case TOKEN_INT:
        fprintf(out, "(%d)", attribute_table_get_integer(attr_tab, &err, tok.attribute_index));
        break;
    case TOKEN_DOUBLE:
        fprintf(out, "(%g)", attribute_table_get_double(attr_tab, &err, tok.attribute_index));
        break;
    case TOKEN_STRING:
        fputc('(', out);
        ast_print_quoted(out, attribute_table_get_string(attr_tab, &err, tok.attribute_index), '"');
        fputc(')', out);
        break;
    case TOKEN_BOOLEAN:
        fprintf(out, "(%s)", tok.attribute_index ? "true" : "false");
        break;
    case TOKEN_KEYWORD:
        fprintf(out, "(%s)", LOOKUP(keywords, tok.attribute_index));
        break;
    case TOKEN_OPERATOR:
        fprintf(out, "(%s)", LOOKUP(operators, tok.attribute_index));
        break;
    default:
        break;
    }
#undef LOOKUP
#undef COUNT
}

// one row per logical line: the row ends after EOL and starts with the source line of its first token
static void print_read_tokens(FILE *out, const attribute_table *attr_tab)
{
    fprintf(out, "tokens read before the error (%zu, the last one is where reading stopped):\n", read_token_count);
    bool at_row_start = true;
    for (size_t index = 0; index < read_token_count; ++index)
    {
        if (at_row_start)
            fprintf(out, "%4zu |", read_tokens[index].line);
        fputc(' ', out);
        print_token(out, read_tokens[index], attr_tab);
        at_row_start = read_tokens[index].type == TOKEN_EOL;
        if (at_row_start || index + 1 == read_token_count)
            fputc('\n', out);
    }
}

// utils.h declares log_printf, but the lexer-parser merge dropped its definition from utils.c.
// This weak copy is used only while the real one is missing; the linker prefers the real one.
__attribute__((weak)) void log_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <source file>\n", argv[0]);
        return 2;
    }
    FILE *input = fopen(argv[1], "rb"); // a file, not stdin: the lexer's lookahead needs a seekable stream
    if (input == NULL)
    {
        perror(argv[1]);
        return 2;
    }

    error err = NONE;
    attribute_table *attr_tab = new_attribute_table(&err);
    if (err != NONE || attr_tab == NULL)
        return 2;

    ast_node *tree = parser_run(&err, input, attr_tab);

    if (error_is_internal(err))
        fprintf(stderr, "internal error code: %d\n", err);
    printf("error: %d\n", error_is_internal(err) ? GENERAL_INTERNAL : err);
    if (err == NONE)
        ast_print(stdout, tree, attr_tab);
    else
    {
        fflush(stdout); // keeps "error:" above the token list when both streams go to one terminal
        print_read_tokens(stderr, attr_tab);
    }

    ast_node_destruct_recursively(&tree);
    free(read_tokens);
    destruct_attribute_table(&attr_tab);
    fclose(input);
    return 0;
}
