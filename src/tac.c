// @brief Implementation of TAC (Three Address Code)
// @author Oleksandr Fedorenko (xfedoro00)
#include "tac.h"
#include "attribute_table.h"
tac_list *new_tac_list(error *err)
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

static void append_instruction(tac_list *self, tac_instruction *ir_node)
{
    static unsigned int id = 0;
    ir_node->id = id;
    if (is_empty(self))
    {
        self->first_instruction = ir_node;
        self->curr_instruction = ir_node;
        self->last_instruction = ir_node;
        self->size = 1;
        return;
    }

    self->last_instruction->next_instruction = ir_node;
    self->last_instruction = ir_node;

    self->size += 1;
    id++;
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

static tac_argument tac_expr_emit(error *err, ast_node *expr)
{
    if (expr->type == AST_NODE_LITERAL)
    {
        tac_argument literal = {0};
        switch (expr->literal_type)
        {
        case AST_TYPE_INT:
            literal.const_int = (long long int)attribute_table_get_integer(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_NONE};
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_INT;
            break;
        case AST_TYPE_DOUBLE:
            literal.const_double = attribute_table_get_double(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_NONE};
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_FLOAT;
            break;
        case AST_TYPE_STRING:
            literal.const_str = attribute_table_get_string(&attr_tabl, err, expr->attribute_index);
            if (*err != NONE)
            {
                return (tac_argument){.type = ARG_NONE};
            }
            literal.type = ARG_LITERAL;
            literal.value_type = ARG_STRING;
            break;
        case AST_TYPE_UNIT:
            literal.unit = NULL;
            literal.type = ARG_UNIT;
            literal.value_type = ARG_UNIT;
            break;
        case AST_TYPE_BOOLEAN:
            literal.const_bool = expr->attribute_index; // 0 for false / 1 for true
            literal.type = ARG_LITERAL;
            literal.value_type = NONE;
            break;
        default:
            // TODO: REPORT ERROR
        }
        return literal;
    }
    if (expr->type == AST_NODE_IDENTIFIER)
    {
        tac_argument id = {0};

        id.type = ARG_ID;
        id.value_type = expr->literal_type;
        // We are getting id_name and preserving scope (id_scopeid)
        char* id_name =attribute_table_get_identifier(&attr_table, err, expr->attribute_index);
        if (*err != NONE)
        {
            // TODO: ERROR
            return (tac_argument){.type = ARG_NONE};
        }
        int new_size = snprintf(NULL, 0, "%s_%ld",id_name,expr->block_index);
        if (new_size <0)
        {
            *err = GENERAL_INTERNAL;
            // TODO: ERROR
            return (tac_argument){.type = ARG_NONE};
        }

        id.identifier_name = utils_safe_malloc(err, (size_t)(new_size + 1));
        if (*err != NONE)
        {
            // TODO: ERROR
            return (tac_argument){.type = ARG_NONE};

        }
        snprintf(id.identifier_name, (size_t)(new_size + 1), "%s_%ld", id_name, expr->block_index);
        return id;
    }
    if (expr->type == AST_NODE_UNARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child);
        if (*err != NONE)
        {
            // TODO: ERROR
            return (tac_argument){.type = ARG_NONE};
        }

    }
}

static tac_argument create_temporary_variable(tac_argument_value_type expr_type)
{
    static unsigned int id = 0;
    tac_argument new_temporary = {0};
    new_temporary.type = ARG_TEMPORARY_VAR;
    new_temporary.value_type = expr_type;
    new_temporary.temporary_var_id =id;
    id++;
    return new_temporary;
}


