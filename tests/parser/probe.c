// @file probe.c
//
// @brief runs lexer + parser on one source file and prints the error code and the AST
//
// usage: probe <source file>
// The compiler's own log messages go to stderr, the report goes to stdout.

#include <stdarg.h>
#include <stdio.h>

#include "ast.h"
#include "ast_print.h"
#include "attribute_table.h"
#include "lexer.h"
#include "parser.h"

lexer *parser_test_lexer = NULL; // used by lexer_glue.h

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
    parser_test_lexer = new_lexer(&err, input, attr_tab);
    if (err != NONE || parser_test_lexer == NULL)
        return 2;

    ast_node *tree = parser_run(&err, input, attr_tab);

    if (error_is_internal(err))
        fprintf(stderr, "internal error code: %d\n", err);
    printf("error: %d\n", error_is_internal(err) ? GENERAL_INTERNAL : err);
    if (err == NONE)
        ast_print(stdout, tree, attr_tab);

    ast_node_destruct_recursively(&tree);
    lexer_destruct(&parser_test_lexer);
    destruct_attribute_table(&attr_tab);
    fclose(input);
    return 0;
}
