// @brief Header file for TAC (Three Adress Code)
// @author Oleksandr Fedorenko (xfedoro00)

#ifndef TAC_H
#define TAC_H

#include "ast.h"
#include "utils.h"

// @brief Enum of possible operation in TAC
typedef enum
{
    TAC_PLUS, // (PLUS, OPERAND_1, OPERAND_2, RESULT), ALSO CONCAT FOR STR,  RESULT = INT || FLOAT || STR
    TAC_MINUS, // (MINUS, OPERAND_1, OPERAND_2, RESULT), RESULT = INT || FLOAT
    TAC_UNARY_MINUS, // (UNARY_MINUS, OPERAND_1, ---, RESULT), RESULT = INT || FLOAT
    TAC_MUL, // (MUL, OPERAND_1, OPERAND_2, RESULT),  RESULT = INT || FLOAT
    TAC_DIV,  // (DIV, OPERAND_1, OPERAND_2, RESULT), RESULT = INT || FLOAT
    TAC_CALL,  // (CALL, FUNC_NAME, NUM_OF_PARAMS, RESULT), NUM_OF_PARAMS = how many params func have, RESULT = return of function call
    TAC_GOTO,  // (GOTO, ---, ---, NAME_OF_LABEL)
    TAC_PARAM, // (PARAM, PARAM_1, ---, ---) || (PARAM, ID_PARAM, ---, ID)
    TAC_ASSIGNMENT, // (ASSIGNMENT, OPERAND_1, ---, RESULT), FOR EX. X = 10 => (ASSIGMENT, 10, ---, X)
    TAC_RETURN,  // (RETURN, ---, ---, ID || EXP)
    TAC_IF_JUMP, // (IF_JUMP, TRUE_LABEL, FALSE_LABEL, EXP), EXP = boolean expression
    TAC_LABEL,   // (LABEL, ---, ---, NAME_OF_LABEL),
    TAC_EQUALS,  // (EQUALS, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 == op_2), RESULT = BOOLEAN
    TAC_GREATER, // (GREATER, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 > op_2),RESULT = BOOLEAN
    TAC_LESSER, // (LESSER, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 < op_2), RESULT = BOOLEAN
    TAC_GREATER_EQ, // (GREATER_EQ, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 >= op_2),RESULT = BOOLEAN
    TAC_LESSER_EQ, // (LESSER_EQ, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 <= op_2), RESULT = BOOLEAN
    TAC_NOT_EQ, // (NOT_EQ, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 != op_2), RESULT = BOOLEAN
    TAC_NOT, // (NOT_EQ, OPERAND_1, ---, RESULT), RESULT = result of expression (!op_1), RESULT = BOOLEAN
    TAC_FUNC_START,  // (TAC_FUNC_START, ---, ---, FUNC_NAME)
    TAC_FUNC_END,    // (TAC_FUNC_END, ---, ---, FUNC_NAME)
    TAC_INT_TO_FLOAT, // (INT_TO_FLOAT, OPERAND_1, ---, RESULT), RESULT = result of int_to_float(op_1)
    TAC_OR, // (OR, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 || op_2)
    TAC_AND, // (OR, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 && op_2)
} tac_operator;

typedef enum
{
    ARG_EMPTY,
    ARG_TEMPORARY_VAR,
    ARG_LABEL,
    ARG_FUNC,
    ARG_ID,
    ARG_LITERAL, // used if arg is a literal
} tac_argument_type;

typedef enum
{
    ARG_NONE = AST_TYPE_NONE,
    ARG_UNIT = AST_TYPE_UNIT,
    ARG_STRING = AST_TYPE_STRING,
    ARG_FLOAT = AST_TYPE_DOUBLE,
    ARG_INT = AST_TYPE_INT,
    ARG_BOOL = AST_TYPE_BOOLEAN,

} tac_argument_value_type;

typedef struct tac_arg_s
{
    tac_argument_type type;
    tac_argument_value_type value_type;
    // Value of the argument
    union {

        // unsigned int attribute_index_tac; // for identifiers and literals
        char *identifier_name;
        unsigned int temporary_var_id; // for t1,t2, ..., tn, kde n je N U {0}
        unsigned int label_id;         // for L1, L2, ..., Ln, kde n je N U {0}


        // Constants used for convenient constant folding
        long long int const_int; // for integer operand
        double const_double; // for double operand
        bool const_bool; // for boolean operand
        char *const_str; // for string operand / UNIT
    };
} tac_argument;

// @brief Structure of Three Address Instruction (Quadruple)
typedef struct tac_instruction_s
{
    unsigned int id;
    tac_operator op; // Operation (+,-, =, goto, call)
    tac_argument arg1;
    tac_argument arg2;
    tac_argument result;
    struct tac_instruction_s *next_instruction;

} tac_instruction;

// @brief Structure of TAC (All instruction in linked list)
typedef struct tac_list_s
{
    long size;
    tac_instruction *first_instruction;
    tac_instruction *last_instruction; // To append to the end in constant time
    tac_instruction *curr_instruction; // Pointer to the current instruction

} tac_list;

static tac_instruction *create_instruction(error *err, const tac_operator op, const tac_argument arg1,
                                           const tac_argument arg2, const tac_argument result);
// @brief Initialize new tac_list
// @return Pointer to the new initialized list
tac_list *new_tac_list(error *err);

// @brief Destroy tac_list
void tac_list_destruct(tac_list *self);

// @brief Gets current instruction and moves pointer to the next one
// @return Pointer to current_instruction in the list
tac_instruction *tac_get_curr_instruction(tac_list *self);

// @brief Gets instruction after current and DO NOT moves pointer.
// Must be used ONLY after get_curr_instruction!!!
// @return Pointer to the instruction after current
tac_instruction *tac_peek_next_instruction(error *err, tac_list *self);

static tac_argument tac_expr_emit(error *err, tac_list *self, ast_node *expr);
// @brief Create temporary variable for simple tac instructions
// @return Temporary variable (tn) of type tac_argument
static tac_argument create_temporary_variable(tac_argument_value_type expr_type);

#endif /*TAC_H*/