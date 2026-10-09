// @brief Implementation of TAC (Three Adress Code)
// @author Created by Oleksandr Fedorenko (xfedoro00)

#include "tac.h"
#include "utils.h"

void ta_list_init(tac_list_ptr list)
{
    list->size = 0;
    list->curr_instruction = NULL;
    list->first_instruction = NULL;
    list->last_instruction = NULL;
}

bool isEmpty(tac_list_ptr list)
{
    return list->first_instruction == NULL;
}

// TODO: Implement this when I'll have enough information
ta_instruction_ptr create_instruction(error *err, char *op, char *arg1, char *arg2, char *result)
{
    *err = NONE;
    ta_instruction_ptr new_ir_node = (ta_instruction_ptr)utils_safe_malloc(err, sizeof(ta_instruction));
    if (*err != NONE)
    {
        return;
    }
    new_ir_node->next_instuction = NULL;
    return;
}

void append_instruction(tac_list_ptr list, ta_instruction_ptr ir_node)
{
    if (isEmpty(list))
    {
        list->first_instruction = ir_node;
        list->curr_instruction = ir_node;
        list->last_instruction = ir_node;
        list->size = 1;
        return;
    }

    list->last_instruction->next_instuction = ir_node;
    list->last_instruction = ir_node;

    list->size += 1;
    return;
}

ta_instruction_ptr get_curr_instruction(tac_list_ptr list)
{
    if (isEmpty(list) || list->curr_instruction == NULL)
    {
        return NULL;
    }
    if (list->size == 1)
    {
        list->curr_instruction = NULL;
        return list->first_instruction;
    }

    ta_instruction_ptr tmp = list->curr_instruction;
    list->curr_instruction = tmp->next_instuction;
    return tmp;
}

ta_instruction_ptr peek_next_instruction(tac_list_ptr list)
{
    if (isEmpty(list))
    {
        return NULL;
    }
    if (list->curr_instruction == NULL)
    {
        return NULL;
    }
    return list->curr_instruction;
}
