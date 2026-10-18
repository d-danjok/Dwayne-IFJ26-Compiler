// @file stack.c
//
// @brief stack implementation
// @author Daniil Didenko (xdidend00)

#include "stack.h"
#include "utils.h"

typedef struct stack_item_s
{
    struct stack_item_s *next;
    void *data;
} stack_item; // implemented as linked list since it is way easier to implement
// and it wont house large amount of data, so it being slightly slower is not critical

struct stack_s
{
    stack_item *top;
    int item_count;
};

void item_destruct_recursively(stack_item **self)
{
    if (self == NULL || *self == NULL)
    {
        return;
    }

    if ((*self)->data != NULL)
    {
        free((*self)->data);
    }
    item_destruct_recursively(&(*self)->next);
    UTILS_SAFE_FREE(self);
}

stack *new_stack(error *err)
{
    *err = NONE;
    stack *new = (stack *)utils_safe_malloc(err, sizeof(stack));
    if (*err != NONE)
    {
        return NULL;
    }

    new->item_count = 0;
    new->top = NULL;

    return new;
}

void stack_destruct(stack **self)
{
    if (self == NULL || *self == NULL)
    {
        return;
    }
    if ((*self)->top != NULL)
    {
        item_destruct_recursively(&(*self)->top);
    }
    UTILS_SAFE_FREE(self);
}

void stack_push(stack *self, error *err, void **data)
{
    *err = NONE;

    if (data == NULL || *data == NULL)
    {
        *err = INTERNAL_NULL_ITEM_INSERT;
        return;
    }

    stack_item *new_item = (stack_item *)utils_safe_malloc(err, sizeof(stack_item));
    if (*err != NONE)
    {
        return;
    }

    new_item->data = *data;
    *data = NULL; // ownership enforcement
    new_item->next = self->top;

    self->top = new_item;
    self->item_count++;
}

void *stack_pop(stack *self, error *err)
{
    *err = NONE;
    if (self->top == NULL)
    {
        *err = INTERNAL_STACK_IS_EMPTY;
        return NULL;
    }

    stack_item *popped_item = self->top;
    void *data = popped_item->data;

    self->top = popped_item->next;
    self->item_count--;

    free(popped_item);
    return data;
}

void *stack_peek(stack *self, error *err, int depth)
{
    *err = NONE;
    if (depth < 0 || depth >= self->item_count)
    {
        *err = INTERNAL_INDEX_OUT_OF_BOUNDS;
        return NULL;
    }

    int current_depth = 0;
    stack_item *current_item = self->top;
    while (depth != current_depth && current_item != NULL)
    {
        current_item = current_item->next;
        current_depth++;
    }
    return current_item->data;
}
