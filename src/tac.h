// @brief Header file for TAC (Three Adress Code)
// @author Oleksandr Fedorenko (xfedoro00)

#ifndef TAC_H
#define TAC_H

#include "utils.h"


// @brief Enum of possible operation in TAC
typedef enum op
{
    PLUS,
    MINUS,
    UNARY_MINUS,
    MUL,
    DIV,
    CALL, // (CALL, NUM_OF_PARAMS, ---, NAME_OF_FUNCTION)
    GOTO, // (GOTO, ---, ---, NAME_OF_LABEL)
    PARAM, // (PARAM, PARAM_1, PARAM_2, ---)
    ASSIGNMENT,
    RETURN, // (RETURN, ---, ---, ID || EXP)
    IF_JUMP, // (IF_JUMP, TRUE_LABEL, FALSE_LABEL, EXP)
    LABEL,  // (LABEL, ---, ---, NAME)
    EQUALS,
    GREATER,
    LESSER,
    FUNC_START, // (FUNC_START, ---, ---, NAME_OF_FUNCTION)
    FUNC_END    // (FUNC_END, ---, ---, NAME_OF_FUNCTION)
} t_operator;

// @brief Structure of Three Address Instruction (Quadruple)
typedef struct tac_instruction_s
{
    unsigned int id;
    t_operator op; // Operation (+,-, =, goto, call)
    char *arg1;  // Pointer to item in symtable
    char *arg2;  // Pointer to item in symtable
    char *result;
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

#endif /*TAC_H*/