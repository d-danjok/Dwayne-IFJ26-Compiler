// @brief Implementation of TAC (Three Address Code)
// @author Oleksandr Fedorenko (xfedoro00)
#include "tac.h"

#include <math.h>
#include <stdarg.h>

#define ARGUMENT_ERROR                                                                                                 \
    (tac_argument)                                                                                                     \
    {                                                                                                                  \
        .type = ARG_ERROR                                                                                              \
    }
#define ARGUMENT_EMPTY                                                                                                 \
    (tac_argument)                                                                                                     \
    {                                                                                                                  \
        .type = ARG_EMPTY                                                                                              \
    } // sometimes used as success return type

// @brief Main structure of TAC used only in .c module
typedef struct
{
    tac_list *list;
    attribute_table *attr_table;
    unsigned int temporary_var_id;
    unsigned int instruction_id;
    unsigned int label_id;
} tac_prog;

static tac_argument tac_emit_statement(error *err, ast_node *stat, tac_prog *prog);
static tac_argument tac_evaluate_block(error *err, ast_node *block, tac_prog *prog);
static tac_argument tac_expr_emit(error *err, ast_node *expr, tac_prog *prog);
static tac_argument tac_evaluate_param_list(error *err, ast_node *param_list, tac_prog *prog);
static tac_argument tac_evaluate_param_list_(error *err, ast_node *param_list_, tac_prog *prog);
static tac_argument tac_emit_param(error *err, ast_node *param, tac_prog *prog);
static tac_argument tac_evaluate_if_list(error *err, ast_node *if_list, tac_prog *prog);

// @brief Function to free identifier name that have been allocated
static void free_unconsumed_argument(tac_argument arg)
{
    if (arg.type == ARG_ID)
    {
        free(arg.identifier_name);
    }
}

// @brief Macro to create and add instruction to tac_list
#define ADD_INSTR_RETURN_TEMP(op)                                                                                      \
    tac_instruction *new_instruction = create_instruction(err, op, arg, arg2, temp);                                   \
    if (new_instruction == NULL)                                                                                       \
    {                                                                                                                  \
        free_unconsumed_argument(arg);                                                                                 \
        free_unconsumed_argument(arg2);                                                                                \
        return ARGUMENT_ERROR;                                                                                         \
    }                                                                                                                  \
    append_instruction(prog, new_instruction);                                                                         \
    return temp;

// @brief Log function to print errors during TAC process
static void tac_log_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error has occurred in tac module: \n");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// @brief Function that initialize new tac_list and a program
void new_tac_prog(error *err, tac_prog *prog)
{
    *err = NONE;
    tac_list *list = (tac_list *)utils_safe_malloc(err, sizeof(tac_list));

    if (*err != NONE)
        return;

    list->size = 0;
    list->curr_instruction = NULL;
    list->first_instruction = NULL;
    list->last_instruction = NULL;

    prog->list = list;
    prog->instruction_id = 0;
    prog->temporary_var_id = 0;
    prog->label_id = 0;
}
// @brief Predicate for checking if list is empty
// @return true if empty
static bool is_empty(tac_list *self)
{
    return self->first_instruction == NULL;
}

void tac_list_destruct(tac_list *self)
{
    if (self == NULL)
    {
        return;
    }

    tac_instruction *curr = self->first_instruction;
    while (curr != NULL)
    {
        tac_instruction *next = curr->next_instruction;
        free_unconsumed_argument(curr->result);
        free_unconsumed_argument(curr->arg1);
        free_unconsumed_argument(curr->arg2);
        free(curr);
        curr = next;
    }
    free(self);
}

// @brief Function to create a new instzruction (OP, ARG1, ARG2, RESULT)
// @return New instruction tac_instruction
static tac_instruction *create_instruction(error *err, const tac_operator op, const tac_argument arg1,
                                           const tac_argument arg2, const tac_argument result)
{
    *err = NONE;
    tac_instruction *new_ir_node = (tac_instruction *)utils_safe_malloc(err, sizeof(tac_instruction));
    if (*err != NONE)
    {
        return NULL;
    }
    new_ir_node->arg1 = arg1;
    new_ir_node->arg2 = arg2;
    new_ir_node->result = result;
    new_ir_node->op = op;
    new_ir_node->next_instruction = NULL;

    return new_ir_node;
}
// @brief Function to append instruction into existing tac_list
static void append_instruction(tac_prog *prog, tac_instruction *ir_node)
{
    ir_node->id = prog->instruction_id;
    prog->instruction_id++;
    if (is_empty(prog->list))
    {
        prog->list->first_instruction = ir_node;
        prog->list->curr_instruction = ir_node;
        prog->list->last_instruction = ir_node;
        prog->list->size = 1;
        return;
    }

    prog->list->last_instruction->next_instruction = ir_node;
    prog->list->last_instruction = ir_node;

    prog->list->size += 1;
}

