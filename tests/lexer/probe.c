/* IFJ26: lexer test adapter. */
#include "lexer.h"

#include <stdlib.h>
#include <string.h>

/* Convert token enums to names without relying on their numeric ordering. */
static const char *token_name(token_type type)
{
#define NAME(value) case value: return #value
    switch (type)
    {
        NAME(TOKEN_UNDEFINED);
        NAME(TOKEN_EOL);
        NAME(TOKEN_EOF);
        NAME(TOKEN_INT);
        NAME(TOKEN_DOUBLE);
        NAME(TOKEN_STRING);
        NAME(TOKEN_UNIT);
        NAME(TOKEN_BOOLEAN);
        NAME(TOKEN_ANNOTATION);
        NAME(TOKEN_IDENTIFIER);
        NAME(TOKEN_KEYWORD);
        NAME(TOKEN_OPERATOR);
        NAME(TOKEN_DOT);
        NAME(TOKEN_COMMA);
        NAME(TOKEN_UNDERSCORE);
        NAME(TOKEN_SEMICOLON);
        NAME(TOKEN_OPEN_PAREN);
        NAME(TOKEN_CLOSE_PAREN);
        NAME(TOKEN_COLON);
        NAME(TOKEN_OPEN_BRACKET);
        NAME(TOKEN_CLOSE_BRACKET);
        NAME(TOKEN_OPEN_BRACE);
        NAME(TOKEN_CLOSE_BRACE);
        NAME(TOKEN_INTERP_BEGIN);
        NAME(TOKEN_INTERP_END);
        NAME(TOKEN_INDENT);
        NAME(TOKEN_DEDENT);
    }
#undef NAME
    return "INVALID_TOKEN_ENUM";
}

/* Expose operator identity, independently of attribute table indices. */
static const char *operator_name(int value)
{
#define NAME(value) case value: return #value
    switch (value)
    {
        NAME(OP_ASSIGN);
        NAME(OP_PLUS);
        NAME(OP_MINUS);
        NAME(OP_MULTIPLY);
        NAME(OP_DIVISION);
        NAME(OP_EQUAL);
        NAME(OP_NOT_EQUAL);
        NAME(OP_GREATER);
        NAME(OP_EQUAL_GREATER);
        NAME(OP_LESS);
        NAME(OP_EQUAL_LESS);
        NAME(OP_NOT);
        NAME(OP_AND);
        NAME(OP_SEPARATOR);
        NAME(OP_OR);
        NAME(OP_CONCAT);
        NAME(OP_ARROW);
        NAME(OP_LEFT_ARROW);
    }
#undef NAME
    return "INVALID_OPERATOR_ENUM";
}

/* Expose keyword identity, so one shared index for every keyword fails. */
static const char *keyword_name(int value)
{
#define NAME(value) case value: return #value
    switch (value)
    {
        NAME(KW_DEF);
        NAME(KW_DO);
        NAME(KW_DOUBLE);
        NAME(KW_ELSE);
        NAME(KW_IF);
        NAME(KW_IMPORT);
        NAME(KW_INT);
        NAME(KW_RETURN);
        NAME(KW_STRING);
        NAME(KW_THEN);
        NAME(KW_UNIT);
        NAME(KW_VAL);
        NAME(KW_VAR);
        NAME(KW_WHILE);
        NAME(KW_BOOLEAN);
        NAME(KW_CASE);
        NAME(KW_CLASS);
        NAME(KW_EXTENSION);
        NAME(KW_FOR);
        NAME(KW_LIST);
        NAME(KW_MATCH);
        NAME(KW_TYPE);
    }
#undef NAME
    return "INVALID_KEYWORD_ENUM";
}

/* Hex encoding preserves tabs, newlines, quotes and backslashes in the protocol. */
static void print_hex(const char *value)
{
    if (value == NULL)
        return;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor)
        printf("%02x", *cursor);
}

