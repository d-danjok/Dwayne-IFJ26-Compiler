// @file symtable.c
//
// @brief symbol table
// @author Daniil Didenko (xdidend00)

#include "symtable.h"
#include "bst.h"

typedef union {
    int int_value;
    float double_value;
    char *string_value;
    bool bool_value;
} identifier_data;

typedef struct identifier_details_s
{
    data_type data_type;

    bool is_constant;
    bool is_function;

    bool is_initialised;
} identifier_details;

typedef struct symtable_s
{
    bst_table *table;
} symtable;