tac_instruction *tac_get_curr_instruction(tac_list *self)
{
    if (is_empty(self) || self->curr_instruction == NULL)
    {
        return NULL;
    }
    if (self->size == 1)
    {
        self->curr_instruction = NULL;
        return self->first_instruction;
    }

    tac_instruction *tmp = self->curr_instruction;
    self->curr_instruction = tmp->next_instruction;
    return tmp;
}
tac_instruction *tac_peek_next_instruction(error *err, tac_list *self)
{
    if (is_empty(self))
    {
        return NULL;
    }
    if (self->curr_instruction == NULL)
    {
        return NULL;
    }
    return self->curr_instruction;
}

// @brief Function to create temporary variable for a simple tac instructions
// @return Temporary variable (tn) of type ARG_TEMPORARY_VARIABLE
static tac_argument create_temporary_variable(tac_argument_value_type expr_type, tac_prog *prog)
{
    tac_argument new_temporary = {
        .type = ARG_TEMPORARY_VAR, .value_type = expr_type, .temporary_var_id = prog->temporary_var_id};
    prog->temporary_var_id++;
    return new_temporary;
}
// @brief Function to create label simple tac instructions
// @return Argument of type ARG_LABEL
static tac_argument create_label(tac_prog *prog)
{
    tac_argument new_label = {.type = ARG_LABEL, .value_type = ARG_NONE, .label_id = prog->label_id};
    prog->label_id++;
    return new_label;
}

// @brief Function to create new identifier (id_scope_id)
// @return Argument of type ARG_ID
static tac_argument create_new_id(error *err, attribute_table *attr_table, ast_node *identifier)
{
    tac_argument id_argument = {.type = ARG_ID, .value_type = identifier->literal_type};
    *err = NONE;
    char *id_name = attribute_table_get_identifier(attr_table, err, identifier->attribute_index);
    if (*err != NONE)
    {
        // TODO: ERROR
        return ARGUMENT_ERROR;
    }
    int new_size = snprintf(NULL, 0, "%s_%ld", id_name, identifier->block_index);
    if (new_size < 0)
    {
        *err = GENERAL_INTERNAL;
        // TODO: ERROR
        return ARGUMENT_ERROR;
    }

    id_argument.identifier_name = utils_safe_malloc(err, (size_t)(new_size + 1));
    if (*err != NONE)
    {
        // TODO: ERROR
        return ARGUMENT_ERROR;
    }
    snprintf(id_argument.identifier_name, (size_t)(new_size + 1), "%s_%ld", id_name, identifier->block_index);
    return id_argument;
}

