// @brief Header file for TAC (Three Adress Code)
// @author Oleksandr Fedorenko (xfedoro00)

#ifndef TAC_H
#define TAC_H

#include "ast.h"
#include "utils.h"

// @brief Enum of possible operation in TAC
typedef enum
{
    TAC_PLUS,
    TAC_MINUS,
    TAC_UNARY_MINUS,
    TAC_MUL,
    TAC_DIV,
    TAC_CALL,  // (CALL, NAME_OF_FUNCTION, NUM_OF_PARAMS, VAR_OF_RESULT)
    TAC_GOTO,  // (GOTO, ---, ---, NAME_OF_LABEL)
    TAC_PARAM, // (PARAM, PARAM_1, ---, ---) || (PARAM, ID_PARAM, ---, ID)
    TAC_ASSIGNMENT,
    TAC_RETURN,  // (RETURN, ---, ---, ID || EXP)
    TAC_IF_JUMP, // (IF_JUMP, TRUE_LABEL, FALSE_LABEL, EXP)
    TAC_LABEL,   // (LABEL, ---, ---, NAME)
    TAC_EQUALS,
    TAC_GREATER,
    TAC_LESSER,
    TAC_NOT_EQ,
    TAC_NOT,
    TAC_FUNC_START,  // (TAC_FUNC_START, ---, ---, FUNC_ID)
    TAC_FUNC_END,    // (TAC_FUNC_END, ---, ---, FUNC_ID)
    TAC_INT_TO_FLOAT // (INT_TO_FLOAT, ARG_1, ---, VAR_OF_RESULT)
} tac_operator;

typedef enum
{
    ARG_TEMPORARY_VAR,
    ARG_LABEL,
    ARG_FUNC,
    ARG_ID,
    ARG_LITERAL, // used if arg is a literal
} tac_argument_type;

typedef enum
{
    ARG_NONE = AST_TYPE_NONE, // for errors
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
        unsigned int attribute_index_tac; // for identifiers and literals
        char *identifier_name;
        unsigned int temporary_var_id; // for t1,t2, ..., tn, kde n je N U {0}
        unsigned int label_id;         // for L1, L2, ..., Ln, kde n je N U {0}
        void *unit;
        // Constants used for convenient constant folding
        long long int const_int;
        double const_double;
        bool const_bool;
        char *const_str;
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
static tac_argument create_temporary_variable(tac_argument_value_type expr_type);

#endif /*TAC_H*/