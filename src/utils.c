// @file 	utils.c
//
// @brief 	implementation of utility functions
// @author 	Daniil Didenko (xdidend00)

#include "utils.h"

void* utils_safe_malloc(error* err, size_t size)
{
	*err = NONE;
	if (size == 0)
	{
		*err = INTERNAL_INVALID_MEMORY_ALLOCATION_SIZE;
		return NULL;
	}

	void* ptr = malloc(size);
	if (ptr == NULL)
	{
		*err = INTERNAL_MEMORY_ALLOCATION;
		return NULL;
	}

	memset(ptr, 0, size);
	return ptr;
}

void utils_safe_free(void** ptr)
{
	free(*ptr);
	*ptr = NULL;
}