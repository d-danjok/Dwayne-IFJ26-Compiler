// @file lexer.c
//
// @author 	Maksym Sereda (xseredmd00)

#include "lexer.h"

// --------------------------------------===HELPERS===--------------------------------------

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
// --------------------------------------===BUFFER===--------------------------------------

// Buffer layout:
//   [0, buffer_index)                              - buffer for storing complex tokens
//   [buffer_index, buffer_index + buffer_peeked)   - chars read from input but not consumed yet
// EOF is never stored: after EOF, utils_get_char keeps reporting INTERNAL_END_OF_FILE, so reading again is enough.

// @brief Reserves two writable bytes after the token and lookahead parts (expanding the buffer if necessary)
// @param self Its buffer, index(must not exceed the allocation size), and capacity must be initialized.
//             The buffer must be NULL or realloc-compatible.
//             Zero capacity forces allocation before the next write.
// @param err The pointed-to error must be NONE before the call.
//            Receives INTERNAL_MEMORY_ALLOCATION on failure.
// @return true if two bytes fit, false on allocation failure or size overflow.
// @note On failure, the existing allocation remains valid.
static bool lexer_expand_buffer(lexer *self, error *err)
{
    if (self->buffer_index > SIZE_MAX - 2 - self->buffer_peeked) // drop if we are too close to the limit of size_t
    {
        *err = INTERNAL_MEMORY_ALLOCATION;
        return false;
    }
    size_t required = self->buffer_index + self->buffer_peeked + 2;
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

// @brief appends char that is not taken from input to buffer
// @return false(with err) on allocation failure
static bool lexer_buffer_push(lexer *self, error *err, char c)
{
    if (!lexer_expand_buffer(self, err))
        return false;
    memmove(self->buffer + self->buffer_index + 1, self->buffer + self->buffer_index, self->buffer_peeked);
    self->buffer[self->buffer_index++] = c;
    return true;
}

// @brief reset token buffer while keeping lookahead
static void lexer_buffer_reset(lexer *self)
{
    if (self->buffer_index == 0)
        return;
    memmove(self->buffer, self->buffer + self->buffer_index, self->buffer_peeked);
    self->buffer_index = 0;
}
// --------------------------------------===PEEK_LOGIC===--------------------------------------

// @brief consumes next char and writes it to buffer if KEEP
// @param keep - true: char becomes part of the token, false: char is dropped
// @param internal_err - INTERNAL_END_OF_FILE at end of input and further, NONE otherwise
// @return consumed char, '\0' at EOF or error
static char lexer_get_char(lexer *self, error *err, error *internal_err, bool keep)
{
    if (*err != NONE)
        return '\0';
    char c;
    if (self->buffer_peeked > 0) // char already sits at buffer[buffer_index]
    {
        *internal_err = NONE;
        c = self->buffer[self->buffer_index];
        self->buffer_peeked--;
        if (!keep) // close the gap
            memmove(self->buffer + self->buffer_index, self->buffer + self->buffer_index + 1, self->buffer_peeked);
    }
    else
    {
        c = utils_get_char(internal_err, self->input);
        if (!lexer_check_error(err, *internal_err) || *internal_err == INTERNAL_END_OF_FILE)
            return '\0';
        if (keep)
        {
            if (!lexer_expand_buffer(self, err))
                return '\0';
            self->buffer[self->buffer_index] = c;
        }
    }
    if (keep)
        self->buffer_index++;
    if (c == '\n')
    {
        if (self->current_line == SIZE_MAX)
            *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        else
            self->current_line++;
    }
    return c;
}

// @brief returns char at given offset ahead (0 = next char) without consuming it;
// @param internal_err - INTERNAL_END_OF_FILE at end of input and further, NONE otherwise
// @return peeked char, '\0' at EOF or error
static char lexer_peek_char(lexer *self, error *err, error *internal_err, size_t offset)
{
    if (*err != NONE)
        return '\0';
    *internal_err = NONE;
    while (self->buffer_peeked <= offset)
    {
        char c = utils_get_char(internal_err, self->input);
        if (!lexer_check_error(err, *internal_err) || *internal_err == INTERNAL_END_OF_FILE)
            return '\0';
        if (!lexer_expand_buffer(self, err))
            return '\0';
        self->buffer[self->buffer_index + self->buffer_peeked++] = c;
    }
    return self->buffer[self->buffer_index + offset];
}

// --------------------------------------===INDENTATION_STACK===--------------------------------------

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
static bool lexer_indent_push(lexer *self, error *err)
{
    error stack_err = NONE;
    int *data = utils_safe_malloc(&stack_err, sizeof(int));
    if (data == NULL)
    {
        *err = stack_err;
        return false;
    }
    *data = self->indent_cnt;
    stack_push(self->indent_size_s, &stack_err, (void **)&data);
    if (stack_err != NONE)
    {
        free(data); // ownership was not transferred
        *err = stack_err;
        return false;
    }
    return true;
}
// --------------------------------------===LEXER===--------------------------------------

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
    self->indent_cnt = 0;
    if (!lexer_indent_push(self, err)) // base indentation 0
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
    self->buffer_peeked = 0;
    self->Pdepth = 0;
    self->Cdepth = 0;
    self->CSline = true;
    self->NL = false;
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
    error internal_err = NONE;
    char c;
    token res = {TOKEN_UNDEFINED, -1, 0};

    while (*err == NONE && res.type == TOKEN_UNDEFINED)
    {
        if (self->current_state == STATE_START)
            lexer_buffer_reset(self);
        if (!lexer_expand_buffer(self, err))
            return res;

        // ---- FSM implementation ----
        switch (self->current_state)
        {
        case STATE_START:
            // keep only chars that start a token with a buffered lexeme (identifier, number, dot)
            c = lexer_peek_char(self, err, &internal_err, 0);
            lexer_get_char(self, err, &internal_err, c == '_' || c == '.' || isalnum((unsigned char)c));

            switch (c)
            {
            case '\t':
                if (!self->NL) // no token on this line yet
                    *err = INVALID_INDENTATION;
                break;
            case ' ':
            case '\r': // CR of CRLF is whitespace
                break;
            // EOL, EOF
            case '\0':
                if (internal_err != INTERNAL_END_OF_FILE)
                    *err = INVALID_SYMBOL;
                // fall through - NUL byte ends the line like EOF (after setting the error)
            case '\n':
                self->indent_cnt = 0;
                self->current_state = STATE_NL;
                break;
            // Simple tokens
            case '|':
                self->current_state = STATE_OR;
                break;
            case '&':
                self->current_state = STATE_AND;
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
                self->Pdepth++;
                break;
            case ')':
                res.type = TOKEN_CLOSE_PAREN;
                self->Pdepth--;
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
                self->current_state = STATE_UNDERSCORE;
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
            case '0':
                self->current_state = STATE_ZERO;
                break;
            case '.':
                self->current_state = STATE_DOT;
                break;
            default:
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                    self->current_state = STATE_IDENTIFIER;
                else if (c >= '1' && c <= '9')
                    self->current_state = STATE_INT;
                else
                    *err = INVALID_SYMBOL;
                break;
            }
            res.line = self->current_line;
            break;
        // ANNOTATION
        case STATE_ANNOTATION: {
            bool is_main = lexer_get_char(self, err, &internal_err, false) == 'm' &&
                           lexer_get_char(self, err, &internal_err, false) == 'a' &&
                           lexer_get_char(self, err, &internal_err, false) == 'i' &&
                           lexer_get_char(self, err, &internal_err, false) == 'n';
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (is_main && c != '_' && !isalnum((unsigned char)c))
                res.type = TOKEN_ANNOTATION;
            else
                *err = INVALID_SYMBOL;
        }
        break;
        // OPERATORS
        case STATE_NOT: {
            res.type = TOKEN_OPERATOR;
            if (lexer_peek_char(self, err, &internal_err, 0) == '=')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.attribute_index = OP_NOT_EQUAL;
            }
            else
                res.attribute_index = OP_NOT;
        }
        break;
        case STATE_EQUAL: {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_ASSIGN;
            char c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '=')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.attribute_index = OP_EQUAL;
            }
        }
        break;
        case STATE_MORE:
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_GREATER;
            if (lexer_peek_char(self, err, &internal_err, 0) == '=')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.attribute_index = OP_EQUAL_GREATER;
            }

            break;
        case STATE_LESS: {
            res.type = TOKEN_OPERATOR;
            res.attribute_index = OP_LESS;
            char c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '=')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.attribute_index = OP_EQUAL_LESS;
            }
        }
        break;
        case STATE_DIVIDE: {

            char c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '/')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_S_COMMENT;
            }
            else if (c == '*')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->Cdepth = 1;
                self->CSline = true;
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
            if (lexer_peek_char(self, err, &internal_err, 0) == '|')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.type = TOKEN_OPERATOR;
                res.attribute_index = OP_OR;
            }
            else
                *err = INVALID_SYMBOL;
            break;
        case STATE_AND:
            if (lexer_peek_char(self, err, &internal_err, 0) == '&')
            {
                lexer_get_char(self, err, &internal_err, false);
                res.type = TOKEN_OPERATOR;
                res.attribute_index = OP_AND;
            }
            else
            {
                *err = INVALID_SYMBOL;
            }
            break;
        // COMMENTS
        case STATE_INDENT_DIV:
            c = lexer_get_char(self, err, &internal_err, false);
            if (c == '/')
            {
                self->current_state = STATE_S_COMMENT;
            }
            else if (c == '*')
            {
                self->Cdepth++;
                self->CSline = true;
                self->current_state = STATE_M_COMMENT;
            }
            else
            {
                *err = INVALID_SYMBOL;
            }
            break;
        case STATE_S_COMMENT:
            c = lexer_get_char(self, err, &internal_err, false);
            if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
            {
                self->indent_cnt = 0;
                self->current_state = STATE_NL;
            }
            break;
        case STATE_M_COMMENT:
            c = lexer_get_char(self, err, &internal_err, false);
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_COMMENT;
            else if (c == '\n')
            {
                self->CSline = false;
                self->current_state = STATE_M_COMMENT;
            }
            else if (c == '/')
                self->current_state = STATE_MLINE_DIV;
            else if (c == '*')
                self->current_state = STATE_MLINE_MUL;
            else
                self->current_state = STATE_M_COMMENT;
            break;
        case STATE_MLINE_DIV:
            c = lexer_get_char(self, err, &internal_err, false);
            self->current_state = STATE_M_COMMENT;
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_COMMENT;
            else if (c == '\n')
                self->CSline = false;
            else if (c == '*')
                self->Cdepth++;
            break;
        case STATE_MLINE_MUL:
            c = lexer_get_char(self, err, &internal_err, false);
            self->current_state = STATE_M_COMMENT;
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_COMMENT;
            else if (c == '\n')
                self->CSline = false;
            else if (c == '*')
                self->current_state = STATE_MLINE_MUL;
            else if (c == '/')
            {
                self->Cdepth--;
                if (self->Cdepth == 0)
                {
                    if (!self->CSline)
                        self->current_state = STATE_WAITING_EOL;
                    else if (!self->NL) // comment starts the line ("  /* c */ x"): x keeps indentation before it
                        self->current_state = STATE_NL_AFTER_COMMENT;
                    else
                        self->current_state = STATE_START;
                }
            }
            break;
        case STATE_WAITING_EOL:
            c = lexer_get_char(self, err, &internal_err, false);
            if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
            {
                self->indent_cnt = 0;
                self->current_state = STATE_NL;
            }
            else if (c == ' ' || c == '\t' || c == '\r')
                self->current_state = STATE_WAITING_EOL;
            else if (c == '/')
                self->current_state = STATE_INDENT_DIV;
            else
                *err = INVALID_SYMBOL;
            break;
        case STATE_NL:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (self->NL && (self->Pdepth == 0 || internal_err == INTERNAL_END_OF_FILE))
            { // a non-empty logical line ended: EOL goes before anything on the next lines
                res.type = TOKEN_EOL;
                res.line = self->current_line;
            }
            else if (c == ' ')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->indent_cnt++;
            }
            else if (c == '\t') // tab before the first token of line
                *err = INVALID_INDENTATION;
            else if (c == '\r') // CR of CRLF
                lexer_get_char(self, err, &internal_err, false);
            else if (c == '\n')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->indent_cnt = 0;
            }
            else if (c == '/')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_INDENT_DIV; // indent_cnt is kept
            }
            else if (self->Pdepth == 0 || internal_err == INTERNAL_END_OF_FILE)
            {
                res.line = self->current_line;
                if (internal_err == INTERNAL_END_OF_FILE)
                    self->current_state = STATE_DEDENT;
                else if (self->indent_cnt > lexer_indent_top(self, err))
                {
                    res.type = TOKEN_INDENT;
                    lexer_indent_push(self, err);
                }
                else if (self->indent_cnt < lexer_indent_top(self, err))
                    self->current_state = STATE_DEDENT;
                else
                    self->current_state = STATE_START;
            }
            else if (self->Pdepth > 0)
            {
                if (self->indent_cnt <= lexer_indent_top(self, err))
                    *err = INVALID_PARENTACES_INDENTATION;
                else
                    self->current_state = STATE_START;
            }
            else
                self->current_state = STATE_START;
            break;
        case STATE_NL_AFTER_COMMENT:
            if (lexer_peek_char(self, err, &internal_err, 0) == ' ')
                lexer_get_char(self, err, &internal_err, false); // not indentation
            else
                self->current_state = STATE_NL;
            break;
        case STATE_DEDENT:
            if (self->indent_cnt < lexer_indent_top(self, err))
            {
                error stack_err = NONE;
                int *ptr = stack_pop(self->indent_size_s, &stack_err);
                if (stack_err != NONE)
                {
                    lexer_check_error(err, stack_err);
                    break;
                }
                free(ptr);
                if (self->indent_cnt > lexer_indent_top(self, err))
                {
                    *err = INVALID_INDENTATION;
                    break;
                }
                res.type = TOKEN_DEDENT;
                res.line = self->current_line;
            }
            else if (internal_err == INTERNAL_END_OF_FILE)
            {
                res.type = TOKEN_EOF;
            }
            else
            {
                self->current_state = STATE_START;
            }
            break;
        // IDENTIFIERS
        case STATE_UNDERSCORE:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '_' || isalnum(c)) // todo: check if isalnum is correct
                self->current_state = STATE_IDENTIFIER;
            else
            {
                res.type = TOKEN_UNDERSCORE;
            }
            break;
        case STATE_IDENTIFIER:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '_' || isalnum(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_IDENTIFIER;
            }
            else
            {
                if (!lexer_buffer_push(self, err, '\0'))
                    break;
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
                if (strcmp(self->buffer, "false") == 0)
                {
                    res.type = TOKEN_BOOLEAN;
                    res.attribute_index = 0;
                }
                else if (strcmp(self->buffer, "true") == 0)
                {
                    res.type = TOKEN_BOOLEAN;
                    res.attribute_index = 1;
                }
                if (res.type != TOKEN_KEYWORD && res.type != TOKEN_BOOLEAN)
                {
                    res.type = TOKEN_IDENTIFIER;
                    res.attribute_index = lexer_insert_identifier(self, err);
                }
            }
            break;
        // LITERALS
        case STATE_STR_START:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '"')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_STR_END_OR_MULTILN;
            }
            else if (c == '\\')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_STR_ESCAPE;
            }
            else if (c != '\n' && internal_err == NONE)
                self->current_state = STATE_STR;
            else
                *err = UNFINISHED_STRING;
            break;
        case STATE_STR_END_OR_MULTILN:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '"')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_STR_MULTILN;
            }
            else
            {
                if (!lexer_buffer_push(self, err, '\0')) // empty string ""
                    break;
                res.type = TOKEN_STRING;
                res.attribute_index = lexer_insert_string(self, err);
            }
            break;
        case STATE_STR_MULTILN: // everything incl. quotes is kept, closing """ is cut off in STATE_STR_MQ2
            c = lexer_get_char(self, err, &internal_err, true);
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_STRING;
            else if (c == '"')
                self->current_state = STATE_STR_MQ1;
            break;
        case STATE_STR_MQ1:
            c = lexer_get_char(self, err, &internal_err, true);
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_STRING;
            else
                self->current_state = c == '"' ? STATE_STR_MQ2 : STATE_STR_MULTILN;
            break;
        case STATE_STR_MQ2:
            c = lexer_get_char(self, err, &internal_err, true);
            if (internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_STRING;
            else
                self->current_state = c == '"' ? STATE_STR_MQ3 : STATE_STR_MULTILN;
            break;
        case STATE_STR_MQ3:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '"')
            {
                c = lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_STR_MQ3;
            }
            else
            {
                self->buffer[self->buffer_index - 3] = '\0'; // cut off closing """
                self->current_state = STATE_STR_END;
            }
            break;
        case STATE_STR_ESCAPE: {
            char decoded;
            switch (lexer_get_char(self, err, &internal_err, false))
            {
            case '\\':
                decoded = '\\';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'r':
                decoded = '\r';
                break;
            case '"':
                decoded = '"';
                break;
            default:
                *err = INVALID_ESCAPE_SEQUENCE;
                decoded = '\0';
                break;
            }
            if (*err == NONE && lexer_buffer_push(self, err, decoded))
                self->current_state = STATE_STR;
        }
        break;
        case STATE_STR:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '\n' || internal_err == INTERNAL_END_OF_FILE)
                *err = UNFINISHED_STRING;
            else if (c == '"')
            {
                lexer_get_char(self, err, &internal_err, false);
                if (lexer_buffer_push(self, err, '\0'))
                    self->current_state = STATE_STR_END;
            }
            else if (c == '\\')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_STR_ESCAPE;
            }
            else
                lexer_get_char(self, err, &internal_err, true);
            break;
        case STATE_STR_END:
            res.type = TOKEN_STRING;
            res.attribute_index = lexer_insert_string(self, err);
            break;
        // NUMERIC
        case STATE_INT:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_INT;
            }
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false); // eat underscore
                self->current_state = STATE_UINT;
            }
            else if (c == '.')
            {
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(self, err, &internal_err, true); // eat e/E
                self->current_state = STATE_EXP;
            }
            else if (isalpha(c))
                *err = INVALID_SYMBOL; // 12abc, 0x1F, 1L
            else
                self->current_state = STATE_PROCESS_INT; // c stays unread, it starts the next token
            break;
        case STATE_UINT:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
                self->current_state = STATE_INT;
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false); // eat underscore
                self->current_state = STATE_UINT;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_ZERO:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_DOUBLE_START;
            }
            else if (c == '.')
            {
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_UDOUBLE_START;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_D_SUFF;
            }
            else if (isalpha(c))
                *err = INVALID_SYMBOL; // 12abc, 0x1F, 1L
            else
                self->current_state = STATE_PROCESS_INT; // c stays unread, it starts the next token
            break;
        case STATE_PROCESS_INT: {
            res.type = TOKEN_INT;
            if (!lexer_buffer_push(self, err, '\0'))
                break;
            int result = utils_string_to_int(&internal_err, self->buffer);
            if (!lexer_check_error(err, internal_err))
                break;
            res.attribute_index = attribute_table_insert_integer(self->attr_table, err, result);
        }
        break;
        case STATE_DOUBLE_START:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_DOUBLE_START;
            }
            else if (c == '.' && isdigit(lexer_peek_char(self, err, &internal_err, 1))) // 01.foo stays an error
            {
                self->current_state = STATE_DOT_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_UDOUBLE_START;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_EXP;
            }
            else
            {
                *err = INVALID_SYMBOL;
            }
            break;
        case STATE_UDOUBLE_START:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
                self->current_state = STATE_DOUBLE_START;
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false); // eat underscore
                self->current_state = STATE_UDOUBLE_START;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_DOT:
            if (isdigit(lexer_peek_char(self, err, &internal_err, 0)))
            {
                self->current_state = STATE_DOUBLE;
            }
            else
            {
                res.type = TOKEN_DOT;
            }
            break;
        case STATE_DOT_DOUBLE: // current char is dot
            c = lexer_peek_char(self, err, &internal_err, 1);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
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
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_DOUBLE;
            }
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_UDOUBLE;
            }
            else if (c == 'e' || c == 'E')
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_EXP;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_D_SUFF;
            }
            else
                self->current_state = STATE_D_SUFF; // no suffix, same end-of-literal check
            break;
        case STATE_UDOUBLE:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
                self->current_state = STATE_DOUBLE;
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false); // eat underscore
                self->current_state = STATE_UDOUBLE;
            }
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_EXP:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
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
            lexer_get_char(self, err, &internal_err, true); // keep sign
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
                self->current_state = STATE_EXP_PART;
            else
                *err = INVALID_SYMBOL;
            break;

        case STATE_EXP_PART:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
            {
                lexer_get_char(self, err, &internal_err, true);
                self->current_state = STATE_EXP_PART;
            }
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_UEXP_PART;
            }
            else if (c == 'd' || c == 'D')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_D_SUFF;
            }
            else
                self->current_state = STATE_D_SUFF; // no suffix, same end-of-literal check
            break;
        case STATE_UEXP_PART:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (isdigit(c))
                self->current_state = STATE_EXP_PART;
            else if (c == '_')
            {
                lexer_get_char(self, err, &internal_err, false);
                self->current_state = STATE_UEXP_PART;
            }
            else
                *err = INVALID_SYMBOL;
            break;
        case STATE_D_SUFF:
            c = lexer_peek_char(self, err, &internal_err, 0);
            if (c == '_' || isalnum(c) || c == '.')
                *err = INVALID_SYMBOL; // 1.5x, 1.5Dx, 2e0.01, 1.5.5
            else
                self->current_state = STATE_PROCESS_DOUBLE; // c stays unread, it starts the next token
            break;
        case STATE_PROCESS_DOUBLE:
            res.type = TOKEN_DOUBLE;
            if (!lexer_buffer_push(self, err, '\0'))
                break;
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
    if (self->current_state != STATE_DEDENT && self->current_state != STATE_NL)
        self->current_state = STATE_START; // next call starts fresh
    self->NL = res.type != TOKEN_EOL && res.type != TOKEN_EOF && res.type != TOKEN_INDENT && res.type != TOKEN_DEDENT;
    return res;
}
