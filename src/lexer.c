// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"
#include "utils.h"

#include <token.h>

token lexer_get_next_token(error *err, lexer *l)
{
    token res = {TOKEN_UNDEFINED, -1, 0};
    error internal_err = NONE;

    // ---- FSM implementation ----
    char c = utils_peek_char(&internal_err, l->input, 0);
    switch (c)
    {
    // Operators
    case '.':
        res.type = TOKEN_DOT;
        utils_get_char(&internal_err, l->input);
        break;
    case ',':
        res.type = TOKEN_COMMA;
        utils_get_char(&internal_err, l->input);
        break;
    case '_':
        res.type = TOKEN_UNDERSCORE;
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

    default:
        *err = GENERAL_LEXICAL;
        break;
    }
    if (internal_err != NONE && *err == NONE)
    {
        *err = internal_err;
    }
    return res;
}
