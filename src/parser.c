// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

typedef enum
{
    OP_PLUS,
    OP_MINUS,

    OP_MULTIPLY,
    OP_DIVIDE,

    OP_UNARY_MINUS,

    // comp operators
    OP_LESS, // <
    OP_LESS_EQUAL, // <=
    OP_GREATER, // >
    OP_GREATER_EQUAL, // >=

    OP_EQUALS,
    OP_NOT_EQUALS,

    LPAREN, // (
    RPAREN, // )

    OPERAND,

    EXPR_TERMINATOR
} precedence_table_member;

typedef enum
{
    LS, // less
    EQ, // equal
    GR, // greater

    IV, // invalid
} operator_priority;

static const operator_priority precedence_table[EXPR_TERMINATOR + 1][EXPR_TERMINATOR + 1] = {
    //
    { GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, LS, LS, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { GR, GR, GR, GR, LS, GR, GR, GR, GR, GR, GR, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, IV, IV, IV, IV, IV, IV, LS, GR, LS, GR },
    { LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, EQ, LS, IV },
    { GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR },
    { GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, GR, IV, GR, IV, GR },
    { LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, LS, IV, LS, EQ },
};
