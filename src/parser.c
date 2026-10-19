// @file 	parser.c
//
// @author 	Daniil Didenko (xdidend00)

typedef enum
{
    OPERAND,

    LPAREN, // (
    RPAREN, // )

    OP_UNARY_PLUS,
    OP_UNARY_MINUS,

    OP_MULTIPLY,
    OP_DIVIDE,

    OP_PLUS,
    OP_MINUS,

    // comp operators
    OP_LESS,          // <
    OP_LESS_EQUAL,    // <=
    OP_GREATER,       // >
    OP_GREATER_EQUAL, // >=

    OP_EQUALS,
    OP_NOT_EQUALS,

    EXPR_TERMINATOR
} precedence_table_member;

typedef enum
{
    PR_LS, // less
    PR_EQ, // equal
    PR_GR, // greater

    IN_VL, // invalid
} operator_priority;

static const operator_priority precedence_table[EXPR_TERMINATOR][EXPR_TERMINATOR] = {
    // same order as in precedence_table_member enum
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
};