/* Use public getters, so a correct token with an invalid attribute also fails. */
static error print_token(token value, attribute_table *table)
{
    error err = NONE;
    printf("TOKEN\t%s\t%zu\t%d\t", token_name(value.type), value.line, value.attribute_index);
    switch (value.type)
    {
    case TOKEN_INT:
        printf("%d", attribute_table_get_integer(table, &err, value.attribute_index));
        break;
    case TOKEN_DOUBLE:
        printf("%a", attribute_table_get_double(table, &err, value.attribute_index));
        break;
    case TOKEN_STRING:
        print_hex(attribute_table_get_string(table, &err, value.attribute_index));
        break;
    case TOKEN_IDENTIFIER:
        print_hex(attribute_table_get_identifier(table, &err, value.attribute_index));
        break;
    case TOKEN_OPERATOR:
        printf("%s", operator_name(value.attribute_index));
        break;
    case TOKEN_KEYWORD:
        printf("%s", keyword_name(value.attribute_index));
        break;
    case TOKEN_BOOLEAN:
        printf("%d", value.attribute_index);
        break;
    default:
        break;
    }
    putchar('\n');
    return err;
}

/* Interleave scanners with separate inputs, tables and line counters. */
static bool check_independent_instances(lexer *first)
{
    error err = NONE;
    FILE *input = tmpfile();
    attribute_table *table = new_attribute_table(&err);
    if (input == NULL || table == NULL || err != NONE)
        return false;
    lexer *second = new_lexer(&err, input, table);
    if (second == NULL || err != NONE)
        return false;
    bool passed = false;
    if (fputs("first\nlast", first->input) >= 0 && fputs("other", input) >= 0 &&
        fseek(first->input, 0, SEEK_SET) == 0 && fseek(input, 0, SEEK_SET) == 0)
    {
        token a = lexer_get_next_token(first, &err);
        if (err == NONE && a.type == TOKEN_IDENTIFIER)
        {
            token b = lexer_get_next_token(second, &err);
            if (err == NONE && b.type == TOKEN_IDENTIFIER)
            {
                token eol = lexer_get_next_token(first, &err);
                if (err == NONE && eol.type == TOKEN_EOL)
                {
                    token last = lexer_get_next_token(first, &err);
                    if (err == NONE && last.type == TOKEN_IDENTIFIER)
                    {
                        char *first_value = attribute_table_get_identifier(first->attr_table, &err, a.attribute_index);
                        if (err == NONE && first_value != NULL && strcmp(first_value, "first") == 0)
                        {
                            char *second_value = attribute_table_get_identifier(table, &err, b.attribute_index);
                            passed = err == NONE && second_value != NULL && strcmp(second_value, "other") == 0 &&
                                     first->current_line == 2 && second->current_line == 1;
                        }
                    }
                }
            }
        }
    }
    lexer_destruct(&second);
    destruct_attribute_table(&table);
    fclose(input);
    return passed;
}

/* Stored strings survive buffer reuse, reallocation, and scanner destruction. */
static bool check_attribute_lifetime(lexer **scanner, attribute_table *table)
{
    FILE *input = (*scanner)->input;
    if (fputs("\"first\" \"", input) < 0)
        return false;
    for (size_t index = 0; index < 257; ++index)
        if (fputc('x', input) == EOF)
            return false;
    if (fputc('"', input) == EOF || fseek(input, 0, SEEK_SET) != 0)
        return false;
    error err = NONE;
    token first = lexer_get_next_token(*scanner, &err);
    if (err != NONE || first.type != TOKEN_STRING)
        return false;
    token second = lexer_get_next_token(*scanner, &err);
    if (err != NONE || second.type != TOKEN_STRING)
        return false;
    lexer_destruct(scanner);
    char *value = attribute_table_get_string(table, &err, first.attribute_index);
    if (err != NONE || value == NULL || strcmp(value, "first") != 0)
        return false;
    value = attribute_table_get_string(table, &err, second.attribute_index);
    if (err != NONE || value == NULL || strlen(value) != 257)
        return false;
    for (size_t index = 0; index < 257; ++index)
        if (value[index] != 'x')
            return false;
    return true;
}

