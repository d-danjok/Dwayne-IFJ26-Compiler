// @brief Implementation for TAC OPTIMIZER
// @author Oleksandr Fedorenko (xfedoro00)

#include "tac_optimiser.h"
#include <math.h>

#define TAC_INTEGER_MAX 9223372036854775807LL
#define TAC_INTEGER_MIN (-TAC_INTEGER_MAX - 1LL)

// @brief Predicate to determine whether argument is variable or not
static bool is_variable(const tac_argument argument)
{
    return argument.type == ARG_ID || argument.type == ARG_TEMPORARY_VAR;
}

// @brief Predicate to determine whether it is the same variable
static bool is_same_variable(const tac_argument first_argument, const tac_argument second_argument)
{
    if (first_argument.type != second_argument.type || first_argument.value_type != second_argument.value_type)
    {
        return false;
    }

    if (first_argument.type == ARG_TEMPORARY_VAR)
    {
        return first_argument.temporary_var_id == second_argument.temporary_var_id;
    }

    return first_argument.type == ARG_ID &&
           strcmp(first_argument.identifier_name, second_argument.identifier_name) == 0;
}

// @brief Same as in TAC
static void free_unconsumed_argument(const tac_argument argument)
{
    if (argument.type == ARG_ID)
    {
        free(argument.identifier_name);
    }
}

// @brief Predicate to detect whether expr is an operation
static bool is_expression_operation(const tac_operator op)
{
    switch (op)
    {
    case TAC_PLUS:
    case TAC_MINUS:
    case TAC_UNARY_MINUS:
    case TAC_MUL:
    case TAC_DIV:
    case TAC_EQUALS:
    case TAC_GREATER:
    case TAC_LESSER:
    case TAC_NOT:
    case TAC_OR:
    case TAC_AND:
        return true;
    default:
        return false;
    }
}

