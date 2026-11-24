// @file stack.h
//
// @brief stack struct and method declaration
// @author Daniil Didenko (xdidend00)

#ifndef STACK_H
#define STACK_H

#include "error.h"

struct stack_s;
typedef struct stack_s stack;

// @brief Creates new empty stack;
stack *new_stack(error *err);

// @brief Destructs given stack
void stack_destruct(stack **self); // for emergency purposes

// @brief pushes data on stack
//
// @param data - pointer to data we want to insert (only dynamically allocated, ownership transition)
void stack_push(stack *self, error *err, void **data);

// @brief get data from top of stack
//
// @return pointer to data which were stored on top of the stack (ownership transition)
void *stack_pop(stack *self, error *err);

// @brief get data from top of stack
// @note: inefficient method, do not use often
//
// @param depth - how far item we want to check related to top of the stack (index 0 -> top of the stack)
// @return pointer to data which were stored on given depth level
void *stack_peek(const stack *self, error *err,
                 int depth); // added this because some time it is useful to check other items

// @brief get count of items in stack
int stack_get_item_count(const stack *self);

#endif
