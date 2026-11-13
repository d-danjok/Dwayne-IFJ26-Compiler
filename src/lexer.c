// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)
//
// @note    some fsm parts will be implemented by other team members

#include "lexer.h"
#include "utils.h"

#include <ctype.h>
#include <stdint.h>
#include <token.h>

// notes: integer followed by dot(1.) -> becomes int(1) and dot(.)(should throw error)
// TODO: fix indentation handling in braces()
// TODO: implement keywords

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
    free(string); // used free cause utils_safe_free(&string) sets string to NULL
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
    free(string); // used free cause utils_safe_free(&string) sets string to NULL
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
            *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
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

// @brief returns indentation width on top of the indent stack
// @param self - pointer to lexer structure
// @param err - pointer to external error (set if stack is empty)
// @return indentation width, 0 if error occurred
static int lexer_indent_top(lexer *self, error *err)
{
    error stack_err = NONE;
    int *top = stack_peek(self->indent_size_s, &stack_err, 0);
    if (top == NULL)
    {
        lexer_check_error(err, stack_err);
        return 0;
    }
    return *top;
}

// @brief pushes indentation width on the indent stack (allocated, stack takes ownership)
// @param self - pointer to lexer structure
// @param err - pointer to external error
// @param width - indentation width to push
// @return true on success, false otherwise
static bool lexer_indent_push(lexer *self, error *err, int width)
{
    error stack_err = NONE;
    int *data = utils_safe_malloc(&stack_err, sizeof(int));
    if (data == NULL)
    {
        *err = stack_err;
        return false;
    }
    *data = width;
    stack_push(self->indent_size_s, &stack_err, (void **)&data);
    if (stack_err != NONE)
    {
        free(data); // ownership was not transferred
        *err = stack_err;
        return false;
    }
    return true;
}

lexer *new_lexer(error *err, FILE *input, attribute_table *attr_table)
{
    if (input == NULL || attr_table == NULL || err == NULL)
    {
        if (err != NULL)
            *err = INTERNAL_MEMORY_ALLOCATION;
        return NULL;
    }

    lexer *self = utils_safe_malloc(err, sizeof(lexer));
    if (self == NULL)
        return NULL;

    self->indent_size_s = new_stack(err);
    if (self->indent_size_s == NULL)
    {
        UTILS_SAFE_FREE(self);
        return NULL;
    }
    if (!lexer_indent_push(self, err, 0)) // base indentation 0
    {
        stack_destruct(&self->indent_size_s);
        UTILS_SAFE_FREE(self);
        return NULL;
    }
    self->current_state = STATE_START;
    self->input = input;
    self->attr_table = attr_table;
    self->buffer = NULL;
    self->buffer_index = 0;
    self->buffer_capacity = 0;
    self->coment_depth = 0;
    self->comment_sline = false;
    self->indent_cnt = 0;
    self->last_was_eol = false;
    self->last_was_eof = false;
    self->empty_line = true;
    self->parentaces = 0;
    self->m_comment = false;
    self->current_line = 1;

    return self;
}

void lexer_destruct(lexer **self)
{
    if (self == NULL || *self == NULL)
        return;
    free((*self)->buffer);
    stack_destruct(&(*self)->indent_size_s);
    UTILS_SAFE_FREE(self); // input stream and attr_table are not owned by lexer
}

