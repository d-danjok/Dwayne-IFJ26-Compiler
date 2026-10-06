// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"
#include "utils.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// notes: integer followed by dot(1.) -> becomes int(1) and dot(.)
//  TODO: rewrite get_next_token to use loop instead of recursion to avoid stack overflow

// ------ HELPERS -------

// @brief checks if internal error is set and sets external error if so
// @param err - pointer to external error
// @param internal_err - internal error
// @return true if no error, false otherwise
static bool lexer_check_error(error *err, error internal_err)
{
    if (internal_err != NONE && internal_err != INTERNAL_END_OF_FILE)
        *err = internal_err;
    return *err == NONE;
}

// @brief helper that inserts string from buffer into attribute table and returns its index
// @param self - pointer to lexer structure
// @param err - pointer to external error
// @return index of inserted string in attribute table, -1 if error occurred
static int lexer_insert_string(lexer *self, error *err)
{
    if (*err != NONE)
        return -1;
    size_t size = strlen(self->buffer) + 1;
    char *string = utils_safe_malloc(err, size);
    if (string == NULL)
        return -1;
    memcpy(string, self->buffer, size);
    int index = attribute_table_insert_string(self->attr_table, err, &string);
    free(string);
    return index;
}

// @brief helper that inserts identifier from buffer into attribute table and returns its index
// @param self - pointer to lexer structure
// @param err - pointer to external error
// @return index of inserted identifier in attribute table, -1 if error occurred
static int lexer_insert_identifier(lexer *self, error *err)
{
    if (*err != NONE)
        return -1;
    size_t size = strlen(self->buffer) + 1;
    char *string = utils_safe_malloc(err, size);
    if (string == NULL)
        return -1;
    memcpy(string, self->buffer, size);
    int index = attribute_table_insert_identifier(self->attr_table, err, &string);
    free(string);
    return index;
}

// @brief wrapper for utils_get_char which checks for internal errors and sets external error if so
static char lexer_get_char(error *err, error *internal_err, FILE *in, size_t *line_ptr)
{
    if (*err != NONE)
        return '\0';
    char c = utils_get_char(internal_err, in);
    if (c == '\n' && line_ptr != NULL)
    {
        if (*line_ptr == SIZE_MAX)
            *internal_err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        else
            ++(*line_ptr);
    }
    lexer_check_error(err, *internal_err);
    return c;
}
// @brief wrapper for utils_peek_char which checks for internal errors and sets external error if so
static char lexer_peek_char(error *err, error *internal_err, FILE *in, int offset)
{
    if (*err != NONE)
        return '\0';
    char c = utils_peek_char(internal_err, in, offset);
    lexer_check_error(err, *internal_err);
    return c;
}

