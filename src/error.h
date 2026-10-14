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
    SYMOBL_AFTER_COMMENT = 1,

    DOUBLE_INSTEAD_OF_INT = 6,

    PROLOGUE_MISSING = 2,

    ENTER_FUNC_MISSING = 3,
    ENTER_FUNC_MORE_THEN_ONE = 4,
    ENTER_FUNC_PARAMS_FOR = 5,
    ENTER_FUNC_INVALID_RETURN_TYPE = 6,

    VAR_WRONG_ASSIGN_TYPE = 6,
    VAR_MULTIPLE_DEFINITION = 4,
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

    GENERAL_INTERNAL = 99, // used only as an output error code, is not used inside of modules

    // Internal use errors
    INTERNAL_MEMORY_ALLOCATION = 200,
    INTERNAL_WRONG_NUMERIC_FORMAT,
    INTERNAL_ITEM_ALREADY_EXISTS,
    INTERNAL_INDEX_OUT_OF_BOUNDS,
    INTERNAL_INVALID_MEMORY_ALLOCATION_SIZE,
    INTERNAL_FILE_READ_ERROR,
    INTERNAL_END_OF_FILE, //	not critical error - usually means there is nothing more to read
    //							(so in case we are using function which can return this error we should check for this
    //							particular error and do not propagate it further only stop reading)
    INTERNAL_STACK_IS_EMPTY,
    INTERNAL_NULL_ITEM_INSERT,
} error;

// @brief checks if error is internal
//
// @param err error we want to check
// @return true if is internal, false if not
bool error_is_internal(error err);

#endif
