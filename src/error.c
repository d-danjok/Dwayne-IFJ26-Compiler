// @file   	error.c
//
// @brief  	implementation of error related functions
// @author 	Daniil Didenko (xdidend00)

#include "error.h"

bool error_is_internal(error err)
{
	if (err == INTERNAL_MEMORY_ALLOCATION || err == INTERNAL_MEMORY_FREE || err == INTERNAL_INDEX_OUT_OF_BOUNDS)
	{
		return true;
	}
	return false;
}
