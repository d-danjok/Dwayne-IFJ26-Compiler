// @file bst.h
//
// @author Daniil Didenko (xdidend00)

#ifndef BST_H
#define BST_H

#include "error.h"
#include <stddef.h>

struct bst_table_s;
typedef struct bst_table_s bst_table;

// @brief Creates new empty bst table;
bst_table *new_table(error *err);

// @brief Destructs given bst table
void bst_destruct(bst_table **self);

// @brief Insert item into table
//
// @param key - key for data we want to insert
// @param data - pointer to data we want to insert (only dynamically allocated, ownership transition)
void bst_insert(bst_table *self, error *err, int key, void **data);

// @brief Insert item into table
//
// @param key - key for data we want to remove
void bst_remove(bst_table *self, int key);

// @brief Search data in table by key
//
// @param key - key for data we want to find
// @return true if found, otherwise else
bool bst_search(const bst_table *self, int key);

// @brief Get data from table by key
//
// @param key - key for data we want to get
// @return pointer to data stored by key
void *bst_getdata(const bst_table *self, int key);

#endif
