// @file 	utils.h
//
// @brief 	declaration of utility functions
// @author 	Daniil Didenko (xdidend00)

#ifndef UTILS_H
#define UTILS_H

#include "error.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// @brief safe allocator, zeroes allocated memory in order to prevent UB
//
// @param err is a pointer to error handling variable in calling function
// @param size is number of bytes we want to allocate
// @return pointer to an allocated chunk of memory
void *utils_safe_malloc(error *err, size_t size);

// @brief safe reallocator, NULLates old pointer if reallocation fails
//
// @param err is a pointer to error handling variable in calling function
// @param ptr is a reference to pointer to chunk of memory we want to reallocate
// @param size is number of bytes we want to allocate
void utils_safe_realloc(error *err, void **ptr, size_t size);

// @brief safe free wrapper, which will NULLate freed pointer
//
// @param ptr is a pointer we want to free
void utils_safe_free(void **ptr);

#endif
