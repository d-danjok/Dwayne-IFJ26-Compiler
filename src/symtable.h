#ifndef SYMTABLE_H
#define SYMTABLE_H

#include "error.h"
#include <stdbool.h>

#include "token.h"

typedef enum
{
    DATA_TYPE_UNDEFINED,
    DATA_TYPE_INT = TOKEN_INT,
    DATA_TYPE_DOUBLE = TOKEN_DOUBLE,
    DATA_TYPE_STRING = TOKEN_STRING,
    DATA_TYPE_UNIT = TOKEN_UNIT,
    DATA_TYPE_BOOLEAN = TOKEN_BOOLEAN,
} data_type;

typedef enum
{
    ID_TYPE_VARIABLE,
    ID_TYPE_CONSTANT,
    ID_TYPE_FUNCTION
} identifier_type;

// exposed values
typedef union {
    int value_int;
    double value_double;
    char *value_string;
    bool value_bool;
} identifier_value;

typedef struct identifier_data_s
{
    identifier_value value;
    data_type data_type; // in case of function return type

    identifier_type type;
    bool is_initialised;

    // used only for functions
    int function_arg_count;
    data_type function_args[]; // when allocating give more memory if you need
                               // some args example: for 0 args:
                               // utils_safe_malloc(err, sizeof(identifier_data));
                               // for N args: utils_safe_malloc(err,
                               // sizeof(identifier_data) + N * sizeof(data_type))
} identifier_data;

#define NEW_IDENTIFIER_DATA(err, number_of_function_args)                                                              \
    /*Note: set number of function args to 0 allocating not for function */                                            \
    (identifier_data *)utils_safe_malloc(err, sizeof(identifier_data) + (number_of_function_args) * sizeof(data_type))

// symtable itself
struct symtable_s;
typedef struct symtable_s symtable;

// @brief Creates new empty symtable;
symtable *new_symtable(error *err);

// @brief Destructs given symtable
void symtable_destruct(symtable **self);

// @brief Insert item into symtable
//
// @param identifier_id - index of identifier in attribute table we want to
// insert
// @param data - pointer to data we want to insert (only dynamically allocated,
// ownership transition)
void symtable_insert(symtable *self, error *err, int identifier_id, identifier_data **data);

// @brief Remove item from symtable
//
// @param identifier_id - index of identifier in attribute table we want to
// remove
void symtable_remove(symtable *self, int identifier_id);

// @brief Search data in symtable by identifier_id
//
// @param identifier_id - index of identifier in attribute table we want to find
// @return true if found, otherwise else
bool symtable_search(symtable *self, int identifier_id);

// @brief Get data from symtable by identifier_id
//
// @param identifier_id - index of identifier in attribute table we want to get
// @return pointer to data stored of identifier
identifier_data *symtable_getdata(symtable *self, int identifier_id);

#endif