token lexer_get_next_token(lexer *self, error *err)
{
    FILE *in = self->input;
    error internal_err = NONE;
    size_t *line = &self->current_line;
    size_t *strt_line = line;
    char c;
    token res = {TOKEN_UNDEFINED, -1, 0};

    while (*err == NONE && res.type == TOKEN_UNDEFINED)
    {
        if (self->current_state == STATE_START)
            self->buffer_index = 0;
        if (!lexer_expand_buffer(self, err))
            return res;

        // ---- FSM implementation ----
        switch (self->current_state)
        {

        case STATE_EOL: {
            char c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == ' ')
            {
                lexer_get_char(err, &internal_err, in, line);
                if (!self->m_comment)
                    self->indent_cnt++;
                self->current_state = STATE_EOL;
            }
            else if (c == '\n')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->indent_cnt = 0;
                self->m_comment = false;
                self->current_state = STATE_EOL;
            }
            else if (c == '/')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_INDENT_DIV;
            }
            else if (c == '\0' && internal_err == INTERNAL_END_OF_FILE)
            {
                self->indent_cnt = 0;
                self->m_comment = false;
                self->last_was_eol = true;
                self->last_was_eof = true;
                self->current_state = STATE_START;
            }
            else
            {
                self->last_was_eol = true;
                self->current_state = STATE_START;
            }
        }
        break;
        case STATE_INDENT_DIV: {
            char c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '/')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->indent_cnt = 0;
                self->m_comment = false;
                self->current_state = STATE_S_COMMENT;
            }
            else if (c == '*')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->coment_depth = 1;
                self->comment_sline = true;
                self->m_comment = true;
                self->current_state = STATE_M_COMMENT;
            }
            else
            {
                *err = UNFINISHED_COMMENT;
                //@note: very uncertain about this
                // todo: drop dedent and div to parser to throw error
            }
        }
        break;
        case STATE_START: {
            if (self->last_was_eol)
            {
                self->last_was_eol = false;
                if (!self->empty_line)
                {
                    res.type = TOKEN_EOL;
                    res.line = self->current_line;
                    return res;
                }
            }
            if (self->indent_cnt > lexer_indent_top(self, err) && self->parentaces == 0)
            {
                res.type = TOKEN_INDENT;
                res.line = self->current_line;
                lexer_indent_push(self, err, self->indent_cnt);
                return res;
            }
            else if (self->indent_cnt < lexer_indent_top(self, err) && self->parentaces == 0)
            {

                error stack_err = NONE;
                int *ptr = stack_pop(self->indent_size_s, &stack_err);
                if (stack_err != NONE)
                {
                    lexer_check_error(err, stack_err);
                    return res;
                }
                free(ptr);
                if (self->indent_cnt > lexer_indent_top(self, err))
                {
                    *err = INVALID_INDENTATION;
                    return res;
                }
                res.type = TOKEN_DEDENT;
                res.line = self->current_line;
                return res;
            }
            if (self->last_was_eof)
            {
                res.type = TOKEN_EOF;
                res.line = self->current_line;
                return res;
            }
            c = lexer_get_char(err, &internal_err, in, line);
            if (c != '\0' && c != '\n' && c != ' ' && c != '/')
            {
                self->empty_line = false;
            }
            switch (c)
            {
            case ' ':
                break;
            // EOL, EOF
            case '\n':
                if (self->parentaces != 0)
                    self->indent_cnt = 0;
                self->m_comment = false;
                self->current_state = STATE_EOL; // could throw errpr on negative par

                break;
            case '\0':
                if (internal_err == INTERNAL_END_OF_FILE)
                {
                    self->indent_cnt = 0;
                    self->m_comment = false;
                    self->last_was_eof = true;
                    self->last_was_eol = true;
                }

                else
                    *err = INVALID_SYMBOL;
                break;

            // Simple tokens
            case '|':
                self->current_state = STATE_OR;
                break;
            case ',':
                res.type = TOKEN_COMMA;
                break;
            case ':':
                res.type = TOKEN_COLON;
                break;
            case ';':
                res.type = TOKEN_SEMICOLON;
                break;
            case '(':
                res.type = TOKEN_OPEN_PAREN;
                self->parentaces++;
                break;
            case ')':
                res.type = TOKEN_CLOSE_PAREN;
                self->parentaces--;
                break;
            // Operators
            case '+':
                self->current_state = STATE_PLUS;
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
                self->current_state = STATE_DIVIDE;
                break;
            case '=':
                self->current_state = STATE_EQUAL;
                break;
            case '!':
                self->current_state = STATE_NOT;
                break;
            case '<':
                self->current_state = STATE_LESS;
                break;
            case '>':
                self->current_state = STATE_MORE;
                break;

            // Identifiers
            case '_':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_UNDERSCORE;
                break;
            case 'a' ... 'z':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_IDENTIFIER;
                break;
            case 'A' ... 'Z':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_IDENTIFIER;
                break;
            // Annotations
            case '@':
                self->current_state = STATE_ANNOTATION;
                break;
            // Literals
            case '"':
                self->current_state = STATE_STR_START;
                break;
            // Numeric
            case '1' ... '9':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_INT;
                break;
            case '0':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_ZERO;
                break;
            case '.':
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_DOT;
                break;
            default:
                *err = INVALID_SYMBOL;
            }
            res.line = self->current_line;
        }
        break;
        // ANNOTATION
        case STATE_ANNOTATION: {
            if (lexer_get_char(err, &internal_err, in, line) == 'm' &&
                lexer_get_char(err, &internal_err, in, line) == 'a' &&
                lexer_get_char(err, &internal_err, in, line) == 'i' &&
                lexer_get_char(err, &internal_err, in, line) == 'n' &&
                (lexer_peek_char(err, &internal_err, in, 0) == ' ' ||
                 lexer_peek_char(err, &internal_err, in, 0) == '\n' ||
                 lexer_peek_char(err, &internal_err, in, 0) == '\t' ||
                 lexer_peek_char(err, &internal_err, in, 0) == '\0'))
                res.type = TOKEN_ANNOTATION;
            else
                *err = INVALID_SYMBOL;
        }
        break;
        // OPERATORS
        case STATE_NOT: {
            res.type = TOKEN_OPERATOR;
            if (lexer_peek_char(err, &internal_err, in, 0) == '=')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_NOT_EQUAL;
            }
            else
                res.attribute_index = OP_NOT;
        }
        break;
        case STATE_EQUAL: {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_ASSIGN;
            char c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '=')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_EQUAL;
            }
            else if (c == '>')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_ARROW;
            }
        }
        break;
        case STATE_MORE: {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_GREATER;
            if (lexer_peek_char(err, &internal_err, in, 0) == '=')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_EQUAL_GREATER;
            }
        }
            break;
        case STATE_LESS: {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_LESS;
            char c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '=')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_EQUAL_LESS;
            }
            else if (c == '-')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_LEFT_ARROW;
            }
        }
        break;
        case STATE_DIVIDE: {
        
            char c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '/')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_S_COMMENT;
            }
            else if (c == '*')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->coment_depth = 1;
                self->comment_sline = true;
                self->current_state = STATE_M_COMMENT;
            }
            else
            {
                res.type = TOKEN_OPERATOR;
                res.attribute_index = OP_DIVISION;
            }
            }
            break;
        case STATE_OR:
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_SEPARATOR;
            if (lexer_peek_char(err, &internal_err, in, 0) == '|')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_OR;
            }
            break;
        case STATE_AND:
            if (lexer_peek_char(err, &internal_err, in, 0) == '&')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.type = TOKEN_OPERATOR;
                res.attribute_index = OP_AND;
            }
            else
            {
                *err = INVALID_SYMBOL;
            }
            break;
        case STATE_PLUS:
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_PLUS;
            if (lexer_peek_char(err, &internal_err, in, 0) == '+')
            {
                lexer_get_char(err, &internal_err, in, line);
                res.attribute_index = OP_CONCAT;
            }
            break;
        // COMMENTS
        case STATE_S_COMMENT:
            c = lexer_get_char(err, &internal_err, in, line);
            if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
            {
                self->indent_cnt = 0;
                if (self->parentaces == 0)
                {
                    if (self->m_comment)
                        self->indent_cnt = 0;
                    self->current_state = STATE_EOL;
                }
                else
                {
                    self->current_state = STATE_START;
                }
                self->m_comment = false;
            }
            else
                self->current_state = STATE_S_COMMENT;
            break;
        case STATE_M_COMMENT:
            c = lexer_get_char(err, &internal_err, in, line);
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_COMMENT;
            else if (c == '\n')
            {
                self->comment_sline = false;
                self->indent_cnt = 0;
                self->m_comment = false;
                self->current_state = STATE_M_COMMENT;
            }
            else if (c == '/' && lexer_peek_char(err, &internal_err, in, 0) == '*')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->coment_depth++;
                self->current_state = STATE_M_COMMENT;
            }
            else if (c == '*' && lexer_peek_char(err, &internal_err, in, 0) == '/')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->coment_depth--;
                if (self->coment_depth == 0)
                {

                    if (self->comment_sline)
                    {

                        if (self->m_comment)
                            self->current_state = STATE_EOL;
                        else
                            self->current_state = STATE_START;
                    }
                    else
                        self->current_state = STATE_WAITING_EOL;
                }
                else
                    self->current_state = STATE_M_COMMENT;
            }
            else
            {
                self->current_state = STATE_M_COMMENT;
            }
            break;
        case STATE_WAITING_EOL:
            c = lexer_get_char(err, &internal_err, in, line);
            if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
            {

                self->indent_cnt = 0;
                self->m_comment = false;
                self->current_state = STATE_EOL;
            }
            else if (c == ' ')
                self->current_state = STATE_WAITING_EOL;
            else
                *err = INVALID_SYMBOL;
            break;
        // IDENTIFIERS
        case STATE_UNDERSCORE:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '_' || isalnum(c)) // todo: check if isalnum is correct
                self->current_state = STATE_IDENTIFIER;
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
                self->current_state = STATE_IDENTIFIER;
            }
            else
            {
                self->buffer[self->buffer_index++] = '\0';
                char *keyword_str[] = {"def",    "do",        "Double", "else", "if",    "import", "Int",     "return",
                                       "String", "then",      "Unit",   "val",  "var",   "while",  "Boolean", "case",
                                       "class",  "extension", "for",    "List", "match", "type"};

                for (int i = 0; i < (int)(sizeof(keyword_str) / sizeof(char *)); i++)
                {
                    if (strcmp(self->buffer, keyword_str[i]) == 0)
                    {
                        res.type = TOKEN_KEYWORD;
                        res.attribute_index = i;
                        break;
                    }
                }
                if (res.type != TOKEN_KEYWORD)
                {
                    res.type = TOKEN_IDENTIFIER;
                    res.attribute_index = lexer_insert_identifier(self, err);
                }
            }
            break;
        // LITERALS
        case STATE_STR_START:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '"')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_STR_END_OR_MULTILN;
            }
            else if (c == '\\')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_STR_ESCAPE;
            }
            else if (c != '\n' && internal_err == NONE)
                self->current_state = STATE_STR;
            else
                *err = UNFINISHED_STRING;
            break;
        case STATE_STR_END_OR_MULTILN:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '"')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_STR_MULTILN;
            }
            else
            {
                self->buffer[0] = '\0';
                res.type = TOKEN_STRING;
                res.attribute_index = lexer_insert_string(self, err);
            }
            break;
        case STATE_STR_MULTILN:
            c = lexer_get_char(err, &internal_err, in, 0);
            if (c == '"')
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR_MQ1;
            }
            else if (c != '\0')
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR_MULTILN;
            }
            else
            {
                *err = UNFINISHED_STRING;
            }
            break;
        case STATE_STR_MQ1:
            c = lexer_get_char(err, &internal_err, in, line);
            if (c == '"')
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR_MQ2;
            }
            else if (c != '\0')
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR_MULTILN;
            }
            else
            {
                *err = UNFINISHED_STRING;
            }
            break;
        case STATE_STR_MQ2:
            c = lexer_get_char(err, &internal_err, in, line);
            if (c == '"')
            {
                self->buffer[self->buffer_index-2] = '\0';
                self->current_state = STATE_STR_END;
            }
            else if (c != '\0')
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR_MULTILN;
            }
            else
            {
                *err = UNFINISHED_STRING;
            }
            break;
        case STATE_STR_ESCAPE:
            c = lexer_get_char(err, &internal_err, in, line);
            switch (c)
            {
            case '\\':
                self->buffer[self->buffer_index++] = '\\';
                self->current_state = STATE_STR;
                break;
            case 'n':
                self->buffer[self->buffer_index++] = '\n';
                self->current_state = STATE_STR;
                break;
            case 't':
                self->buffer[self->buffer_index++] = '\t';
                self->current_state = STATE_STR;
                break;
            case 'r':
                self->buffer[self->buffer_index++] = '\r';
                self->current_state = STATE_STR;
                break;
            case '"':
                self->buffer[self->buffer_index++] = '"';
                self->current_state = STATE_STR;
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
                self->buffer[self->buffer_index++] = '\0';
                self->current_state = STATE_STR_END;
            }
            else if (c == '\\')
            {
                self->current_state = STATE_STR_ESCAPE;
            }
            else if (c == '\n' || c == '\0')
            {
                *err = UNFINISHED_STRING;
            }
            else
            {
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_STR;
            }
            break;
        case STATE_STR_END:
                res.type = TOKEN_STRING;
                res.attribute_index = lexer_insert_string(self, err);
            break;
        // NUMERIC
        case STATE_INT:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_INT;
            }
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line); // eat underscore
                self->current_state = STATE_UINT;
            }
            else if (c == '.')
            {
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == 'e' || c == 'E')
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line); // eat e/E
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_D_SUFF;
            }
            else if (isalpha(c))
                *err = INVALID_SYMBOL; // 12abc, 0x1F, 1L
            else
                self->current_state = STATE_PROCESS_INT; // c stays unread, it starts the next token
            break;
        case STATE_UINT:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
                self->current_state = STATE_INT;
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line); // eat underscore
                self->current_state = STATE_UINT;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_ZERO:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '0')
            {
                self->buffer[0] = lexer_get_char(err, &internal_err, in, line);
                self->buffer_index = 1;
                self->current_state = STATE_ZERO;
            }
            else if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_DOUBLE_START;
            }
            else if (c == '.')
            {
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_UZERO;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_D_SUFF;
            }
            else if (isalpha(c))
                *err = INVALID_SYMBOL; // 12abc, 0x1F, 1L
            else
                self->current_state = STATE_PROCESS_INT; // c stays unread, it starts the next token
            break;
        case STATE_UZERO:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '0')
                self->current_state = STATE_ZERO;
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line); // eat underscore
                self->current_state = STATE_UZERO;
            }
            else
                *err = INVALID_SYMBOL;
            break;
        case STATE_PROCESS_INT: {
            res.type = TOKEN_INT;
            self->buffer[self->buffer_index++] = '\0';
            int result = utils_string_to_int(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, result);
        }
        break;
        case STATE_DOUBLE_START:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_DOUBLE_START;
            }
            else if (c == '.' && isdigit(lexer_peek_char(err, &internal_err, in, 1))) // 01.foo stays an error
            {
                // self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_UDOUBLE_START;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D') // #uncertainty: should we allow d/D suffix?
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_D_SUFF;
            }
            else
            {
                *err = INVALID_SYMBOL;
            }
            break;
        case STATE_UDOUBLE_START:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
                self->current_state = STATE_DOUBLE_START;
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line); // eat underscore
                self->current_state = STATE_UDOUBLE_START;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_DOT:
            if (isdigit(lexer_peek_char(err, &internal_err, in, 0)))
            {
                self->current_state = STATE_DOUBLE;
            }
            else
            {
                res.type = TOKEN_DOT;
            }
            break;
        case STATE_DOT_DOUBLE: // current char is dot
            c = lexer_peek_char(err, &internal_err, in, 1);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_DOUBLE;
            }            
            else
            {
                // #uncertainty: error or int and dot
                // *err = INVALID_SYMBOL;
                // process as integer and dot separately if there is no digit after dot
                self->current_state = STATE_PROCESS_INT; // process as integer and dot separately
            }
            break;

        case STATE_DOUBLE:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_UDOUBLE;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->buffer[self->buffer_index++] = c;
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_D_SUFF;
            }
            else
                self->current_state = STATE_D_SUFF; // no suffix, same end-of-literal check
            break;
        case STATE_UDOUBLE:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
                self->current_state = STATE_DOUBLE;
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line); // eat underscore
                self->current_state = STATE_UDOUBLE;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_EXP:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_EXP_PART;
            }
            else if (c == '+' || c == '-')
            {
                self->current_state = STATE_EXP_SIGN;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_EXP_SIGN:
            self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
                self->current_state = STATE_EXP_PART;
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_EXP_PART:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
            {
                self->buffer[self->buffer_index++] = lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_EXP_PART;
            }
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_UEXP_PART;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_D_SUFF;
            }
            else
                self->current_state = STATE_D_SUFF; // no suffix, same end-of-literal check
            break;
        case STATE_UEXP_PART:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (isdigit(c))
                self->current_state = STATE_EXP_PART;
            else if (c == '_')
            {
                lexer_get_char(err, &internal_err, in, line);
                self->current_state = STATE_UEXP_PART;
            }
            else
                *err = INVALID_SYMBOL;
            break;
        case STATE_D_SUFF:
            c = lexer_peek_char(err, &internal_err, in, 0);
            if (c == '_' || isalnum(c) || c == '.')
                *err = INVALID_SYMBOL; // 1.5x, 1.5Dx, 2e0.01, 1.5.5
            else
                self->current_state = STATE_PROCESS_DOUBLE; // c stays unread, it starts the next token
            break;
        case STATE_PROCESS_DOUBLE:
            res.type = TOKEN_DOUBLE;
            self->buffer[self->buffer_index++] = '\0';
            double result = utils_string_to_double(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_double(self->attr_table, err, result);
            break;
        }

        if (!lexer_check_error(err, internal_err))
        {
            res.type = TOKEN_UNDEFINED;
            return res;
        }
    }
    self->current_state = STATE_START; // next call starts fresh
    return res;
}
