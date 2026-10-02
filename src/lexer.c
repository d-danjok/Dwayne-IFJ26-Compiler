// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"

#include "utils.c"

#include <ctype.h>
#include <math.h>

char *parse_identifier(error *err, FILE *input);
char *parse_literal(error *err, FILE *input);
double complete_double_from_dot(error *err, FILE *input, double int_part);
double complete_double_from_e(error *err, FILE *input, double int_part);

// TODO: line-tracing, proper error handling, identifiers and literals
// (possible integer overflow in case[0-9])
// todo: check errors when token is EOF

token lexer_get_next_token(lexer *self, error *err)
{
    FILE *in = self->input;
    token res = {TOKEN_UNDEFINED, -1, 0};
    error internal_err = NONE;
    char *identifier = NULL;

    // ---- FSM implementation ----
    char c = utils_peek_char(&internal_err, in, 0);
    switch (c)
    {
    // Whitespaces
    case ' ':
        res.type = TOKEN_WHITESPACE;
        while (utils_peek_char(&internal_err, in, 0) == ' ')
            utils_get_char(&internal_err, in);
        break;
    case '\n':
        res.type = TOKEN_EOL;
        utils_get_char(&internal_err, in);
        break;
    case '\0':
        res.type = TOKEN_EOF;
        utils_get_char(&internal_err, in);
        break;

    // Operators
    case ',':
        res.type = TOKEN_COMMA;
        utils_get_char(&internal_err, in);
        break;
    case '|':
        res.type = TOKEN_DIVIDER;
        utils_get_char(&internal_err, in);
        break;
    case '(':
        res.type = TOKEN_OPEN_PAREN;
        utils_get_char(&internal_err, in);
        break;
    case ')':
        res.type = TOKEN_CLOSE_PAREN;
        utils_get_char(&internal_err, in);
        break;
    case ':':
        res.type = TOKEN_COLON;
        utils_get_char(&internal_err, in);
        break;
    case '=':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, in);
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL;
        }
        else
            res.attribute_index = OP_ASSIGN;
        break;
    case '<':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, in);
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL_LESS;
        }
        else
            res.attribute_index = OP_LESS;
        break;
    case '>':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, in);
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL_GREATER;
        }
        else

            res.attribute_index = OP_GREATER;

        break;
    case '!':
        res.type = TOKEN_OPERATOR;
        utils_get_char(&internal_err, in);
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
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
        utils_get_char(&internal_err, in);
        break;
    case '-':
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_MINUS;
        utils_get_char(&internal_err, in);
        break;
    case '*':
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_MULTIPLY;
        utils_get_char(&internal_err, in);
        break;

    // Identifiers
    case 'a' ... 'z':
        res.type = TOKEN_IDENTIFIER;
        identifier = parse_identifier(err, in);
        res.attribute_index = attribute_table_insert_identifier(self->attr_table, err, &identifier);
        break;
    case 'A' ... 'Z':
        res.type = TOKEN_IDENTIFIER;
        identifier = parse_identifier(err, in);
        res.attribute_index = attribute_table_insert_identifier(self->attr_table, err, &identifier);
        break;
    case '_':
        c = utils_peek_char(&internal_err, in, 1);
        if (isalnum(c) || c == '_')
        {
            res.type = TOKEN_IDENTIFIER;
            identifier = parse_identifier(err, in);
            res.attribute_index = attribute_table_insert_identifier(self->attr_table, err, &identifier);
        }
        else
        {
            utils_get_char(&internal_err, in);
            res.type = TOKEN_UNDERSCORE;
        }
        break;

    // Annotations
    case '@':
        utils_get_char(&internal_err, in);
        if (utils_peek_char(&internal_err, in, 0) == 'm' && utils_peek_char(&internal_err, in, 1) == 'a' &&
            utils_peek_char(&internal_err, in, 2) == 'i' && utils_peek_char(&internal_err, in, 3) == 'n')
        {
            for (int i = 0; i < 4; i++)
                utils_get_char(&internal_err, in);
            res.type = TOKEN_ANNOTATION;
        }
        else
        {
            *err = INVALID_SYMBOL;
        }
        break;

    // Comments
    case '/':
        utils_get_char(&internal_err, in);
        c = utils_peek_char(&internal_err, in, 0);
        if (c == '/') // single-line comment
        {
            while ((c = utils_peek_char(&internal_err, in, 0)) != '\n' && internal_err == NONE)
                utils_get_char(&internal_err, in);

            if (internal_err == INTERNAL_END_OF_FILE)
                res.type = TOKEN_EOF;
            else if (internal_err != NONE)
                *err = internal_err;

            else
            {
                utils_get_char(&internal_err, in);
                res.type = TOKEN_EOL;
            }
        }
        else if (c == '*') // multi-line comment
        {
            utils_get_char(&internal_err, in);
            int depth = 1;
            bool Sline_comment = true;
            while (depth > 0)
            {
                c = utils_get_char(&internal_err, in);
                if (c == '\0')
                {
                    *err = UNFINISHED_COMMENT;
                    break;
                }
                else if (c == '\n')
                    Sline_comment = false;
                else if (c == '*' && utils_peek_char(&internal_err, in, 0) == '/')
                {
                    utils_get_char(&internal_err, in);
                    depth--;
                }
                else if (c == '/' && utils_peek_char(&internal_err, in, 0) == '*')
                {
                    utils_get_char(&internal_err, in);
                    depth++;
                }
            }
            if (*err != NONE)
                break;
            if (Sline_comment)
                res.type = TOKEN_WHITESPACE;
            else
            {
                res.type = TOKEN_EOL;
                while ((c = utils_peek_char(&internal_err, in, 0)) == ' ' && internal_err == NONE)

                    utils_get_char(&internal_err, in);

                if (internal_err != NONE && internal_err != INTERNAL_END_OF_FILE)
                {
                    *err = internal_err;
                }
                else if (c == '\n')
                {
                    utils_get_char(&internal_err, in);
                    res.type = TOKEN_EOL;
                }
                else
                    *err = SYMOBL_AFTER_COMMENT;
            }
        }
        else
        {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_DIVISION;
        }
        break;
        // Literals
    case '"':
        res.type = TOKEN_STRING;
        char *literal = parse_literal(err, in);
        res.attribute_index = attribute_table_insert_string(self->attr_table, err, &literal);
        break;
    // Numeric
    case '0' ... '9':
        bool integer = true;
        double val = 0;

        char c = utils_peek_char(&internal_err, in, 0);
        if (c == '0')
        {
            while (c == '0')
            {
                utils_get_char(&internal_err, in); // eat zero
                c = utils_peek_char(&internal_err, in, 0);
                if (c == '_')
                {
                    utils_get_char(&internal_err, in); // eat underscore
                    c = utils_peek_char(&internal_err, in, 0);
                    if (c != '0')
                    {
                        *err = INVALID_SYMBOL;
                        break;
                    }
                }
                else if (c == '.')
                {
                    utils_get_char(&internal_err, in); // eat dot
                    val = complete_double_from_dot(err, in, val);
                    integer = false;
                    break;
                }
                else if (c == 'e' || c == 'E')
                {
                    c = utils_get_char(&internal_err, in); // eat e
                    val = complete_double_from_e(err, in, val);
                    integer = false;
                    break;
                }
            }
        }
        else
            while (isdigit(c))
            {
                val = val * 10 + (c - '0');
                utils_get_char(&internal_err, in); // eat digit
                c = utils_peek_char(&internal_err, in, 0);
                if (c == '_')
                {
                    utils_get_char(&internal_err, in); // eat underscore
                    c = utils_peek_char(&internal_err, in, 0);
                    if (!isdigit(c))
                    {
                        *err = INVALID_SYMBOL;
                        break;
                    }
                }
                else if (c == '.')
                {
                    utils_get_char(&internal_err, in); // eat dot
                    val = complete_double_from_dot(err, in, val);
                    integer = false;
                    break;
                }
                else if (c == 'e' || c == 'E')
                {
                    utils_get_char(&internal_err, in); // eat e
                    val = complete_double_from_e(err, in, val);
                    integer = false;
                    break;
                }
            }
        if (*err == NONE && internal_err != INTERNAL_END_OF_FILE)
            *err = internal_err;

        if (integer && *err == NONE)
        {
            res.type = TOKEN_INT;

            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, (int)val);
        }
        else if (*err == NONE)
        {
            res.type = TOKEN_DOUBLE;
            res.attribute_index = attribute_table_insert_double(self->attr_table, err, val);
        }
        break;
    case '.':
        c = utils_get_char(&internal_err, in);
        if (isdigit(utils_peek_char(&internal_err, in, 0)))
        {
            double val = complete_double_from_dot(err, in, 0.0);
            if (*err == NONE)
            {

                res.type = TOKEN_DOUBLE;
                res.attribute_index = attribute_table_insert_double(self->attr_table, err, val);
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
char *parse_literal(error *err, FILE *input)
{
    // TODO:
    return NULL;
}

double complete_double_from_dot(error *err, FILE *input, double int_part)
{
    error internal_err = NONE;
    int pos = 0;
    char c = utils_peek_char(err, input, 0);
    if (!isdigit(c))
    {
        *err = INVALID_SYMBOL;
        return int_part;
    }
    while (isdigit(c))
    {
        int_part += (c - '0') / pow(10.0, ++pos);
        utils_get_char(&internal_err, input); // eat digit
        c = utils_peek_char(&internal_err, input, 0);
        if (c == '_')
        {
            utils_get_char(&internal_err, input);         // eat underscore
            c = utils_peek_char(&internal_err, input, 0); // eat underscore
            if (!isdigit(c))
            {
                *err = INVALID_SYMBOL;
                break;
            }
        }
        else if (c == 'e' || c == 'E')
        {
            utils_get_char(&internal_err, input); // eat e
            int_part = complete_double_from_e(err, input, int_part);
            break;
        }
        else if (c == 'd' || c == 'D')
        {
            utils_get_char(&internal_err, input); // eat d
            break;
        }
    }

    return int_part;
}
double complete_double_from_e(error *err, FILE *input, double int_part)
{
    error internal_err = NONE;
    double exponent = 0.0;
    bool sign = 1;
    char c = utils_peek_char(err, input, 0);
    if (c == '+' || c == '-')
    {
        sign = c == '-' ? 0 : 1;
        utils_get_char(&internal_err, input); // eat sign
        c = utils_peek_char(&internal_err, input, 0);
    }
    if (!isdigit(c))
    {
        *err = INVALID_SYMBOL;
    }
    else
        while (isdigit(c))
        {
            exponent = exponent * 10.0 + (c - '0');

            utils_get_char(&internal_err, input); // eat digit
            c = utils_peek_char(&internal_err, input, 0);
            if (c == '_')
            {
                utils_get_char(&internal_err, input); // eat underscore
                c = utils_peek_char(&internal_err, input, 0);
                if (!isdigit(c))
                {
                    *err = INVALID_SYMBOL;
                    break;
                }
            }
            else if (c == 'd' || c == 'D')
            {
                utils_get_char(&internal_err, input); // eat d
                break;
            }
        }
    if (int_part != 0.0)
    {
        int_part *= pow(10.0, (sign ? 1 : -1) * exponent);
    }
    return int_part;
}