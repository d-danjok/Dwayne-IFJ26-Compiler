// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"

#include "utils.h"

#include <ctype.h>
#include <error.h>
#include <math.h>
#include <token.h>


// TODO: line-tracing, proper error handling, identifiers and literals
// (possible integer overflow in case[0-9])
// todo: check errors when token is EOF

token lexer_get_next_token(lexer *self, error *err, state current_state)
{
    FILE *in = self->input;
    token res = {TOKEN_UNDEFINED, -1, 0};
    error internal_err = NONE;
    char *identifier = NULL;

    // ---- FSM implementation ----
    switch (current_state)
    {
    case STATE_START:
        char c = utils_get_char(&internal_err, in);
        switch (c)
        {
        // EOL, EOF
        case '\n':
            res.type = TOKEN_EOL;
            break;
        case '\0':
            res.type = TOKEN_EOF;
            break;

        // Simple tokens
        case '|':
            res.type = TOKEN_SEPARATOR;
            break;
        case ',':
            res.type = TOKEN_COMMA;
            break;
        case ':':
            res.type = TOKEN_COLON;
            break;
        case '(':
            res.type = TOKEN_OPEN_PAREN;
            break;
        case ')':
            res.type = TOKEN_CLOSE_PAREN;
            break;
        // Operators
        case '+':
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_PLUS;
            break;
        case '-':
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_MINUS;
            break;
        case '*':
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_MULTIPLY;
            break;
        case '/':
            res = lexer_get_next_token(self, err, STATE_DIVIDE);
            break;
        case '=':
            res = lexer_get_next_token(self, err, STATE_EQUAL);
            break;
        case '!':
            res = lexer_get_next_token(self, err, STATE_NOT);
            break;
        case '<':
            res = lexer_get_next_token(self, err, STATE_LESS);
            break;
        case '>':
            res = lexer_get_next_token(self, err, STATE_MORE);
            break;

        // Identifiers
        case '_':
            res = lexer_get_next_token(self, err, STATE_UNDERSCORE);
            break;
        case 'a' ... 'z':
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
            break;
        case 'A' ... 'Z':
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
            break;
        // Annotations
        case '@':
            res = lexer_get_next_token(self, err, STATE_ANNOTATION);
            break;
        // Literals
        case '"':
            res = lexer_get_next_token(self, err, STATE_STR_START);
            break;
        // Numeric
        case '1' ... '9':
            res = lexer_get_next_token(self, err, STATE_INT);
            break;
        case '0':
            res = lexer_get_next_token(self, err, STATE_ZERO);
            break;
        case '.':
            res = lexer_get_next_token(self, err, STATE_DOT);
            break;
        }
        break;
    // ANNOTATION
    case STATE_ANNOTATION:
        if (utils_peek_char(&internal_err, in, 0) == 'm' && utils_peek_char(&internal_err, in, 1) == 'a' &&
            utils_peek_char(&internal_err, in, 2) == 'i' && utils_peek_char(&internal_err, in, 3) == 'n')
        {
            for (int i = 0; i < 4; i++)
                utils_get_char(&internal_err, in);
            res.type = TOKEN_ANNOTATION;
        }
        else
            *err = INVALID_SYMBOL;
        break;
    // OPERATORS
    case STATE_NOT:
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_NOT_EQUAL;
        }
        else
            *err = INVALID_SYMBOL;
        break;
    case STATE_EQUAL:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_ASSIGN;
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL;
        }
        break;
    case STATE_MORE:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_GREATER;
        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL_GREATER;
        }
        break;
    case STATE_LESS:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_LESS;

        if (utils_peek_char(&internal_err, in, 0) == '=')
        {
            utils_get_char(&internal_err, in);
            res.attribute_index = OP_EQUAL_LESS;
        }
        break;
    case STATE_DIVIDE:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_DIVISION;
        c = utils_peek_char(&internal_err, in, 0);
        if (c == '/')
        {
            utils_get_char(&internal_err, in);
            res = lexer_get_next_token(self, err, STATE_S_COMMENT);
        }
        else if (c == '*')
        {
            utils_get_char(&internal_err, in);
            res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        break;
    // COMMENTS
    case STATE_S_COMMENT:
        break;
    case STATE_M_COMMENT:
        break;
    // IDENTIFIERS
    case STATE_UNDERSCORE:
        c = utils_peek_char(&internal_err, in, 0);
        if (c == '_' || isalnum(c)) // todo: check if isalnum is correct
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
        else
        {
            utils_get_char(&internal_err, in);
            res.type = TOKEN_UNDERSCORE;
        }
        break;
    case STATE_IDENTIFIER:
        break;
    // LITERALS
    case STATE_STR_START:
        c = utils_get_char(&internal_err, in);
        if (c == '"')
        {
            res.type = TOKEN_STRING;
            res.attribute_index = attribute_table_insert_string(self->attr_table, err, "");
        }
        else if (c == '\\')
            res = lexer_get_next_token(self, err, STATE_ESCAPE);
        else if (c == '\n' || c == '\0')
            *err = UNFINISHED_STRING;
        else
            res = lexer_get_next_token(self, err, STATE_STR);
        break;
    case STATE_END_OR_MULTIPLE:
        break;
    case STATE_ESCAPE:
        c = utils_get_char(&internal_err, in);
        switch (c)
        {
        case '\\':
        case 'n':
        case 't':
        case 'r':
        case '"':
        default:
            *err = INVALID_ESCAPE_SEQUENCE;
            break;
        }
        break;
    case STATE_STR:
        break;
    }
    return res;
}