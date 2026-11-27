// @file symtable.c
//
// @brief symbol table
// @author Daniil Didenko (xdidend00)

#include <stdbool.h>

#include "bst.h"
#include "symtable.h"
#include "utils.h"

typedef struct symtable_s
{
    bst_table *table;
} symtable;

symtable *new_symtable(error *err)
{
    *err = NONE;
    symtable *table = (symtable *)utils_safe_malloc(err, sizeof(symtable));
    if (*err != NONE)
    {
        return NULL;
    }
    table->table = new_table(err);
    if (*err != NONE)
    {
        UTILS_SAFE_FREE(&table);
        return NULL;
    }
    return table;
}

void symtable_destruct(symtable **self)
{
    bst_destruct(&(*self)->table);
    UTILS_SAFE_FREE(self);
}

void symtable_insert(const symtable *self, error *err, const int identifier_id, identifier_data **data)
{
    bst_insert(self->table, err, identifier_id, (void **)data);
}

void symtable_remove(const symtable *self, const int identifier_id)
{
    bst_remove(self->table, identifier_id);
}

bool symtable_search(const symtable *self, const int identifier_id)
{
    return bst_search(self->table, identifier_id);
}

identifier_data *symtable_getdata(const symtable *self, const int identifier_id)
{
    return (identifier_data *)bst_getdata(self->table, identifier_id);
}