// @brief Wrapper function to create proper assignment argument, can create and expression instruction
// @return Argument of type EXPR if no errors occurred otherwise ERROR
static tac_argument tac_arg_assignment(error *err, ast_node *node, tac_prog *prog)
{
    // Creation of assignment argument
    *err = NONE;
    tac_argument ass_value = tac_expr_emit(err, node->left_child, prog);
    if (ass_value.type == ARG_ERROR || *err != NONE)
    {
        free_unconsumed_argument(ass_value);
        return ARGUMENT_ERROR;
    }
    return ass_value;
}
// @brief Wrapper function to create proper val_def argument(result)
// @return Argument of type ID if no errors occurred otherwise ERROR
static tac_argument tac_arg_val_def(error *err, ast_node *val, tac_prog *prog)
{
    *err = NONE;
    ast_node *id = val->left_child->left_child;
    tac_argument val_variable = create_new_id(err, prog->attr_table, id);
    if (val_variable.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    return val_variable;
}

// @brief Wrapper function to create proper var_def argument (result)
// @return Argument of type ID if no errors occurred otherwise ERROR
static tac_argument tac_arg_var_def(error *err, ast_node *var, tac_prog *prog)
{
    *err = NONE;
    ast_node *id = var->left_child->left_child;
    tac_argument val_variable = create_new_id(err, prog->attr_table, id);
    if (val_variable.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    return val_variable;
}

// @brief Function to create and append PARAM instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_call_param(error *err, ast_node *call_param, tac_prog *prog, long long int *num_of_args)
{
    *err = NONE;
    tac_argument expr = tac_expr_emit(err, call_param->left_child, prog);
    if (expr.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    tac_instruction *call_param_instruction = create_instruction(err, TAC_PARAM, expr, ARGUMENT_EMPTY, ARGUMENT_EMPTY);
    if (call_param_instruction == NULL)
    {
        free_unconsumed_argument(expr);
        return ARGUMENT_ERROR;
    }
    *num_of_args += 1;
    append_instruction(prog, call_param_instruction);
    return ARGUMENT_EMPTY;
}

// @brief Function to evaluatre <call_params'> Node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_call_param_list_(error *err, ast_node *call_param_list_, tac_prog *prog,
                                                  long long int *num_of_args)
{
    *err = NONE;
    if (call_param_list_ == NULL)
    {
        return ARGUMENT_EMPTY;
    }
    // <call_param> Node
    tac_argument result = tac_emit_call_param(err, call_param_list_->left_child, prog, num_of_args);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    // <call_params'> Node
    return tac_evaluate_call_param_list_(err, call_param_list_->right_child, prog, num_of_args);
}

// @brief Function to evaluatre <call_params> Node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_call_param_list(error *err, ast_node *call_param_list, tac_prog *prog,
                                                 long long int *num_of_args)
{
    *err = NONE;
    if (call_param_list->left_child == NULL)
    {
        return ARGUMENT_EMPTY;
    }

    // <call_params> Node
    if (call_param_list->left_child->type == AST_NODE_CALL_PARAMS)
    {
        return tac_evaluate_call_param_list(err, call_param_list->left_child, prog, num_of_args);
    }

    // <call_param> Node
    tac_argument result = tac_emit_call_param(err, call_param_list->left_child, prog, num_of_args);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    // <call_params'> Node
    return tac_evaluate_call_param_list_(err, call_param_list->right_child, prog, num_of_args);
}

// @brief Recursive function to  create instructions of expressions  and append them to tac_list
// @return Argument of instruction
static tac_argument tac_expr_emit(error *err, ast_node *expr, tac_prog *prog)
{
    *err = NONE;
    if (expr->type == AST_NODE_LITERAL)
    {
        tac_argument literal = {0};
        switch (expr->literal_type)
        {
        case AST_TYPE_INT:
            literal.const_int =
                (long long int)attribute_table_get_integer(prog->attr_table, err, expr->attribute_index);
            if (*err != NONE)
            {
                return ARGUMENT_ERROR;
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_INT;
            break;
        case AST_TYPE_DOUBLE:
            literal.const_double = attribute_table_get_double(prog->attr_table, err, expr->attribute_index);
            if (*err != NONE)
            {
                return ARGUMENT_ERROR;
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_FLOAT;
            break;
        case AST_TYPE_STRING:
            literal.const_str = attribute_table_get_string(prog->attr_table, err, expr->attribute_index);
            if (*err != NONE)
            {
                return ARGUMENT_ERROR;
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_STRING;
            break;
        case AST_TYPE_UNIT:
            literal.const_str = NULL;
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_UNIT;
            break;
        case AST_TYPE_BOOLEAN:
            literal.const_bool = expr->attribute_index; // 0 for false / 1 for true
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_BOOL;
            break;
        default:
            tac_log_print("Unknown expression type!\n");
            return ARGUMENT_ERROR;
        }
        return literal;
    }
    if (expr->type == AST_NODE_FUNC_CALL)
    {
        long long int num_of_args = 0;
        tac_argument result_param = tac_evaluate_call_param_list(err, expr->right_child, prog, &num_of_args);

        if (result_param.type == ARG_ERROR || *err != NONE)
        {
            return ARGUMENT_ERROR;
        }

        tac_argument param_number = {.type = ARG_LITERAL, .value_type = ARG_INT, .const_int = num_of_args};
        tac_argument result = create_temporary_variable(expr->left_child->literal_type, prog);

        char *func_name = attribute_table_get_identifier(prog->attr_table, err, expr->left_child->attribute_index);
        if (*err != NONE)
        {
            return ARGUMENT_ERROR;
        }

        tac_argument func_name_argument = {
            .type = ARG_FUNC, .value_type = expr->left_child->literal_type, .identifier_name = func_name};
        tac_instruction *func_call_instruction =
            create_instruction(err, TAC_CALL, func_name_argument, param_number, result);

        if (func_call_instruction == NULL)
        {
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, func_call_instruction);
        return result;
    }
    if (expr->type == AST_NODE_IDENTIFIER)
    {
        tac_argument id = {0};

        id.type = ARG_ID;
        id.value_type = expr->literal_type;

        // We are getting id_name and preserving scope (id_scopeid)
        char *id_name = attribute_table_get_identifier(prog->attr_table, err, expr->attribute_index);
        if (*err != NONE)
        {
            return ARGUMENT_ERROR;
        }
        int new_size = snprintf(NULL, 0, "%s_%ld", id_name, expr->block_index);
        if (new_size < 0)
        {
            *err = GENERAL_INTERNAL;
            return ARGUMENT_ERROR;
        }

        id.identifier_name = utils_safe_malloc(err, (size_t)(new_size + 1));
        if (*err != NONE)
        {
            return ARGUMENT_ERROR;
        }
        snprintf(id.identifier_name, (size_t)(new_size + 1), "%s_%ld", id_name, expr->block_index);
        return id;
    }
    if (expr->type == AST_NODE_UNARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return ARGUMENT_ERROR;
        }
        tac_argument temp = create_temporary_variable(arg.value_type, prog);
        tac_argument arg2 = {.type = ARG_EMPTY};
        if (expr->operator == AST_OP_NOT)
        {
            tac_instruction *new_instruction = create_instruction(err, TAC_NOT, arg, arg2, temp);
            if (new_instruction == NULL)
            {
                free_unconsumed_argument(arg);
                return ARGUMENT_ERROR;
            }
            append_instruction(prog, new_instruction);

            return temp;
        }
        if (expr->operator == AST_OP_MINUS_UNARY)
        {
            tac_instruction *new_instruction = create_instruction(err, TAC_UNARY_MINUS, arg, arg2, temp);
            if (new_instruction == NULL)
            {
                free_unconsumed_argument(arg);
                return ARGUMENT_ERROR;
            }
            append_instruction(prog, new_instruction);

            return temp;
        }
        free_unconsumed_argument(arg);
        return ARGUMENT_ERROR;
    }
    if (expr->type == AST_NODE_BINARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return ARGUMENT_ERROR;
        }
        tac_argument arg2 = tac_expr_emit(err, expr->right_child, prog);
        if (*err != NONE || arg2.type == ARG_ERROR)
        {
            free_unconsumed_argument(arg);
            return ARGUMENT_ERROR;
        }

        // Create additional instruction to clear instruction id properly
        if (expr->operator == AST_OP_GREATER_EQUAL || expr->operator == AST_OP_LESS_EQUAL)
        {
            tac_argument *operands[] = {&arg, &arg2};
            for (int i = 0; i < 2; ++i)
            {
                if (operands[i]->type != ARG_ID)
                {
                    continue;
                }

                tac_argument temp = create_temporary_variable(operands[i]->value_type, prog);
                tac_instruction *copy = create_instruction(err, TAC_ASSIGNMENT, *operands[i], ARGUMENT_EMPTY, temp);
                if (copy == NULL)
                {
                    free_unconsumed_argument(arg);
                    free_unconsumed_argument(arg2);
                    return ARGUMENT_ERROR;
                }
                append_instruction(prog, copy);
                *operands[i] = temp;
            }
        }

        if (expr->operator == AST_OP_PLUS)
        {
            tac_argument temp = create_temporary_variable(arg.value_type, prog);
            ADD_INSTR_RETURN_TEMP(TAC_PLUS);
        }
        if (expr->operator == AST_OP_MINUS)
        {
            tac_argument temp = create_temporary_variable(arg.value_type, prog);
            ADD_INSTR_RETURN_TEMP(TAC_MINUS);
        }
        if (expr->operator == AST_OP_MULTIPLY)
        {
            tac_argument temp = create_temporary_variable(arg.value_type, prog);
            ADD_INSTR_RETURN_TEMP(TAC_MUL);
        }
        if (expr->operator == AST_OP_DIV)
        {
            tac_argument temp = create_temporary_variable(arg.value_type, prog);
            ADD_INSTR_RETURN_TEMP(TAC_DIV);
        }

        if (expr->operator == AST_OP_EQUAL)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_EQUALS);
        }

        if (expr->operator == AST_OP_GREATER)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_GREATER);
        }
        if (expr->operator == AST_OP_GREATER_EQUAL)
        {
            tac_argument temp_gt = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_or = create_temporary_variable(ARG_BOOL, prog);

            tac_instruction *greater = create_instruction(err, TAC_GREATER, arg, arg2, temp_gt);

            if (greater == NULL)
            {
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }

            tac_instruction *equals = create_instruction(err, TAC_EQUALS, arg, arg2, temp_eq);
            if (equals == NULL)
            {
                free(greater);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }
            tac_instruction *or_instr = create_instruction(err, TAC_OR, temp_gt, temp_eq, temp_or);

            if (or_instr == NULL)
            {
                free(equals);
                free(greater);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }

            append_instruction(prog, greater);
            append_instruction(prog, equals);
            append_instruction(prog, or_instr);
            return temp_or;
        }
        if (expr->operator == AST_OP_LESS)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_LESSER);
        }

        if (expr->operator == AST_OP_LESS_EQUAL)
        {
            tac_argument temp_lt = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_or = create_temporary_variable(ARG_BOOL, prog);

            tac_instruction *lesser = create_instruction(err, TAC_LESSER, arg, arg2, temp_lt);

            if (lesser == NULL)
            {
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }

            tac_instruction *equals = create_instruction(err, TAC_EQUALS, arg, arg2, temp_eq);
            if (equals == NULL)
            {
                free(lesser);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }
            tac_instruction *or_instr = create_instruction(err, TAC_OR, temp_lt, temp_eq, temp_or);

            if (or_instr == NULL)
            {
                free(equals);
                free(lesser);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }

            append_instruction(prog, lesser);
            append_instruction(prog, equals);
            append_instruction(prog, or_instr);
            return temp_or;
        }

        if (expr->operator == AST_OP_AND)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_AND);
        }

        if (expr->operator == AST_OP_OR)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_OR);
        }
        if (expr->operator == AST_OP_NOT_EQUAL) // basically divided to t1 = a == b, t2 = !t1
        {
            tac_argument temp_for_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_for_not_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_instruction *eq_instruction = create_instruction(err, TAC_EQUALS, arg, arg2, temp_for_eq);
            if (eq_instruction == NULL)
            {
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }
            tac_argument empty = {.type = ARG_EMPTY};
            tac_instruction *not_eq_instruction = create_instruction(err, TAC_NOT, temp_for_eq, empty, temp_for_not_eq);
            if (not_eq_instruction == NULL)
            {
                free(eq_instruction);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return ARGUMENT_ERROR;
            }
            append_instruction(prog, eq_instruction);
            append_instruction(prog, not_eq_instruction);

            return temp_for_not_eq;
        }
        free_unconsumed_argument(arg);
        free_unconsumed_argument(arg2);
        return ARGUMENT_ERROR;
    }
    if (expr->type == AST_NODE_INT_TO_DOUBLE_CONVERSION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return ARGUMENT_ERROR;
        }
        tac_argument temp = create_temporary_variable(ARG_FLOAT, prog);
        tac_instruction *param = create_instruction(err, TAC_PARAM, arg, ARGUMENT_EMPTY, ARGUMENT_EMPTY);
        if (param == NULL)
        {
            free_unconsumed_argument(arg);
            return ARGUMENT_ERROR;
        }
        tac_argument func = {.type = ARG_FUNC, .value_type = ARG_FLOAT, .identifier_name = "INT_TO_FLOAT"};
        tac_argument count = {.type = ARG_LITERAL, .value_type = ARG_INT, .const_int = 1};
        tac_instruction *call = create_instruction(err, TAC_CALL, func, count, temp);
        if (call == NULL)
        {
            free_unconsumed_argument(arg);
            free(param);
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, param);
        append_instruction(prog, call);
        return temp;
    }
    return ARGUMENT_ERROR; // return error if undefined
}

