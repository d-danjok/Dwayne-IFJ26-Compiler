// @file   	error.c
//
// @brief  	implementation of error related functions
// @author 	Daniil Didenko (xdidend00)

#include "error.h"

bool error_is_internal(error err)
{
    return err >= INTERNAL_MEMORY_ALLOCATION;
}
