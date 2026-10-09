// @brief Header file for TAC (Three Adress Code)
// @author Created by Oleksandr Fedorenko (xfedoro00)

#ifndef TAC
#define TAC

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

} Operator;

// @brief Structure of Three Adress Instruction (Quadruple)
typedef struct tac
{
    Operator op; // Operation (+,-, =, goto, call)
    char *arg1;  // Pointer to item in symtable
    char *arg2;  // Pointer to item in symtable
    char *result;
    struct tac *next_instuction;

} ta_instruction, *ta_instruction_ptr;

// @brief Structure of TAC (All instruction in linked list)
typedef struct tac_list
{
    long size;
    ta_instruction_ptr first_instruction;
    ta_instruction_ptr last_instruction; // To append to the end in constant time
    ta_instruction_ptr curr_instruction; // Pointer to the current instruction

} tac_list, *tac_list_ptr;

// @brief Initialize tac_list
// @param list  - Pointer to the list
void ta_list_init(tac_list_ptr list);

// @brief Destroy tac_list
// @param list - Pointer to the list
void ta_list_destruct(tac_list_ptr list);

// @brief Gets current instruction and moves pointer to the next one
// @param list - Pointer to the list
// @return Pointer to current_instruction in the list
ta_instruction_ptr get_curr_instruction(tac_list_ptr list);

// @brief Gets instruction after current and DO NOT moves pointer.
// Must be used ONLY after get_curr_instruction!!!
// @param list - Pointer to the list
// @return Pointer to the instruction after current
ta_instruction_ptr peek_next_instruction(tac_list_ptr list);

// @brief Append existing instruction to the list
// @param list - Pointer to the list, ir_node - instruction to be inserted
void append_instruction(tac_list_ptr list, ta_instruction_ptr ir_node);

// @brief Create a new instruction
// @param &err - error, op - operation, arg1 - first operand,
// arg2 - second operand, result - operand to be assigned
ta_instruction_ptr create_instruction(error *err, char *op, char *arg1, char *arg2, char *result);

ta_instruction_ptr traverse_postorder(void *ast, tac_list_ptr list);

#endif /*TAC*/