// @brief Function to create and append a VAL_DEF/VAR_DEF instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_definition(error *err, ast_node *def, tac_prog *prog)
{
    *err = NONE;
    tac_argument unknown_type_definition = {0};

    if (def->left_child->type == AST_NODE_VAL_DEF)
    {
        unknown_type_definition = tac_arg_val_def(err, def->left_child, prog);
    }
    else
    {
        unknown_type_definition = tac_arg_var_def(err, def->left_child, prog);
    }

    if (unknown_type_definition.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    tac_argument ass_value = tac_arg_assignment(err, def->right_child, prog);
    if (ass_value.type == ARG_ERROR || *err != NONE)
    {
        free_unconsumed_argument(unknown_type_definition);
        return ARGUMENT_ERROR;
    }

    if (def->left_child->type == AST_NODE_VAL_DEF)
    {
        tac_instruction *definition_instruction =
            create_instruction(err, TAC_VAL_DEF, ass_value, (tac_argument){.type = ARG_EMPTY}, unknown_type_definition);
        if (definition_instruction == NULL)
        {
            free_unconsumed_argument(unknown_type_definition);
            free_unconsumed_argument(ass_value);
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, definition_instruction);
        return (tac_argument){.type = ARG_EMPTY};
    }
    else
    {
        tac_instruction *definition_instruction =
            create_instruction(err, TAC_VAR_DEF, ass_value, (tac_argument){.type = ARG_EMPTY}, unknown_type_definition);
        if (definition_instruction == NULL)
        {
            free_unconsumed_argument(unknown_type_definition);
            free_unconsumed_argument(ass_value);
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, definition_instruction);
        return (tac_argument){.type = ARG_EMPTY};
    }
    return ARGUMENT_ERROR;
}

// @brief Function to create and append a RETURN instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_return(error *err, ast_node *ret, tac_prog *prog)
{
    *err = NONE;
    tac_argument arg1 = (tac_argument){.type = ARG_EMPTY};
    tac_argument arg2 = (tac_argument){.type = ARG_EMPTY};
    tac_argument result = {.type = ARG_EMPTY};
    if (ret->left_child != NULL)
    {
        result = tac_arg_assignment(err, ret->left_child, prog);
        if (result.type == ARG_ERROR || *err != NONE)
        {
            return ARGUMENT_ERROR;
        }
    }
    tac_instruction *ret_instruction = create_instruction(err, TAC_RETURN, arg1, arg2, result);
    if (ret_instruction == NULL)
    {
        free_unconsumed_argument(result);
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, ret_instruction);
    return (tac_argument){.type = ARG_EMPTY};
}

// @brief Function to create and append a WHILE instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_while(error *err, ast_node *while_node, tac_prog *prog)
{
    *err = NONE;

    tac_argument upper_while_label = create_label(prog);
    tac_argument while_body_label = create_label(prog);
    tac_argument outside_while_label = create_label(prog);

    tac_instruction *upper_label =
        create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, upper_while_label);
    if (upper_label == NULL || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, upper_label);

    tac_argument expr = tac_expr_emit(err, while_node->left_child, prog);
    if (expr.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    tac_instruction *while_instruction =
        create_instruction(err, TAC_IF_JUMP, while_body_label, outside_while_label, expr);
    if (while_instruction == NULL || *err != NONE)
    {
        free_unconsumed_argument(expr);
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, while_instruction);
    tac_instruction *body_label = create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, while_body_label);
    if (body_label == NULL || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, body_label);

    tac_argument result = tac_evaluate_block(err, while_node->right_child, prog);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    tac_instruction *goto_upper_label =
        create_instruction(err, TAC_GOTO, ARGUMENT_EMPTY, ARGUMENT_EMPTY, upper_while_label);
    if (goto_upper_label == NULL || *err != NONE)
    {
        free_unconsumed_argument(result);
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, goto_upper_label);

    tac_instruction *outside_label =
        create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, outside_while_label);
    if (outside_label == NULL || *err != NONE)
    {
        free_unconsumed_argument(result);
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, outside_label);
    return ARGUMENT_EMPTY;
}