// @brief Function to replace arguments, used for propagation
static bool replace_argument(error *err, tac_argument *destination, tac_argument source)
{
    if (source.type == ARG_ID)
    {
        size_t size = strlen(source.identifier_name) + 1;
        char *copy = utils_safe_malloc(err, size);

        if (*err != NONE)
        {
            return false;
        }

        memcpy(copy, source.identifier_name, size);
        source.identifier_name = copy;
    }

    free_unconsumed_argument(*destination);
    *destination = source;
    return true;
}
// @brief Function to propagate arguments
static bool tac_propagate_argument(error *err, tac_instruction **instructions, size_t block_start, size_t use_index,
                                   tac_argument *argument)
{
    size_t search_limit = use_index; // used for backwards searching
    while (is_variable(*argument))
    {
        bool found_definition = false;
        for (size_t definition_index = search_limit; definition_index > block_start;)
        {
            const tac_instruction *definition_instruction = instructions[--definition_index];

            // skip removed instruction
            if (definition_instruction == NULL)
            {
                continue;
            }
            const tac_operator op = definition_instruction->op;

            if (!is_expression_operation(op) && op != TAC_ASSIGNMENT && op != TAC_VAR_DEF && op != TAC_VAL_DEF &&
                op != TAC_CALL)
            {
                continue;
            }
            if (!is_same_variable(definition_instruction->result, *argument))
            {
                continue;
            }
            if (definition_instruction->op != TAC_ASSIGNMENT && definition_instruction->op != TAC_VAR_DEF &&
                definition_instruction->op != TAC_VAL_DEF)
            {
                return true;
            }
            const tac_argument source_argument = definition_instruction->arg1;
            if (source_argument.type != ARG_LITERAL && !is_variable(source_argument))
            {
                return true;
            }
            if (source_argument.value_type != argument->value_type)
            {
                return true;
            }
            if (is_variable(source_argument))
            {
                for (size_t write_index = definition_index; write_index < use_index; ++write_index)
                {
                    const tac_instruction *write_instruction = instructions[write_index];
                    if (write_instruction == NULL)
                        continue;
                    const tac_operator write_op = write_instruction->op;
                    if ((is_expression_operation(write_op) || write_op == TAC_ASSIGNMENT || write_op == TAC_VAR_DEF ||
                         write_op == TAC_VAL_DEF || write_op == TAC_CALL) &&
                        is_same_variable(write_instruction->result, source_argument))
                        return true;
                }
            }
            if (!replace_argument(err, argument, source_argument))
            {
                return false;
            }
            search_limit = definition_index;
            found_definition = true;
            break;
        }
        if (!found_definition)
        {
            break;
        }
    }
    return true;
}
// @brief Function to fold constant literals
static bool tac_fold_constants(const tac_instruction *instruction, tac_argument *folded_result)
{

    const tac_argument first_argument = instruction->arg1;
    const tac_argument second_argument = instruction->arg2;

    *folded_result = (tac_argument){.type = ARG_LITERAL, .value_type = instruction->result.value_type};

    if (first_argument.type != ARG_LITERAL)
    {
        return false; // cannot fold non_literal
    }

    // Folding of NOT instruction
    if (instruction->op == TAC_NOT && first_argument.value_type == ARG_BOOL && folded_result->value_type == ARG_BOOL)
    {
        folded_result->const_bool = !first_argument.const_bool;
        return true;
    }

    // Folding of TAC_UNARY_MINUS instruction
    if (instruction->op == TAC_UNARY_MINUS && first_argument.value_type == folded_result->value_type)
    {
        if (first_argument.value_type == ARG_INT && first_argument.const_int != TAC_INTEGER_MIN)
        {
            folded_result->const_int = -first_argument.const_int;
            return true;
        }
        if (first_argument.value_type == ARG_FLOAT && isfinite(first_argument.const_double))
        {
            folded_result->const_double = -first_argument.const_double;
            return true;
        }
        return false;
    }

    if (second_argument.type != ARG_LITERAL || first_argument.value_type != second_argument.value_type)
    {
        return false; // cannot fold non_liter or different literal types
    }

    // Constant folding for integer with overflow checks
    if (first_argument.value_type == ARG_INT && folded_result->value_type == ARG_INT)
    {
        const long long left = first_argument.const_int;
        const long long right = second_argument.const_int;

        switch (instruction->op)
        {
        case TAC_PLUS:
            if ((right > 0 && left > TAC_INTEGER_MAX - right) || (right < 0 && left < TAC_INTEGER_MIN - right))
            {
                return false;
            }
            folded_result->const_int = left + right;
            return true;
        case TAC_MINUS:
            if ((right > 0 && left < TAC_INTEGER_MIN + right) || (right < 0 && left > TAC_INTEGER_MAX + right))
            {
                return false;
            }
            folded_result->const_int = left - right;
            return true;
        case TAC_MUL:
            if ((left > 0 && right > 0 && left > TAC_INTEGER_MAX / right) ||
                (left > 0 && right < 0 && right < TAC_INTEGER_MIN / left) ||
                (left < 0 && right > 0 && left < TAC_INTEGER_MIN / right) ||
                (left < 0 && right < 0 && left < TAC_INTEGER_MAX / right))
            {
                return false;
            }
            folded_result->const_int = left * right;
            return true;
        case TAC_DIV:
            if (second_argument.const_int == 0 ||
                (first_argument.const_int == TAC_INTEGER_MIN && second_argument.const_int == -1))
            {
                return false;
            }
            folded_result->const_int = first_argument.const_int / second_argument.const_int;
            return true;
        default:
            break;
        }
    }
    // Constant folding for float numbers
    if (first_argument.value_type == ARG_FLOAT && folded_result->value_type == ARG_FLOAT &&
        isfinite(first_argument.const_double) && isfinite(second_argument.const_double))
    {
        switch (instruction->op)
        {
        case TAC_PLUS:
            folded_result->const_double = first_argument.const_double + second_argument.const_double;
            break;
        case TAC_MINUS:
            folded_result->const_double = first_argument.const_double - second_argument.const_double;
            break;
        case TAC_MUL:
            folded_result->const_double = first_argument.const_double * second_argument.const_double;
            break;
        case TAC_DIV:
            if (second_argument.const_double == 0.0)
            {
                return false;
            }
            folded_result->const_double = first_argument.const_double / second_argument.const_double;
            break;
        default:
            return false;
        }
        return isfinite(folded_result->const_double);
    }

    // Folding of boolean expr
    if (folded_result->value_type != ARG_BOOL)
    {
        return false;
    }
    if (first_argument.value_type == ARG_BOOL)
    {
        switch (instruction->op)
        {
        case TAC_EQUALS:
            folded_result->const_bool = first_argument.const_bool == second_argument.const_bool;
            return true;
        case TAC_AND:
            folded_result->const_bool = first_argument.const_bool && second_argument.const_bool;
            return true;
        case TAC_OR:
            folded_result->const_bool = first_argument.const_bool || second_argument.const_bool;
            return true;
        default:
            return false;
        }
    }
    if (first_argument.value_type == ARG_INT)
    {
        switch (instruction->op)
        {
        case TAC_EQUALS:
            folded_result->const_bool = first_argument.const_int == second_argument.const_int;
            return true;
        case TAC_LESSER:
            folded_result->const_bool = first_argument.const_int < second_argument.const_int;
            return true;
        case TAC_GREATER:
            folded_result->const_bool = first_argument.const_int > second_argument.const_int;
            return true;
        default:
            return false;
        }
    }
    if ((first_argument.value_type == ARG_FLOAT) && (isfinite(first_argument.const_double)) &&
        isfinite(second_argument.const_double))
    {
        switch (instruction->op)
        {
        case TAC_EQUALS:
            folded_result->const_bool = first_argument.const_double == second_argument.const_double;
            return true;
        case TAC_LESSER:
            folded_result->const_bool = first_argument.const_double < second_argument.const_double;
            return true;
        case TAC_GREATER:
            folded_result->const_bool = first_argument.const_double > second_argument.const_double;
            return true;
        default:
            return false;
        }
    }
    return false;
}

