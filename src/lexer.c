// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"
#include "utils.h"

#include <ctype.h>
#include <error.h>

char *parse_identifier(error *err, FILE *input);
double complete_double_from_dot(error *err, FILE *input, double int_part);

token lexer_get_next_token(error *err, lexer *l)
{
    token res = {TOKEN_UNDEFINED, -1, 0};
    error internal_err = NONE;

    // ---- FSM implementation ----
    char c = utils_peek_char(&internal_err, l->input, 0);
    switch (c)
    {
    // Whitespaces
    case ' ':
        res.type = TOKEN_WHITESPACE;
        while (utils_peek_char(&internal_err, l->input, 0) == ' ')
            utils_get_char(&internal_err, l->input);
        break;
    case '\n':
        res.type = TOKEN_EOL;
        utils_get_char(&internal_err, l->input);
        break;
    case '\0':
        res.type = TOKEN_EOF;
        utils_get_char(&internal_err, l->input);
        break;

    // Operators
    case ',':
        res.type = TOKEN_COMMA;
        utils_get_char(&internal_err, l->input);
        break;
    case '|':
        res.type = TOKEN_DIVIDER;
        utils_get_char(&internal_err, l->input);
        break;
    case '(':
        res.type = TOKEN_OPEN_PAREN;
        utils_get_char(&internal_err, l->input);
        break;
    case ')':
        res.type = TOKEN_CLOSE_PAREN;
        utils_get_char(&internal_err, l->input);
        break;
    case ':':
        res.type = TOKEN_COLON;
        utils_get_char(&internal_err, l->input);
        break;
    case '=':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, l->input);
        if (utils_peek_char(&internal_err, l->input, 0) == '=')
        {
            utils_get_char(&internal_err, l->input);
            res.attribute_index = OP_EQUAL;
        }
        else
            res.attribute_index = OP_ASSIGN;
        break;
    case '<':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, l->input);
        if (utils_peek_char(&internal_err, l->input, 0) == '=')
        {
            utils_get_char(&internal_err, l->input);
            res.attribute_index = OP_EQUAL_LESS;
        }
        else
            res.attribute_index = OP_LESS;
        break;
    case '>':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, l->input);
        if (utils_peek_char(&internal_err, l->input, 0) == '=')
        {
            utils_get_char(&internal_err, l->input);
            res.attribute_index = OP_EQUAL_GREATER;
        }
        else

            res.attribute_index = OP_GREATER;

        break;
    case '!':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, l->input);
        if (utils_peek_char(&internal_err, l->input, 0) == '=')
        {
            utils_get_char(&internal_err, l->input);
            res.attribute_index = OP_NOT_EQUAL;
        }
        else
        {
            *err = INVALID_SYMBOL;
            res.type = TOKEN_UNDEFINED;
        }
        break;
    case '+':
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_PLUS;
        utils_get_char(&internal_err, l->input);
        break;
    case '-':
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_MINUS;
        utils_get_char(&internal_err, l->input);
        break;
    case '*':
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_MULTIPLY;
        utils_get_char(&internal_err, l->input);
        break;
    
    case '_':
        c = utils_peek_char(&internal_err, l->input, 1);
        if (isalnum(c) || c == '_')
        {
            res.type = TOKEN_IDENTIFIER;
            char *identifier = parse_identifier(err, l->input);
            res.attribute_index = attribute_table_insert_identifier(l->attr_table, err, &identifier);
        }
        else
        {
            utils_get_char(&internal_err, l->input);
            res.type = TOKEN_UNDERSCORE;
        }
        break;
    // Comments
    case '/':
        utils_get_char(&internal_err, l->input);
        c = utils_peek_char(&internal_err, l->input, 0);
        if (c == '/') // single-line comment
        {
            // TODO:
        }
        else if (c == '*') // multi-line comment
        {
            // TODO:
        }
        else
        {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_DIVISION;
        }
        break;
    case '.':
        c = utils_get_char(&internal_err, l->input);
        if (isdigit(utils_peek_char(&internal_err, l->input, 0)))
        {
            double val = complete_double_from_dot(err, l->input, 0.0);
            if (*err == NONE)
            {

                res.type = TOKEN_DOUBLE;
                res.attribute_index = attribute_table_insert_double(l->attr_table, err, val);
            }
        }
        else
        {
            res.type = TOKEN_DOT;
        }

        break;
    default:
        *err = GENERAL_LEXICAL;
        break;
    }
    if (internal_err != NONE && *err == NONE)
        *err = internal_err;
    return res;
}

char *parse_identifier(error *err, FILE *input)
{
    // TODO: implement
    return NULL; 
}

double complete_double_from_dot(error *err, FILE *input, double int_part)
{
    // TODO: implement
    return int_part;
}