// @file attribute_table.h
//
// @author 	Daniil Didenko 		(xdidend00)
//			Oleksandr Fedorenko (xfedoro00)

#ifndef ATTRIBUTE_TABLE_H
#define ATTRIBUTE_TABLE_H

#include "utils.h"

struct attribute_table_s;
typedef struct attribute_table_s attribute_table;

// @brief creates new attribute_table
attribute_table *new_attribute_table(error *err);

// @brief destructs given attribute table
void destruct_attribute_table(attribute_table **self);

// @brief inserts integer value into attribute table and returns index by which it can be accessed
//
// @param val - integer value we want to insert
// @return index in by which value can be accessed
int attribute_table_insert_integer(attribute_table *self, error *err, int val);

// @brief inserts float value into attribute table and returns index by which it can be accessed
//
// @param val - float value we want to insert
// @return index in by which value can be accessed
int attribute_table_insert_float(attribute_table *self, error *err, float val);

// @brief inserts string value into attribute table and returns index by which it can be accessed
//
// @param val - string value we want to insert (ownership transition)
// @return index in by which value can be accessed
int attribute_table_insert_string(attribute_table *self, error *err, char *val);

// @brief inserts identifier into attribute table and returns index by which it can be accessed
//
// @param identifier - identifier we want to insert
// @return index in by which identifier can be accessed
int attribute_table_insert_identifier(attribute_table *self, error *err, char *identifier);

// @brief get integer value by index
//
// @param index value from which we want to get
int attribute_table_get_integer(attribute_table *self, error *err, int index);

// @brief get float value by index
//
// @param index value from which we want to get
float attribute_table_get_float(attribute_table *self, error *err, int index);

// @brief get string value by index
//
// @param index value from which we want to get
char *attribute_table_get_string(attribute_table *self, error *err, int index);

// @brief get identifier by index
//
// @param index value from which we want to get
char *attribute_table_get_identifier(attribute_table *self, error *err, int index);

#endif