static void remove_instruction(tac_instruction **instruction_link)
{
    tac_instruction *instruction = *instruction_link;
    *instruction_link = instruction->next_instruction;
    free_unconsumed_argument(instruction->arg1);
    free_unconsumed_argument(instruction->arg2);
    free_unconsumed_argument(instruction->result);
    free(instruction);
}
// @brief Predicate to detect whether variable is used in instruction before, used for dead-code elimination
static bool is_variable_used(const tac_list *list, tac_argument argument)
{
    for (const tac_instruction *instruction = list->first_instruction; instruction != NULL;
         instruction = instruction->next_instruction)
    {
        if (is_same_variable(argument, instruction->arg1) || is_same_variable(argument, instruction->arg2))
        {
            return true;
        }
        if ((instruction->op == TAC_RETURN || instruction->op == TAC_IF_JUMP) &&
            is_same_variable(argument, instruction->result))
        {
            return true;
        }
    }
    return false;
}

// @brief Function to eliminate dead code
static void tac_eliminate_dead_code(tac_list *list)
{
    bool is_unreachable = false;
    tac_instruction **instruction_link = &list->first_instruction;

    // Traverse of instruction list
    while (*instruction_link != NULL)
    {
        tac_instruction *instruction = *instruction_link;
        if (instruction->op == TAC_LABEL || instruction->op == TAC_MAIN || instruction->op == TAC_FUNC_START ||
            instruction->op == TAC_FUNC_END)
        {
            is_unreachable = false;
        }
        if (is_unreachable && instruction->op != TAC_VAR_DEF && instruction->op != TAC_VAL_DEF)
        {
            remove_instruction(instruction_link);
            continue;
        }
        // Instructions after RETURN or GOTO will be eliminated
        if (instruction->op == TAC_GOTO || instruction->op == TAC_RETURN)
        {
            is_unreachable = true;
        }

        instruction_link = &instruction->next_instruction;
    }

    // Remove temporary var if it is never used
    for (instruction_link = &list->first_instruction; *instruction_link != NULL;)
    {
        tac_instruction *instruction = *instruction_link;
        if ((instruction->op == TAC_ASSIGNMENT) && (instruction->result.type == ARG_TEMPORARY_VAR) &&
            (instruction->arg1.type == ARG_LITERAL) && (!is_variable_used(list, instruction->result)))
        {
            remove_instruction(instruction_link);
        }
        else
        {
            instruction_link = &instruction->next_instruction;
        }
    }
}

// @brief Function to update instruction list size and data
static void update_list_info(tac_list *list)
{
    list->size = 0;
    list->last_instruction = NULL;
    for (tac_instruction *instruction = list->first_instruction; instruction;
         instruction = instruction->next_instruction)
    {
        instruction->id = (unsigned int)list->size++;
        list->last_instruction = instruction;
    }
}

// @brief Function to detect INT_TO_FLOAT instruction
static bool is_conversion_call(const tac_instruction *param, const tac_instruction *call)
{
    return param->op == TAC_CALL_PARAM && call != NULL && call->op == TAC_CALL && call->arg1.identifier_name != NULL &&
           strcmp(call->arg1.identifier_name, "INT_TO_FLOAT") == 0 && call->arg2.type == ARG_LITERAL &&
           call->arg2.value_type == ARG_INT && call->arg2.const_int == 1 && call->result.type == ARG_TEMPORARY_VAR &&
           call->result.value_type == ARG_FLOAT;
}

