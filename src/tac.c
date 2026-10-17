// @brief Implementation of TAC (Three Address Code)
// @author Oleksandr Fedorenko (xfedoro00)
#include "tac.h"

tac_list *new_tac_list(error *err)
{
    *err = NONE;
    tac_list *list = (tac_list *)utils_safe_malloc(err, sizeof(tac_list));

    if (*err != NONE)
    {
        return NULL;
    }

    list->size = 0;
    list->curr_instruction = NULL;
    list->first_instruction = NULL;
    list->last_instruction = NULL;

    return list;
}

static bool is_empty(tac_list *self)
{
    return self->first_instruction == NULL;
}

// TODO: Implement this when I'll have enough information
static tac_instruction *create_instruction(error *err, char *op, char *arg1, char *arg2, char *result)
{
    *err = NONE;
    tac_instruction *new_ir_node = (tac_instruction *)utils_safe_malloc(err, sizeof(tac_instruction));
    if (*err != NONE)
    {
        return NULL;
    }
    new_ir_node->next_instruction = NULL;

    return;
}

static void append_instruction(tac_list *self, tac_instruction *ir_node)
{
    if (is_empty(self))
    {
        self->first_instruction = ir_node;
        self->curr_instruction = ir_node;
        self->last_instruction = ir_node;
        self->size = 1;
        return;
    }

    self->last_instruction->next_instruction = ir_node;
    self->last_instruction = ir_node;

    self->size += 1;
    return;
}

tac_instruction *tac_get_curr_instruction(tac_list *self)
{
    if (is_empty(self) || self->curr_instruction == NULL)
    {
        return NULL;
    }
    if (self->size == 1)
    {
        self->curr_instruction = NULL;
        return self->first_instruction;
    }

    tac_instruction *tmp = self->curr_instruction;
    self->curr_instruction = tmp->next_instruction;
    return tmp;
}

tac_instruction *tac_peek_next_instruction(error *err, tac_list *self)
{
    if (is_empty(self))
    {
        return NULL;
    }
    if (self->curr_instruction == NULL)
    {
        return NULL;
    }
    return self->curr_instruction;
}