// @brief Function to evaluate a statement node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_statement(error *err, ast_node *stat, tac_prog *prog)
{
    *err = NONE;
    if (stat->left_child->type == AST_NODE_IDENTIFIER && stat->right_child != NULL &&
        stat->right_child->type == AST_NODE_ASSIGN_VAL)
    {
        // ASSIGNMENT INSTRUCTION BRANCH
        tac_argument id = create_new_id(err, prog->attr_table, stat->left_child);
        if (id.type == ARG_ERROR || *err != NONE)
        {
            return ARGUMENT_ERROR;
        }

        tac_argument ass_value = tac_arg_assignment(err, stat->right_child, prog);
        if (*err != NONE || ass_value.type == ARG_ERROR)
        {
            free_unconsumed_argument(id);
            return ARGUMENT_ERROR;
        }

        tac_instruction *assignment_instruction =
            create_instruction(err, TAC_ASSIGNMENT, ass_value, ARGUMENT_EMPTY, id);
        if (assignment_instruction == NULL)
        {
            free(id.identifier_name);
            if (ass_value.type == ARG_ID)
            {
                free(ass_value.identifier_name);
            }
            return ARGUMENT_ERROR;
        }

        append_instruction(prog, assignment_instruction);
        return ARGUMENT_EMPTY;
    }

    if (stat->left_child->type == AST_NODE_DEFINITION)
    {
        return tac_emit_definition(err, stat->left_child, prog);
    }
    else if (stat->left_child->type == AST_NODE_WHILE)
    {
        return tac_emit_while(err, stat->left_child, prog);
    }
    else if (stat->left_child->type == AST_NODE_IF_LIST)
    {
        return tac_evaluate_if_list(err, stat->left_child, prog);
    }
    else if (stat->left_child->type == AST_NODE_RETURN)
    {
        return tac_emit_return(err, stat->left_child, prog);
    }
    else
    {
        tac_argument result = tac_expr_emit(err, stat->left_child, prog);
        if (*err != NONE || result.type == ARG_ERROR)
        {
            free_unconsumed_argument(result);
            return ARGUMENT_ERROR;
        }
        free_unconsumed_argument(result);
        return ARGUMENT_EMPTY;
    }
}