/* Exercise the constructor and sticky-error contract independently of token cases. */
static int api_test(const char *name)
{
    error err = NONE;
    FILE *input = tmpfile();
    attribute_table *table = new_attribute_table(&err);
    lexer *scanner = NULL;
    bool passed = false;
    if (input == NULL || table == NULL || err != NONE)
        return 2;
    if (strcmp(name, "null_error") == 0)
    {
        scanner = new_lexer(NULL, input, table);
        passed = scanner == NULL;
    }
    else if (strcmp(name, "null_input") == 0)
    {
        scanner = new_lexer(&err, NULL, table);
        passed = scanner == NULL && err == INTERNAL_MEMORY_ALLOCATION;
    }
    else if (strcmp(name, "null_table") == 0)
    {
        scanner = new_lexer(&err, input, NULL);
        passed = scanner == NULL && err == INTERNAL_MEMORY_ALLOCATION;
    }
    else
    {
        scanner = new_lexer(&err, input, table);
        if (scanner != NULL && err == NONE)
        {
            if (strcmp(name, "constructor") == 0)
            {
                passed = scanner->input == input && scanner->attr_table == table && scanner->buffer == NULL &&
                         scanner->buffer_index == 0 && scanner->buffer_capacity == 0 && scanner->coment_depth == 0 &&
                         !scanner->comment_sline && scanner->indent_size_s != NULL && scanner->current_line == 1;
            }
            else if (strcmp(name, "sticky_error") == 0)
            {
                if (fputs("123", input) < 0 || fseek(input, 0, SEEK_SET) != 0)
                    return 2;
                err = GENERAL_LEXICAL;
                token value = lexer_get_next_token(scanner, &err);
                passed = value.type == TOKEN_UNDEFINED && err == GENERAL_LEXICAL && ftell(input) == 0;
            }
            else if (strcmp(name, "initial_line") == 0)
            {
                token value = lexer_get_next_token(scanner, &err);
                passed = err == NONE && value.type == TOKEN_EOF && value.line == 1;
            }
            else if (strcmp(name, "independent_instances") == 0)
                passed = check_independent_instances(scanner);
            else if (strcmp(name, "attribute_lifetime") == 0)
                passed = check_attribute_lifetime(&scanner, table);
        }
    }
    printf("API\t%s\t%s\terror=%d\n", name, passed ? "PASS" : "FAIL", err);
    lexer_destruct(&scanner);
    destruct_attribute_table(&table);
    fclose(input);
    return passed ? 0 : 1;
}

/* Run one isolated fixture; report two EOF reads to verify stable termination. */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && strcmp(argv[1], "--api") == 0)
        return api_test(argv[2]);
    if (argc != 2)
        return 2;

    FILE *input = strcmp(argv[1], "--pipe") == 0 ? stdin : fopen(argv[1], "rb");
    if (input == NULL)
        return 2;
    error err = NONE;
    attribute_table *table = new_attribute_table(&err);
    if (err != NONE || table == NULL)
        return 2;
    lexer *scanner = new_lexer(&err, input, table);
    if (err != NONE || scanner == NULL)
        return 2;
    int eof_count = 0;
    int status = 2;
    for (size_t count = 0; count < 10000; ++count)
    {
        token value = lexer_get_next_token(scanner, &err);
        if (err != NONE)
        {
            printf("ERROR\t%d\t%s\t%zu\n", err, token_name(value.type), value.line);
            status = 0;
            break;
        }
        error attribute_error = print_token(value, table);
        if (attribute_error != NONE)
        {
            printf("ATTRIBUTE_ERROR\t%d\n", attribute_error);
            break;
        }
        if (value.type == TOKEN_EOF && ++eof_count == 2)
        {
            status = 0;
            break;
        }
    }
    printf("COUNTER\t%zu\n", scanner->current_line);
    lexer_destruct(&scanner);
    destruct_attribute_table(&table);
    if (input != stdin)
        fclose(input);
    return status;
}
