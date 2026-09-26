// @file   	error.h
//
// @brief  	error enum type definition and declaration of error related functions
// @author	Daniil Didenko (xdidend00)

#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>

typedef enum
{
    NONE = 0, // not an error btw

    // Task specified errors
    INVALID_SYMBOL = 1,
    INVALID_ESCAPE_SEQUENCE = 1,
    UNFINISHED_STRING = 1,
    UNFINISHED_COMMENT = 1,

    DOUBLE_INSTEAD_OF_INT = 6,

    PROLOGUE_MISSING = 2,

    ENTER_FUNC_MISSING = 3,
    ENTER_FUNC_MORE_THEN_ONE = 4,
    ENTER_FUNC_PARAMS_FOR = 5,
    ENTER_FUNC_INVALID_RETURN_TYPE = 6,

    VAR_WRONG_ASSIGN_TYPE = 6,
    VAR_MULTIPLE_DEFINITION = 5,
    VAR_USE_OUT_OF_SCOPE = 3,

    FUNC_ARGUMENT_SAME_NAME = 4,
    FUNC_ARGUMENT_WRONG_TYPE = 5,
    FUNC_MULTIPLE_DEFINITION = 4,
    FUNC_WRONG_RETURN_TYPE = 6,
    FUNC_WRONG_TERM_TYPE = 5,
    FUNC_WRONG_ARGUMENT_COUNT = 5,
    FUNC_WRONG_ARGUMENT_TYPE = 5,

    OPERATOR_WRONG_OPERANDS = 6,

    WRONG_INDEX = 40,

    STEP_ZERO_LEN = 40,
    STEP_WRONG_TYPE = 6,

    INDEX_OUT_OF_BOUNDS = 40,

    GENERAL_LEXICAL = 1,
    GENERAL_SYNTAX = 2,
    GENERAL_SEMANTICS_USE_WITHOUT_DEFINITION = 3,
    GENERAL_SEMANTICS_INVALID_DEFINITION = 4,
    GENERAL_SEMANTICS_WRONG_FUNC_ARGS = 5,
    GENERAL_SEMANTICS_TYPE_INCOMPATIBILITY = 6,
    GENERAL_SEMANTICS_IMMUTABLE_CHANGE_ATTEMPT = 7,
    GENERAL_SEMANTICS_COMMON = 10,

    GENERAL_INTERNAL = 99,

    // Internal use errors
    INTERNAL_MEMORY_ALLOCATION = 200,
    INTERNAL_INDEX_OUT_OF_BOUNDS,
    INTERNAL_INVALID_MEMORY_ALLOCATION_SIZE,
    INTERNAL_END_OF_FILE

} error;

// @brief checks if error is internal
//
// @param err error we want to check
// @return true if is internal, false if not
bool error_is_internal(error err);

#endif