// @brief Reserves two writable bytes at the current buffer index(expanding the buffer if necessary)
//
// @param self Its buffer, index(must not exceed the allocation size), and capacity must be initialized.
//             The buffer must be NULL or realloc-compatible.
//             Zero capacity forces allocation before the next write.
// @param err The pointed-to error must be NONE before the call.
//            Receives INTERNAL_MEMORY_ALLOCATION on failure.
// @return true if two bytes fit, false on allocation failure or size overflow.
// @note On failure, the existing allocation remains valid.
static bool lexer_expand_buffer(lexer *self, error *err)
{
    if (self->buffer_index > SIZE_MAX - 2) // drop if we are too close to the limit of size_t
    {
        *err = INTERNAL_MEMORY_ALLOCATION;
        return false;
    }
    size_t required = self->buffer_index + 2;
    if (self->buffer == NULL) // If the buffer is NULL, we need to allocate it.
        self->buffer_capacity = 0;
    if (required <= self->buffer_capacity) // Already enough space.
        return true;

    size_t capacity = self->buffer_capacity ? self->buffer_capacity : BUFFER_SIZE;
    while (capacity < required) // Double the capacity until it is enough, but avoid overflow.
    {
        if (capacity > SIZE_MAX / 2)
        {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    char *buffer = realloc(self->buffer, capacity);
    if (buffer == NULL)
    {
        *err = INTERNAL_MEMORY_ALLOCATION;
        return false;
    }
    self->buffer = buffer;
    self->buffer_capacity = capacity;
    return true;
}

token lexer_get_next_token(lexer *self, error *err, state current_state)
{
    FILE *in = self->input;
    error internal_err = NONE;
    size_t *line = self->current_line;
    size_t *strt_line = line;
    char c;
    token res = {TOKEN_UNDEFINED, -1, 0};
    if (*err != NONE)
        return res;
    if (current_state == STATE_START)
        self->buffer_index = 0;
    if (!lexer_expand_buffer(self, err))
        return res;

    // ---- FSM implementation ----
    switch (current_state)
    {
    case STATE_START:
        c = lexer_get_char(err, &internal_err, in, line);
        switch (c)
        {
        case ' ':
            res = lexer_get_next_token(self, err, STATE_START);
            break;
        // EOL, EOF
        case '\n':
            res.type = TOKEN_EOL;
            break;
        case '\0':
            if (internal_err == INTERNAL_END_OF_FILE)
                res.type = TOKEN_EOF;
            else
                *err = INVALID_SYMBOL;
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
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_UNDERSCORE);
            break;
        case 'a' ... 'z':
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
            break;
        case 'A' ... 'Z':
            self->buffer[self->buffer_index++] = c;
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
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_INT);
            break;
        case '0':
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_ZERO);
            break;
        case '.':
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_DOT);
            break;
        default:
            *err = INVALID_SYMBOL;
        }
        res.line = strt_line != NULL ? *strt_line : 0;
        break;
    // ANNOTATION
    case STATE_ANNOTATION:
        if (lexer_peek_char(err, &internal_err, in, 0) == 'm' && lexer_peek_char(err, &internal_err, in, 1) == 'a' &&
            lexer_peek_char(err, &internal_err, in, 2) == 'i' && lexer_peek_char(err, &internal_err, in, 3) == 'n')
        {
            for (int i = 0; i < 4; i++)
                lexer_get_char(err, &internal_err, in, line);
            res.type = TOKEN_ANNOTATION;
        }
        else
            *err = INVALID_SYMBOL;
        break;
    // OPERATORS
    case STATE_NOT:
        if (lexer_peek_char(err, &internal_err, in, 0) == '=')
        {
            lexer_get_char(err, &internal_err, in, line);
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_NOT_EQUAL;
        }
        else
            *err = INVALID_SYMBOL;
        break;
    case STATE_EQUAL:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_ASSIGN;
        if (lexer_peek_char(err, &internal_err, in, 0) == '=')
        {
            lexer_get_char(err, &internal_err, in, line);
            res.attribute_index = OP_EQUAL;
        }
        break;
    case STATE_MORE:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_GREATER;
        if (lexer_peek_char(err, &internal_err, in, 0) == '=')
        {
            lexer_get_char(err, &internal_err, in, line);
            res.attribute_index = OP_EQUAL_GREATER;
        }
        break;
    case STATE_LESS:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_LESS;
        if (lexer_peek_char(err, &internal_err, in, 0) == '=')
        {
            lexer_get_char(err, &internal_err, in, line);
            res.attribute_index = OP_EQUAL_LESS;
        }
        break;
    case STATE_DIVIDE:
        res.type = TOKEN_OPERATOR;
        res.attribute_index = OP_DIVISION;
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '/')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_S_COMMENT);
        }
        else if (c == '*')
        {
            lexer_get_char(err, &internal_err, in, line);
            self->coment_depth = 1;
            self->comment_sline = true;
            res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        break;
    // COMMENTS
    case STATE_S_COMMENT:
        c = lexer_get_char(err, &internal_err, in, line);
        if (c == '\n')
            res.type = TOKEN_EOL;
        else if (internal_err == INTERNAL_END_OF_FILE)
            res.type = TOKEN_EOF;
        else
            res = lexer_get_next_token(self, err, STATE_S_COMMENT);
        break;
    case STATE_M_COMMENT:
        c = lexer_get_char(err, &internal_err, in, line);
        if (internal_err == INTERNAL_END_OF_FILE)
            *err = UNFINISHED_COMMENT;
        else if (c == '\n')
        {
            self->comment_sline = false;
            res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        else if (c == '/' && lexer_peek_char(err, &internal_err, in, 0) == '*')
        {
            lexer_get_char(err, &internal_err, in, line);
            self->coment_depth++;
            res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        else if (c == '*' && lexer_peek_char(err, &internal_err, in, 0) == '/')
        {
            lexer_get_char(err, &internal_err, in, line);
            self->coment_depth--;
            if (self->coment_depth == 0)
            {

                if (self->comment_sline)
                    res = lexer_get_next_token(self, err, STATE_START);
                else
                    res = lexer_get_next_token(self, err, STATE_WAITING_EOL);
            }
            else
                res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        else
        {
            res = lexer_get_next_token(self, err, STATE_M_COMMENT);
        }
        break;
    case STATE_WAITING_EOL:
        c = lexer_get_char(err, &internal_err, in, line);
        if (c == '\n')
            res.type = TOKEN_EOL;
        else if (internal_err == INTERNAL_END_OF_FILE)
            res.type = TOKEN_EOF;
        else if (c == ' ')
            res = lexer_get_next_token(self, err, STATE_WAITING_EOL);
        else
            *err = INVALID_SYMBOL;
        break;
    // IDENTIFIERS
    case STATE_UNDERSCORE:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '_' || isalnum(c)) // todo: check if isalnum is correct
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
        else
        {
            res.type = TOKEN_UNDERSCORE;
        }
        break;
    case STATE_IDENTIFIER:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '_' || isalnum(c))
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_IDENTIFIER);
        }
        else
        {
            self->buffer[self->buffer_index++] = '\0';
            res.type = TOKEN_IDENTIFIER;
            res.attribute_index = lexer_insert_identifier(self, err);
        }
        break;
    // LITERALS
    case STATE_STR_START:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '"')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_END_OR_MULTIPLE);
        }
        else if (c == '\\')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_ESCAPE);
        }
        else if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
            *err = UNFINISHED_STRING;
        else
            res = lexer_get_next_token(self, err, STATE_STR);
        break;
    case STATE_END_OR_MULTIPLE:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '"')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_MULTIPLE_STR);
        }
        else
        {
            self->buffer[0] = '\0';
            res.type = TOKEN_STRING;
            res.attribute_index = lexer_insert_string(self, err);
        }
        break;
    case STATE_MULTIPLE_STR:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '"')
        {
            res = lexer_get_next_token(self, err, STATE_QUOTE_1);
        }
        else if (c == '\0')
        {
            *err = UNFINISHED_STRING;
        }
        else
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_MULTIPLE_STR);
        }
        break;
    case STATE_QUOTE_1:
        c = lexer_peek_char(err, &internal_err, in, 1);
        if (c == '"')
        {
            res = lexer_get_next_token(self, err, STATE_QUOTE_2);
        }
        else
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_MULTIPLE_STR);
        }
        break;
    case STATE_QUOTE_2:
        c = lexer_peek_char(err, &internal_err, in, 2);
        if (c == '"')
        {
            lexer_get_char(err, &internal_err, in, line);
            lexer_get_char(err, &internal_err, in, line);
            lexer_get_char(err, &internal_err, in, line);
            res.type = TOKEN_STRING;
            self->buffer[self->buffer_index++] = '\0';
            res.attribute_index = lexer_insert_string(self, err);
        }
        else
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_MULTIPLE_STR);
        }
        break;
    case STATE_ESCAPE:
        c = lexer_get_char(err, &internal_err, in, line);
        switch (c)
        {
        case '\\':
            self->buffer[self->buffer_index++] = '\\';
            res = lexer_get_next_token(self, err, STATE_STR);
            break;
        case 'n':
            self->buffer[self->buffer_index++] = '\n';
            res = lexer_get_next_token(self, err, STATE_STR);
            break;
        case 't':
            self->buffer[self->buffer_index++] = '\t';
            res = lexer_get_next_token(self, err, STATE_STR);
            break;
        case 'r':
            self->buffer[self->buffer_index++] = '\r';
            res = lexer_get_next_token(self, err, STATE_STR);
            break;
        case '"':
            self->buffer[self->buffer_index++] = '"';
            res = lexer_get_next_token(self, err, STATE_STR);
            break;
        default:
            *err = INVALID_ESCAPE_SEQUENCE;
            break;
        }
        break;
    case STATE_STR:
        c = lexer_get_char(err, &internal_err, in, line);
        if (c == '"')
        {
            res.type = TOKEN_STRING;
            self->buffer[self->buffer_index++] = '\0';
            res.attribute_index = lexer_insert_string(self, err);
        }
        else if (c == '\\')
        {
            res = lexer_get_next_token(self, err, STATE_ESCAPE);
        }
        else if (c == '\n' || c== '\0')
        {
            *err = UNFINISHED_STRING;
        }
        else
        {
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_STR);
        }
        break;
    // NUMERIC
    case STATE_INT:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
        {
            c = lexer_get_char(err, &internal_err, in, line);
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_INT);
        }
        else if (c == '_')
        {
            lexer_get_char(err, &internal_err, in, line); // eat underscore
            res = lexer_get_next_token(self, err, STATE_UINT);
        }
        else if (c == '.')
        {
            res = lexer_get_next_token(self, err, STATE_DOT_OR_DOUBLE);
        }
        else if (c == 'e' || c == 'E')
        {
            lexer_get_char(err, &internal_err, in, line); // eat e/E
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_EXP);
        }
        else
        {
            res.type = TOKEN_INT;
            self->buffer[self->buffer_index++] = '\0';
            int result = utils_string_to_int(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, result);
        }

        break;
    case STATE_UINT:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
            res = lexer_get_next_token(self, err, STATE_INT);
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_ZERO:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_ZERO);
        }
        else if (c == '.')
        {
            res = lexer_get_next_token(self, err, STATE_DOT_OR_DOUBLE);
        }
        else if (c == '_')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_UZERO);
        }
        else if (c == 'e' || c == 'E')
        {
            lexer_get_char(err, &internal_err, in, line);
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_EXP);
        }
        else
        {
            if (!lexer_check_error(err, internal_err))
                break;
            res.type = TOKEN_INT;
            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, 0);
        }
        break;
    case STATE_UZERO:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
            res = lexer_get_next_token(self, err, STATE_ZERO);
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_DOT:
        if (isdigit(lexer_peek_char(err, &internal_err, in, 0)))
        {
            res = lexer_get_next_token(self, err, STATE_DOUBLE);
        }
        else
        {
            res.type = TOKEN_DOT;
        }
        break;
    case STATE_DOT_OR_DOUBLE: // current char is dot
        if (isdigit(lexer_peek_char(err, &internal_err, in, 1)))
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_DOUBLE);
        }
        else
        {
            // process as integer and dot separately if there is no digit after dot
            res.type = TOKEN_INT;
            self->buffer[self->buffer_index++] = '\0';
            int result = utils_string_to_int(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, result);
        }
        break;

    case STATE_DOUBLE:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_DOUBLE);
        }
        else if (c == '_')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_UDOUBLE);
        }
        else if (c == 'e' || c == 'E')
        {
            lexer_get_char(err, &internal_err, in, line);
            self->buffer[self->buffer_index++] = c;
            res = lexer_get_next_token(self, err, STATE_EXP);
        }
        else if (c == 'd' || c == 'D')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_D_SUFF);
        }
        else
        {
            res.type = TOKEN_DOUBLE;
            self->buffer[self->buffer_index++] = '\0';
            double result = utils_string_to_double(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_double(self->attr_table, err, result);
        }
        break;
    case STATE_UDOUBLE:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
            res = lexer_get_next_token(self, err, STATE_DOUBLE);
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_EXP:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
        {
            res = lexer_get_next_token(self, err, STATE_EXP_ZERO);
        }
        else if (isdigit(c))
        {

            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_EXP_PART);
        }
        else if (c == '+' || c == '-')
        {
            res = lexer_get_next_token(self, err, STATE_EXP_SIGN);
        }
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_EXP_ZERO:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_EXP_ZERO);
        }
        else if (c == '_')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_UEXP_ZERO);
        }
        else if (c == 'd' || c == 'D')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_D_SUFF);
        }
        else
        {
            res.type = TOKEN_DOUBLE;
            self->buffer[self->buffer_index++] = '\0';
            double result = utils_string_to_double(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_double(self->attr_table, err, result);
        }
        break;
    case STATE_UEXP_ZERO:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
            res = lexer_get_next_token(self, err, STATE_EXP_ZERO);
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_EXP_SIGN:
        self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (c == '0')
        {
            res = lexer_get_next_token(self, err, STATE_EXP_ZERO);
        }
        else if (isdigit(c))
            res = lexer_get_next_token(self, err, STATE_EXP_PART);
        else
            *err = INVALID_SYMBOL;
        break;

    case STATE_EXP_PART:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
        {
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_EXP_PART);
        }
        else if (c == '_')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_UEXP_PART);
        }
        else if (c == 'd' || c == 'D')
        {
            lexer_get_char(err, &internal_err, in, line);
            res = lexer_get_next_token(self, err, STATE_D_SUFF);
        }
        else
        {
            res.type = TOKEN_DOUBLE;
            self->buffer[self->buffer_index++] = '\0';
            double result = utils_string_to_double(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_double(self->attr_table, err, result);
        }
        break;
    case STATE_UEXP_PART:
        c = lexer_peek_char(err, &internal_err, in, 0);
        if (isdigit(c))
        {
            res = lexer_get_next_token(self, err, STATE_EXP_PART);
        }
        else
            *err = INVALID_SYMBOL;
        break;
    case STATE_D_SUFF:
        res.type = TOKEN_DOUBLE;
        self->buffer[self->buffer_index++] = '\0';
        double result = utils_string_to_double(&internal_err, self->buffer);
        if (!lexer_check_error(err, internal_err))
            break;
        res.attribute_index = attribute_table_insert_double(self->attr_table, err, result);
        break;
    }

    if (!lexer_check_error(err, internal_err))
        res.type = TOKEN_UNDEFINED;
    return res;
}