// @brief Function to evaluate a statement_list' node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_stat_list_(error *err, ast_node *stat_list_, tac_prog *prog)
{
    *err = NONE;
    if (stat_list_ == NULL)
    {
        return ARGUMENT_EMPTY;
    }
    tac_argument statement = tac_emit_statement(err, stat_list_->left_child, prog);
    if (statement.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    return tac_emit_stat_list_(err, stat_list_->right_child, prog);
}
// @brief Function to evaluate a block node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_block(error *err, ast_node *block, tac_prog *prog)
{
    *err = NONE;
    ast_node *stat_list = block->left_child;
    tac_argument statement = tac_emit_statement(err, stat_list->left_child, prog);
    if (statement.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    tac_argument stat_list_ = tac_emit_stat_list_(err, stat_list->right_child, prog);
    if (stat_list_.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    return ARGUMENT_EMPTY;
};

// @brief Function to create and append PARAM instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_param(error *err, ast_node *param, tac_prog *prog)
{
    *err = NONE;
    tac_argument new_id_name = create_new_id(err, prog->attr_table, param->left_child);
    if (new_id_name.type == ARG_ERROR)
    {
        return ARGUMENT_ERROR;
    }
    tac_instruction *param_instruction =
        create_instruction(err, TAC_PARAM, new_id_name, ARGUMENT_EMPTY, ARGUMENT_EMPTY);
    if (param_instruction == NULL)
    {
        free_unconsumed_argument(new_id_name);
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, param_instruction);
    return ARGUMENT_EMPTY;
}

// @brief Function to evaluate a param_list' node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_param_list_(error *err, ast_node *param_list_, tac_prog *prog)
{
    *err = NONE;
    if (param_list_ == NULL)
    {
        return ARGUMENT_EMPTY;
    }
    // <param> Node
    tac_argument result = tac_emit_param(err, param_list_->left_child, prog);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    // <param_list'> Node
    return tac_evaluate_param_list_(err, param_list_->right_child, prog);
}

// @brief Function to evaluate a param_list node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_param_list(error *err, ast_node *param_list, tac_prog *prog)
{
    *err = NONE;
    if (param_list->left_child == NULL)
    {
        return ARGUMENT_EMPTY;
    }

    // <param_list> Node
    if (param_list->left_child->type == AST_NODE_PARAM_LIST)
    {
        return tac_evaluate_param_list(err, param_list->left_child, prog);
    }

    // <param> Node
    tac_argument result = tac_emit_param(err, param_list->left_child, prog);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    // <param_list'> Node
    return tac_evaluate_param_list_(err, param_list->right_child, prog);
}

// @brief Function to create and append FUNC_BEGIN instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_func_start(error *err, ast_node *func_start, tac_prog *prog)
{
    *err = NONE;

    // func identifier
    char *func_id =
        attribute_table_get_identifier(prog->attr_table, err, func_start->left_child->left_child->attribute_index);
    if (*err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    tac_argument func_id_arg = {
        .type = ARG_FUNC, .value_type = func_start->left_child->left_child->literal_type, .identifier_name = func_id};
    tac_instruction *func_start_instruction =
        create_instruction(err, TAC_FUNC_START, ARGUMENT_EMPTY, ARGUMENT_EMPTY, func_id_arg);
    if (func_start_instruction == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, func_start_instruction);

    // <param_list> Node
    return tac_evaluate_param_list(err, func_start->right_child->left_child, prog);
}

// @brief Function to evaluate func_body node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_func_body(error *err, ast_node *func_body, tac_prog *prog)
{
    *err = NONE;
    if (func_body->left_child->type == AST_NODE_BLOCK)
    {
        return tac_evaluate_block(err, func_body->left_child, prog);
    }
    return tac_expr_emit(err, func_body->left_child, prog);
}

// @brief Function to create and append TAC_MAIN, RETURN, FUNC_END instructions
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_main_func(error *err, ast_node *main_func, tac_prog *prog)
{
    *err = NONE;
    ast_node *func_str_node = main_func->left_child->left_child;
    ast_node *func_body_node = main_func->left_child->right_child;
    ast_node *func_name_node = func_str_node->left_child->left_child;

    char *func_id = attribute_table_get_identifier(prog->attr_table, err, func_name_node->attribute_index);
    if (*err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    tac_argument func_id_arg = {.type = ARG_FUNC,
                                .value_type = (tac_argument_value_type)func_name_node->literal_type,
                                .identifier_name = func_id};

    // TAC_MAIN instruction
    tac_instruction *main_func_instruction =
        create_instruction(err, TAC_MAIN, ARGUMENT_EMPTY, ARGUMENT_EMPTY, func_id_arg);
    if (main_func_instruction == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, main_func_instruction);

    // <func_start> Node
    tac_argument result = tac_emit_func_start(err, func_str_node, prog);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    // <func_body> Node
    tac_argument body_result = tac_evaluate_func_body(err, func_body_node, prog);
    if (body_result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    // RETURN and FUNC_END instructions
    if (func_body_node->left_child->type != AST_NODE_BLOCK)
    {
        tac_instruction *ret = create_instruction(err, TAC_RETURN, ARGUMENT_EMPTY, ARGUMENT_EMPTY, body_result);
        if (ret == NULL)
        {
            free_unconsumed_argument(body_result);
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, ret);
    }

    tac_instruction *func_end = create_instruction(err, TAC_FUNC_END, ARGUMENT_EMPTY, ARGUMENT_EMPTY, func_id_arg);
    if (func_end == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, func_end);
    return ARGUMENT_EMPTY;
}

// @brief Function to create and append RETURN, FUNC_END instructions
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_func(error *err, ast_node *func_def, tac_prog *prog)
{
    *err = NONE;

    ast_node *start = func_def->left_child;
    ast_node *body = func_def->right_child;
    ast_node *id = start->left_child->left_child;

    char *name = attribute_table_get_identifier(prog->attr_table, err, id->attribute_index);
    if (*err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    tac_argument func_id = {
        .type = ARG_FUNC, .value_type = (tac_argument_value_type)id->literal_type, .identifier_name = name};

    // <func_start> Node
    tac_argument result = tac_emit_func_start(err, start, prog);
    if (*err != NONE || result.type == ARG_ERROR)
    {
        return ARGUMENT_ERROR;
    }

    // <func_body> Node
    tac_argument body_result = tac_evaluate_func_body(err, body, prog);
    if (*err != NONE || body_result.type == ARG_ERROR)
    {
        return ARGUMENT_ERROR;
    }

    // RETURN and FUNC_END instructions
    if (body->left_child->type != AST_NODE_BLOCK)
    {
        tac_instruction *ret = create_instruction(err, TAC_RETURN, ARGUMENT_EMPTY, ARGUMENT_EMPTY, body_result);
        if (ret == NULL)
        {
            free_unconsumed_argument(body_result);
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, ret);
    }

    tac_instruction *end = create_instruction(err, TAC_FUNC_END, ARGUMENT_EMPTY, ARGUMENT_EMPTY, func_id);
    if (end == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, end);
    return ARGUMENT_EMPTY;
}

// @brief Function to evaluate func_list node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_func_list(error *err, ast_node *def_list, tac_prog *prog)
{
    *err = NONE;
    tac_argument result = {0};

    if (def_list == NULL)
    {
        return ARGUMENT_EMPTY;
    }

    //  <main_func> || <func_def> Node
    if (def_list->left_child->type == AST_NODE_MAIN_FUNC_DEF)
    {
        result = tac_emit_main_func(err, def_list->left_child, prog);
    }
    else
    {
        result = tac_emit_func(err, def_list->left_child, prog);
    }

    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    // <def_list> Node
    return tac_evaluate_func_list(err, def_list->right_child, prog);
}

// @brief Function to create and append IF_JUMP instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_instruction *tac_emit_if(error *err, ast_node *if_node, tac_prog *prog, tac_argument false_label,
                                    tac_argument end_label)
{
    *err = NONE;

    //<expression> Node
    tac_argument expr_arg = tac_expr_emit(err, if_node->left_child, prog);

    if (expr_arg.type == ARG_ERROR || *err != NONE)
    {
        return NULL;
    }

    tac_argument start_label = create_label(prog);
    tac_instruction *if_instruction = create_instruction(err, TAC_IF_JUMP, start_label, false_label, expr_arg);
    if (if_instruction == NULL)
    {
        free_unconsumed_argument(expr_arg);
        return NULL;
    }

    append_instruction(prog, if_instruction);
    tac_instruction *start_label_instruction =
        create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, start_label);

    if (start_label_instruction == NULL)
    {
        return NULL;
    }

    append_instruction(prog, start_label_instruction);
    // <block> Node
    tac_argument block_arg = tac_evaluate_block(err, if_node->right_child, prog);

    if (block_arg.type == ARG_ERROR || *err != NONE)
    {
        return NULL;
    }

    // Goto end_label so no further else if statements will be added
    tac_instruction *skip_instruction = create_instruction(err, TAC_GOTO, ARGUMENT_EMPTY, ARGUMENT_EMPTY, end_label);

    if (skip_instruction == NULL)
    {
        return NULL;
    }

    append_instruction(prog, skip_instruction);
    return if_instruction;
}

// @brief Function to create and append GOTO instruction
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_emit_else(error *err, ast_node *else_node, tac_prog *prog, tac_argument false_label,
                                  tac_argument end_label)
{
    *err = NONE;
    // else <block> Node
    if (else_node->left_child->type == AST_NODE_BLOCK)
    {
        tac_argument result = tac_evaluate_block(err, else_node->left_child, prog);
        if (result.type == ARG_ERROR || *err != NONE)
        {
            return ARGUMENT_ERROR;
        }

        tac_instruction *skip_instruction =
            create_instruction(err, TAC_GOTO, ARGUMENT_EMPTY, ARGUMENT_EMPTY, end_label);
        if (skip_instruction == NULL)
        {
            return ARGUMENT_ERROR;
        }
        append_instruction(prog, skip_instruction);
        return ARGUMENT_EMPTY;
    }

    // else <if> Node
    tac_instruction *result = tac_emit_if(err, else_node->left_child, prog, false_label, end_label);
    if (result == NULL || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    return ARGUMENT_EMPTY;
}

// @brief Function to evaluate <if_list'> Node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_if_list_(error *err, ast_node *if_list_, tac_prog *prog, tac_argument branch_label,
                                          tac_argument end_label)
{
    *err = NONE;
    if (if_list_ == NULL)
    {
        return ARGUMENT_EMPTY;
    }

    tac_instruction *branch_instruction =
        create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, branch_label);
    if (branch_instruction == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, branch_instruction);

    tac_argument false_label = end_label;
    if (if_list_->right_child != NULL)
    {
        false_label = create_label(prog);
    }
    // <else> Node
    tac_argument else_arg = tac_emit_else(err, if_list_->left_child, prog, false_label, end_label);
    if (else_arg.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }
    // <if_list'> Node
    return tac_evaluate_if_list_(err, if_list_->right_child, prog, false_label, end_label);
}

// @brief Function to evaluate <if_list> node
// @return Argument of type EMPTY if no errors occurred otherwise ERROR
static tac_argument tac_evaluate_if_list(error *err, ast_node *if_list, tac_prog *prog)
{
    *err = NONE;
    tac_argument end_label = create_label(prog);
    tac_argument false_label = end_label; // if else is not exist jump straight to end
    if (if_list->right_child != NULL)
    {
        false_label = create_label(prog);
    }

    // <if> Node
    tac_instruction *if_stm = tac_emit_if(err, if_list->left_child, prog, false_label, end_label);
    if (if_stm == NULL || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    // <if_list'> Node
    tac_argument result = tac_evaluate_if_list_(err, if_list->right_child, prog, false_label, end_label);
    if (result.type == ARG_ERROR || *err != NONE)
    {
        return ARGUMENT_ERROR;
    }

    tac_instruction *end_instruction = create_instruction(err, TAC_LABEL, ARGUMENT_EMPTY, ARGUMENT_EMPTY, end_label);
    if (end_instruction == NULL)
    {
        return ARGUMENT_ERROR;
    }
    append_instruction(prog, end_instruction);
    return ARGUMENT_EMPTY;
}

// @brief External function to run an TAC module
// @return Pointer to list of TAC instructions
tac_list *tac_generate(error *err, ast_node **root, attribute_table *attr_table)
{
    *err = NONE;
    tac_prog prog = {0};
    prog.attr_table = attr_table;

    new_tac_prog(err, &prog);
    if (*err != NONE)
    {
        tac_log_print("Error occurred during creation of TAC Program!\n");
        ast_node_destruct_recursively(root);
        return NULL;
    }
    tac_argument result = tac_evaluate_func_list(err, *root, &prog);
    ast_node_destruct_recursively(root);

    if (result.type == ARG_ERROR || *err != NONE)
    {
        tac_log_print("Error occurred during creation of TAC instructions!\n");
        tac_list_destruct(prog.list);

        // If error happened in ARG_ERROR
        if (*err == NONE)
        {
            *err = GENERAL_INTERNAL;
        }

        return NULL;
    }
    return prog.list;
}
