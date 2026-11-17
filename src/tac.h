// @brief Header file for TAC (Three Adress Code)
// @author Oleksandr Fedorenko (xfedoro00)

#ifndef TAC_H
#define TAC_H

#include "ast.h"
#include "attribute_table.h"
#include "utils.h"

// @brief Enum of possible operation in TAC
typedef enum
{
    TAC_MAIN,        // (MAIN, ---, ---, FUNC_NAME)
    TAC_PLUS,        // (PLUS, OPERAND_1, OPERAND_2, RESULT), ALSO CONCAT FOR STR,  RESULT = INT || FLOAT || STR
    TAC_MINUS,       // (MINUS, OPERAND_1, OPERAND_2, RESULT), RESULT = INT || FLOAT
    TAC_UNARY_MINUS, // (UNARY_MINUS, OPERAND_1, ---, RESULT), RESULT = INT || FLOAT
    TAC_MUL,         // (MUL, OPERAND_1, OPERAND_2, RESULT),  RESULT = INT || FLOAT
    TAC_DIV,  // (DIV, OPERAND_1, OPERAND_2, RESULT), RESULT = int/int => int division, float/float = float division
    TAC_CALL, // (CALL, FUNC_NAME, NUM_OF_PARAMS, RESULT), NUM_OF_PARAMS = how many params func have, RESULT = return of
              // function call
    TAC_GOTO, // (GOTO, ---, ---, NAME_OF_LABEL)
    TAC_PARAM,      // (PARAM, PARAM_1, ---, ---)
    TAC_ASSIGNMENT, // (ASSIGNMENT, OPERAND_1, ---, RESULT), FOR EX. X = 10 => (ASSIGMENT, 10, ---, X)
    TAC_RETURN,     // (RETURN, ---, ---, RESULT), RESULT = computed operand or identifier
    TAC_IF_JUMP,    // (IF_JUMP, TRUE_LABEL, FALSE_LABEL, RESULT), RESULT = computed operand
    TAC_LABEL,      // (LABEL, ---, ---, NAME_OF_LABEL),
    TAC_EQUALS,     // (EQUALS, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 == op_2), RESULT =
                    // BOOLEAN
    TAC_GREATER,    // (GREATER, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 > op_2),RESULT =
                    // BOOLEAN
    TAC_LESSER, // (LESSER, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 < op_2), RESULT = BOOLEAN
    TAC_NOT,    // (NOT, OPERAND_1, ---, RESULT), RESULT = result of expression (!op_1), RESULT = BOOLEAN
    TAC_FUNC_START, // (TAC_FUNC_START, ---, ---, FUNC_NAME)
    TAC_FUNC_END,   // (TAC_FUNC_END, ---, ---, FUNC_NAME)
    TAC_OR,         // (OR, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 || op_2)
    TAC_AND,        // (AND, OPERAND_1, OPERAND_2, RESULT), RESULT = result of expression (op_1 && op_2)
    TAC_VAR_DEF, // (VAR_DEF, OPERAND_1, ---, RESULT) RESULT = defined variable for ex. var x = 10 => (VAR_DEF, 10, ---,
                 // x)
    TAC_VAL_DEF,  // (VAL_DEF, OPERAND_1, ---, RESULT) RESULT = defined variable for ex. val x = 10 => (VAL_DEF, 10, ---,
                 // x)
    TAC_CALL_PARAM // (TAC_CALL_PARAM, PARAM_1, ---, ---)
} tac_operator;

// @brief Enum of argument/operand type
typedef enum
{
    ARG_ERROR,
    ARG_EMPTY,
    ARG_TEMPORARY_VAR,
    ARG_LABEL,
    ARG_FUNC,
    ARG_ID,
    ARG_LITERAL, // used if arg is a literal
} tac_argument_type;

// @brief Enum of argument value type
typedef enum
{
    ARG_NONE = AST_TYPE_NONE,
    ARG_UNIT = AST_TYPE_UNIT,
    ARG_STRING = AST_TYPE_STRING,
    ARG_FLOAT = AST_TYPE_DOUBLE,
    ARG_INT = AST_TYPE_INT,
    ARG_BOOL = AST_TYPE_BOOLEAN,

} tac_argument_value_type;

//@ brief Single TAC argument/operand
typedef struct
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
        double const_double;     // for double operand
        bool const_bool;         // for boolean operand
        char *const_str;         // for string operand / UNIT
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
typedef struct
{
    long size;
    tac_instruction *first_instruction;
    tac_instruction *last_instruction; // To append to the end in constant time
    tac_instruction *curr_instruction; // Pointer to the current instruction
} tac_list;

// @brief Destroy tac_list
void tac_list_destruct(tac_list *self);

// @brief Main function to generate TAC instructions
// @return Pointer to list of TAC instructions
tac_list *tac_generate(error *err, ast_node **root, attribute_table *attr_table);

#endif /*TAC_H*/