tac_list *tac_optimise(error *err, tac_list *list)
{
    *err = NONE;
    if (list == NULL)
    {
        *err = GENERAL_INTERNAL;
        return NULL;
    }
    const long instruction_count = list->size;

    // Return empty list, make no optimization
    if (instruction_count == 0)
    {
        update_list_info(list);
        return list;
    }

    tac_instruction **instructions = utils_safe_malloc(err, instruction_count * sizeof(*instructions));
    if (*err != NONE)
    {
        return NULL;
    }

    long index = 0;

    // Create an array of instruction to later work on it
    for (tac_instruction *instruction = list->first_instruction; instruction;
         instruction = instruction->next_instruction)
    {
        instructions[index++] = instruction;
    }

    long block_start = 0;

    // Main loop for optimization
    for (long instruction_index = 0; instruction_index < instruction_count; instruction_index++)
    {
        tac_instruction *instruction = instructions[instruction_index];
        if (instruction == NULL)
        {
            continue; // A folded conversion call was removed from the list.
        }
        tac_instruction *call = instruction->next_instruction;
        const bool conversion = is_conversion_call(instruction, call);
        bool propagation_result = true;

        // Check to reduce INT_TO_FLOAT instruction
        if (conversion)
        {
            propagation_result =
                tac_propagate_argument(err, instructions, block_start, instruction_index, &instruction->arg1);
        }

        // Propagate variables
        else if (is_expression_operation(instruction->op) || instruction->op == TAC_ASSIGNMENT ||
                 instruction->op == TAC_VAR_DEF || instruction->op == TAC_VAL_DEF)
        {
            propagation_result =
                tac_propagate_argument(err, instructions, block_start, instruction_index, &instruction->arg1) &&
                tac_propagate_argument(err, instructions, block_start, instruction_index, &instruction->arg2);
        }
        // Propagate to TAC_RETURN and TAC_IF_JUMP
        else if (instruction->op == TAC_RETURN || instruction->op == TAC_IF_JUMP)
        {
            propagation_result =
                tac_propagate_argument(err, instructions, block_start, instruction_index, &instruction->result);
        }
        if (!propagation_result)
        {
            free(instructions);
            update_list_info(list);
            return NULL;
        }

        // Reduction of TAC_INT_TO_FLOAT
        if (conversion && (instruction->arg1.type == ARG_LITERAL) && (instruction->arg1.value_type == ARG_INT) &&
            (instruction->arg1.const_int >= -9007199254740992LL) && (instruction->arg1.const_int <= 9007199254740992LL))
        {
            instruction->op = TAC_ASSIGNMENT;
            instruction->arg1 = (tac_argument){
                .type = ARG_LITERAL, .value_type = ARG_FLOAT, .const_double = (double)instruction->arg1.const_int};
            instruction->result = call->result;
            instruction->next_instruction =
                call->next_instruction; // Remove TAC_CALL instruction and change it to next after TAC_CALL
            instructions[instruction_index + 1] = NULL; // skip
            free(call);
        }

        // Folding optimization
        tac_argument folded_result;
        if (is_expression_operation(instruction->op) && tac_fold_constants(instruction, &folded_result))
        {
            free_unconsumed_argument(instruction->arg1);
            free_unconsumed_argument(instruction->arg2);

            // New folded instruction
            instruction->op = TAC_ASSIGNMENT;
            instruction->arg1 = folded_result;
            instruction->arg2 = (tac_argument){.type = ARG_EMPTY};
        }

        // If expr is a constant we can simplify and jump straight to block or end
        else if ((instruction->op == TAC_IF_JUMP) && (instruction->result.type == ARG_LITERAL) &&
                 (instruction->result.value_type == ARG_BOOL))
        {
            const tac_argument target_label = instruction->result.const_bool ? instruction->arg1 : instruction->arg2;
            instruction->op = TAC_GOTO;
            instruction->arg1 = (tac_argument){.type = ARG_EMPTY};
            instruction->arg2 = (tac_argument){.type = ARG_EMPTY};
            instruction->result = target_label;
        }

        // Increase boundary so propagation will never changed incorrectly
        if (!is_expression_operation(instruction->op) && instruction->op != TAC_ASSIGNMENT &&
            instruction->op != TAC_VAR_DEF && instruction->op != TAC_VAL_DEF)
        {
            block_start = instruction_index + 1;
        }
    }

    free(instructions);

    tac_eliminate_dead_code(list);
    update_list_info(list);

    return list;
}
