// @brief Implementation of TAC (Three Address Code)
// @author Oleksandr Fedorenko (xfedoro00)
#include "tac.h"
#include "attribute_table.h"

#include <math.h>
#include <stdarg.h>

#define ADD_INSTR_RETURN_TEMP(op)                                                                                      \
    tac_instruction *new_instruction = create_instruction(err, op, arg, arg2, temp);                                   \
    if (new_instruction == NULL)                                                                                       \
    {                                                                                                                  \
        return (tac_argument){.type = ARG_ERROR};                                                                      \
    }                                                                                                                  \
    append_instruction(prog, new_instruction);                                                                         \
    return temp;

typedef struct tac_prog
{
    tac_list *list;
    unsigned int temporary_var_id;
    unsigned int instruction_id;
} tac_prog;

static void tac_log_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error has occurred in tac module: \n");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// @brief Initialize new tac_list
// @return Pointer to the new initialized list
tac_list *new_tac_list(error *err, tac_prog *prog)
{
    *err = NONE;
    tac_list *list = (tac_list *)utils_safe_malloc(err, sizeof(tac_list));

    if (*err != NONE)
    {
        return NULL;
    }

    list->size = 0;
    list->curr_instruction = NULL;
    list->first_instruction = NULL;
    list->last_instruction = NULL;

    prog->list = list;
    prog->instruction_id = 0;
    prog->temporary_var_id = 0;

    return list;
}

static bool is_empty(tac_list *self)
{
    return self->first_instruction == NULL;
}

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

// @brief Create temporary variable for simple tac instructions
// @return Temporary variable (tn) of type tac_argument
static tac_argument create_temporary_variable(tac_argument_value_type expr_type, tac_prog *prog)
{
    tac_argument new_temporary = {0};
    new_temporary.type = ARG_TEMPORARY_VAR;
    new_temporary.value_type = expr_type;
    new_temporary.temporary_var_id = prog->temporary_var_id;
    prog->temporary_var_id++;
    return new_temporary;
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

static tac_argument tac_expr_emit(error *err, ast_node *expr, tac_prog *prog)
{
    *err = NONE;
    if (expr->type == AST_NODE_LITERAL)
    {
        tac_argument literal = {0};
        switch (expr->literal_type)
        {
        case AST_TYPE_INT:
            literal.const_int = (long long int)attribute_table_get_integer(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_ERROR};
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_INT;
            break;
        case AST_TYPE_DOUBLE:
            literal.const_double = attribute_table_get_double(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_ERROR};
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_FLOAT;
            break;
        case AST_TYPE_STRING:
            literal.const_str = attribute_table_get_string(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_ERROR};
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
            return (tac_argument){.type = ARG_ERROR};
        }
        return literal;
    }
    if (expr->type == AST_NODE_IDENTIFIER)
    {
        tac_argument id = {0};

        id.type = ARG_ID;
        id.value_type = expr->literal_type;
        // We are getting id_name and preserving scope (id_scopeid)
        char *id_name = attribute_table_get_identifier(&attr_table, err, expr->attribute_index);
        if (*err != NONE)
        {
            // TODO: ERROR
            return (tac_argument){.type = ARG_ERROR};
        }
        int new_size = snprintf(NULL, 0, "%s_%ld", id_name, expr->block_index);
        if (new_size < 0)
        {
            *err = GENERAL_INTERNAL;
            // TODO: ERROR
            return (tac_argument){.type = ARG_ERROR};
        }

        id.identifier_name = utils_safe_malloc(err, (size_t)(new_size + 1));
        if (*err != NONE)
        {
            // TODO: ERROR
            return (tac_argument){.type = ARG_ERROR};
        }
        snprintf(id.identifier_name, (size_t)(new_size + 1), "%s_%ld", id_name, expr->block_index);
        return id;
    }
    if (expr->type == AST_NODE_UNARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        tac_argument temp = create_temporary_variable(arg.value_type, prog);
        tac_argument arg2 = {.type = ARG_EMPTY};
        if (expr->operator == AST_OP_NOT)
        {
            tac_instruction *new_instruction = create_instruction(err, TAC_NOT, arg, arg2, temp);
            if (new_instruction == NULL)
            {
                return (tac_argument){.type = ARG_ERROR};
            }
            append_instruction(prog, new_instruction);

            return temp;
        }
        if (expr->operator == AST_OP_MINUS_UNARY)
        {
            tac_instruction *new_instruction = create_instruction(err, TAC_UNARY_MINUS, arg, arg2, temp);
            if (new_instruction == NULL)
            {
                return (tac_argument){.type = ARG_ERROR};
            }
            append_instruction(prog, new_instruction);

            return temp;
        }
    }
    if (expr->type == AST_NODE_BINARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        tac_argument arg2 = tac_expr_emit(err, expr->right_child, prog);

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
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_GREATER_EQ);
        }
        if (expr->operator == AST_OP_LESS)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_LESSER);
        }

        if (expr->operator == AST_OP_LESS_EQUAL)
        {
            tac_argument temp = create_temporary_variable(ARG_BOOL, prog);
            ADD_INSTR_RETURN_TEMP(TAC_LESSER_EQ);
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
        if (expr->operator == AST_OP_NOT_EQUAL)
        {
            tac_argument temp_for_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_argument temp_for_not_eq = create_temporary_variable(ARG_BOOL, prog);
            tac_instruction *eq_instruction = create_instruction(err, TAC_EQUALS, arg, arg2, temp_for_eq);
            if (eq_instruction == NULL)
            {
                return (tac_argument){.type = ARG_ERROR};
            }
            arg2 = (tac_argument){.type = ARG_EMPTY};
            tac_instruction *not_eq_instruction = create_instruction(err, TAC_NOT, temp_for_eq, arg2, temp_for_not_eq);
            if (not_eq_instruction == NULL)
            {
                free(eq_instruction);
                return (tac_argument){.type = ARG_ERROR};
            }
            append_instruction(prog, eq_instruction);
            append_instruction(prog, not_eq_instruction);

            return temp_for_not_eq;
        }
    }
}
