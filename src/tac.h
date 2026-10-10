// @brief Header file for TAC (Three Adress Code)
// @author Oleksandr Fedorenko (xfedoro00)

#ifndef TAC_H
#define TAC_H

// @brief Enum of possible operation in TAC
typedef enum op
{
    PLUS,
    MINUS,
    UMINUS,
    MUL,
    DIV,
    CALL,
    GOTO,
    PARAM,
    ASSIGNMENT,
    RETURN,
    IF_STM,
    LABEL,
} operator;

// @brief Structure of Three Adress Instruction (Quadruple)
typedef struct tac
{
    operator op; // Operation (+,-, =, goto, call)
    char *arg1;  // Pointer to item in symtable
    char *arg2;  // Pointer to item in symtable
    char *result;
    struct tac *next_instuction;

} tac_instruction;

// @brief Structure of TAC (All instruction in linked list)
typedef struct tac_list
{
    long size;
    tac_instruction *first_instruction;
    tac_instruction *last_instruction; // To append to the end in constant time
    tac_instruction *curr_instruction; // Pointer to the current instruction

} tac_list;

// @brief Initialize new tac_list
// @return Pointer to the new initialized list
tac_list *new_tac_list();

// @brief Destroy tac_list
void tac_list_destruct(tac_list *self);

// @brief Gets current instruction and moves pointer to the next one
// @return Pointer to current_instruction in the list
tac_instruction *get_curr_instruction(tac_list *self);

// @brief Gets instruction after current and DO NOT moves pointer.
// Must be used ONLY after get_curr_instruction!!!
// @return Pointer to the instruction after current
tac_instruction *peek_next_instruction(tac_list *self);

// @brief Create a new instruction
// @param op -> operation, arg1 -> first operand,
// arg2 -> second operand, result -> operand to be assigned
tac_instruction *create_instruction(error *err, char *op, char *arg1, char *arg2, char *result);

tac_instruction *traverse_postorder(void *ast, tac_list *self);

#endif /*TAC_H*/