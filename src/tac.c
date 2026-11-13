// @brief Implementation of TAC (Three Address Code)
// @author Oleksandr Fedorenko (xfedoro00)
#include "tac.h"
#include "attribute_table.h"

#include <math.h>
#include <stdarg.h>

// @brief Main structure of TAC used only in .c module
typedef struct
{
    tac_list *list;
    unsigned int temporary_var_id;
    unsigned int instruction_id;
    unsigned int label_id;
} tac_prog;

// @brief Enum of patch_target (where to insert label)
typedef enum
{
    PATCH_ARG1,
    PATCH_ARG2,
    PATCH_RESULT,
} patch_target;

// @brief Structure of single backpatch entry
typedef struct
{
    tac_instruction *instruction;
    patch_target target;
} tac_backpatch_entry;

// @brief Structure of backpatch list (Dynamic Array)
typedef struct
{
    tac_backpatch_entry *instruction_list;
    unsigned int count;
    unsigned int capacity;
} patch_list;

// @brief Function to initialize a backpatch list
static void init_backpatch_list(error *err, patch_list *self)
{
    *err = NONE;
    self->capacity = 128;
    tac_backpatch_entry *backpatch_list = utils_safe_malloc(err, sizeof(tac_backpatch_entry) * self->capacity);
    if (*err != NONE)
        return;
    self->count = 0;
    self->instruction_list = backpatch_list;
}

// @brief Function to append an instruction to existing backpatch list, reallocates array in the list if array is full
static void append_to_backpatch_list(error *err, tac_instruction *instruction, patch_list *p_list, patch_target target)
{
    *err = NONE;
    tac_backpatch_entry new_entry = {.instruction = instruction, .target = target};
    if (p_list->capacity <= p_list->count)
    {
        tac_backpatch_entry *backpatch_list =
            realloc(p_list->instruction_list, sizeof(tac_backpatch_entry) * p_list->capacity * 2);
        if (backpatch_list == NULL)
        {
            *err = INTERNAL_MEMORY_ALLOCATION;
            return;
        };
        p_list->instruction_list = backpatch_list;
        p_list->capacity = p_list->capacity * 2;
    }

    p_list->instruction_list[p_list->count] = new_entry;
    p_list->count++;
}

static void backpatch_fix(tac_backpatch_entry *entry, tac_argument label)
{
    if (entry->target == PATCH_ARG1)
    {
        entry->instruction->arg1 = label;
        return;
    }
    if (entry->target == PATCH_ARG2)
    {
        entry->instruction->arg2 = label;
        return;
    }
    if (entry->target == PATCH_RESULT)
    {
        entry->instruction->result = label;
    }
}
// @brief Function to destroy backpatch list
static void backpatch_list_destruct(patch_list *self)
{
    free(self->instruction_list);
}

// @brief Function to free identifier storage that has not been stored in an appended instruction.
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
        return (tac_argument){.type = ARG_ERROR};                                                                      \
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

// @brief Function to create a new instruction (OP, ARG1, ARG2, RESULT)
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
        return (tac_argument){.type = ARG_ERROR};
    }
    int new_size = snprintf(NULL, 0, "%s_%ld", id_name, identifier->block_index);
    if (new_size < 0)
    {
        *err = GENERAL_INTERNAL;
        // TODO: ERROR
        return (tac_argument){.type = ARG_ERROR};
    }

    id_argument.identifier_name = utils_safe_malloc(err, (size_t)(new_size + 1));
    if (*err != NONE)
    {
        // TODO: ERROR
        return (tac_argument){.type = ARG_ERROR};
    }
    snprintf(id_argument.identifier_name, (size_t)(new_size + 1), "%s_%ld", id_name, identifier->block_index);
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
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return (tac_argument){.type = ARG_ERROR};
        }
        tac_argument temp = create_temporary_variable(arg.value_type, prog);
        tac_argument arg2 = {.type = ARG_EMPTY};
        if (expr->operator == AST_OP_NOT)
        {
            tac_instruction *new_instruction = create_instruction(err, TAC_NOT, arg, arg2, temp);
            if (new_instruction == NULL)
            {
                free_unconsumed_argument(arg);
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
                free_unconsumed_argument(arg);
                return (tac_argument){.type = ARG_ERROR};
            }
            append_instruction(prog, new_instruction);

            return temp;
        }
        free_unconsumed_argument(arg);
        return (tac_argument){.type = ARG_ERROR};
    }
    if (expr->type == AST_NODE_BINARY_OPERATION)
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return (tac_argument){.type = ARG_ERROR};
        }
        tac_argument arg2 = tac_expr_emit(err, expr->right_child, prog);
        if (*err != NONE || arg2.type == ARG_ERROR)
        {
            free_unconsumed_argument(arg);
            return (tac_argument){.type = ARG_ERROR};
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
                return (tac_argument){.type = ARG_ERROR};
            }

            tac_instruction *equals = create_instruction(err, TAC_EQUALS, arg, arg2, temp_eq);
            if (equals == NULL)
            {
                free(greater);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return (tac_argument){.type = ARG_ERROR};
            }
            tac_instruction *or_instr = create_instruction(err, TAC_OR, temp_gt, temp_eq, temp_or);

            if (or_instr == NULL)
            {
                free(equals);
                free(greater);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return (tac_argument){.type = ARG_ERROR};
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
                return (tac_argument){.type = ARG_ERROR};
            }

            tac_instruction *equals = create_instruction(err, TAC_EQUALS, arg, arg2, temp_eq);
            if (equals == NULL)
            {
                free(lesser);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return (tac_argument){.type = ARG_ERROR};
            }
            tac_instruction *or_instr = create_instruction(err, TAC_OR, temp_lt, temp_eq, temp_or);

            if (or_instr == NULL)
            {
                free(equals);
                free(lesser);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return (tac_argument){.type = ARG_ERROR};
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
                return (tac_argument){.type = ARG_ERROR};
            }
            tac_argument empty = {.type = ARG_EMPTY};
            tac_instruction *not_eq_instruction = create_instruction(err, TAC_NOT, temp_for_eq, empty, temp_for_not_eq);
            if (not_eq_instruction == NULL)
            {
                free(eq_instruction);
                free_unconsumed_argument(arg);
                free_unconsumed_argument(arg2);
                return (tac_argument){.type = ARG_ERROR};
            }
            append_instruction(prog, eq_instruction);
            append_instruction(prog, not_eq_instruction);

            return temp_for_not_eq;
        }
        free_unconsumed_argument(arg);
        free_unconsumed_argument(arg2);
        return (tac_argument){.type = ARG_ERROR};
    }
    if (expr->type == AST_NODE_INT_TO_DOUBLE_CONVERSION) // TODO: CONVERT TO FUNCTION
    {
        tac_argument arg = tac_expr_emit(err, expr->left_child, prog);
        if (*err != NONE || arg.type == ARG_ERROR)
        {
            return (tac_argument){.type = ARG_ERROR};
        }
        tac_argument arg2 = {.type = ARG_EMPTY};
        tac_argument temp = create_temporary_variable(ARG_FLOAT, prog);
        tac_instruction *new_instruction = create_instruction(err, TAC_INT_TO_FLOAT, arg, arg2, temp);
        if (new_instruction == NULL)
        {
            free_unconsumed_argument(arg);
            return (tac_argument){.type = ARG_ERROR};
        }
        append_instruction(prog, new_instruction);
        return temp;
    }
    return (tac_argument){.type = ARG_ERROR}; // return error if undefined
}

tac_argument tac_emit_block(error *err, ast_node *block, tac_prog *prog) {

};

tac_argument tac_emit_assignment(error *err, ast_node *node, tac_prog *prog)
{
    // ASSIGNMENT INSTRUCTION BRANCH
    *err = NONE;
    tac_argument ass_value = tac_expr_emit(err, node->left_child, prog);
    if (ass_value.type == ARG_ERROR || *err != NONE)
    {
        free_unconsumed_argument(ass_value);
        return (tac_argument){.type = ARG_ERROR};
    }
    return ass_value;
}

tac_argument tac_emit_val_def(error *err, ast_node *val, tac_prog *prog)
{
    *err = NONE;
    ast_node *id = val->left_child->left_child;
    tac_argument val_variable = {.type = id->type, .value_type = id->literal_type, .identifier_name = };
}

tac_argument tac_emit_definition(error *err, ast_node *def, tac_prog *prog)
{
    *err = NONE;
    tac_argument unknown_type_definition = {0};

    if (def->left_child->type == AST_NODE_VAL_DEF)
    {
        unknown_type_definition = tac_emit_val_def(err, def->left_child, prog);
    }
    else
    {
        unknown_type_definition = tac_emit_var_def(err, def->left_child, prog);
    }

    if (unknown_type_definition.type == ARG_ERROR || *err != NONE)
    {
        // TODO: ERROR
        return (tac_argument){.type = ARG_ERROR};
    }

    tac_argument ass_value = tac_emit_assignment(err, def->right_child, prog);
    if (ass_value.type == ARG_ERROR || *err != NONE)
    {
        // TODO: ERROR
        free_unconsumed_argument(unknown_type_definition);
        return (tac_argument){.type = ARG_ERROR};
    }

    if (def->left_child->type == AST_NODE_VAL_DEF)
    {
        tac_instruction *definition_instruction =
            create_instruction(err, TAC_VAL_DEF, ass_value, (tac_argument){.type = ARG_EMPTY}, unknown_type_definition);
        if (definition_instruction == NULL)
        {
            // TODO: ERROR
            free_unconsumed_argument(unknown_type_definition);
            free_unconsumed_argument(ass_value);
            return (tac_argument){.type = ARG_ERROR};
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
            // TODO: ERROR
            free_unconsumed_argument(unknown_type_definition);
            free_unconsumed_argument(ass_value);
            return (tac_argument){.type = ARG_ERROR};
        }
        append_instruction(prog, definition_instruction);
        return (tac_argument){.type = ARG_EMPTY};
    }
    return (tac_argument){.type = ARG_ERROR};
}

eturn id_argument;
}

tac_argument tac_emit_statement(error *err, ast_node *stat, tac_prog *prog)
{
    *err = NONE;
    if (stat->left_child->type == AST_NODE_IDENTIFIER && stat->right_child != NULL &&
        stat->right_child->type == AST_NODE_ASSIGN_VAL)
    {

        // ASSIGNMENT INSTRUCTION BRANCH
        tac_argument id = create_new_id(err, attr, stat->left_child);
        if (id.type == ARG_ERROR || *err != NONE)
        {
            return (tac_argument){.type = ARG_ERROR};
        }

        tac_argument ass_value = tac_emit_assignment(err, stat->right_child, prog);
        if (*err != NONE || ass_value.type == ARG_ERROR)
        {
            free_unconsumed_argument(id);
            return (tac_argument){.type = ARG_ERROR};
        }

        tac_instruction *assignment_instruction =
            create_instruction(err, TAC_ASSIGNMENT, ass_value, (tac_argument){.type = ARG_EMPTY}, id);
        if (assignment_instruction == NULL)
        {
            free(id.identifier_name);
            if (ass_value.type == ARG_ID)
            {
                free(ass_value.identifier_name);
            }
            return (tac_argument){.type = ARG_ERROR};
        }

        append_instruction(prog, assignment_instruction);
        return (tac_argument){.type = ARG_EMPTY};
    }

    if (stat->left_child->type == AST_NODE_DEFINITION)
    {
        // TODO: IMPLEMENT
    }
    else if (stat->left_child->type == AST_NODE_WHILE)
    {
        // TODO: IMPLEMENT
    }
    else if (stat->left_child->type == AST_NODE_IF_LIST)
    {
        // TODO: IMPLEMENT
    }
    else if (stat->left_child->type == AST_NODE_RETURN)
    {
        // TODO: IMPLEMENT
    }
    else
    {
        tac_argument result = tac_expr_emit(err, stat->left_child, prog);
    }
}
void tac_if_emit(error *err, ast_node *node, tac_prog *prog)
{

    *err = NONE;
    patch_list *true_labels = NULL;
    patch_list *false_labels = NULL;
    patch_list *res_labels = NULL;
    init_backpatch_list(err, true_labels);
    if (*err != NONE)
    {
        // TODO: IMPLEMENT
    }
    init_backpatch_list(err, false_labels);
    if (*err != NONE)
    {
        // TODO: IMPLEMENT
    }
    init_backpatch_list(err, res_labels);
    if (*err != NONE)
    {
        // TODO: IMPLEMENT
    }
    tac_argument expr = tac_expr_emit(err, node->left_child, prog);
    if (*err != NONE)
    {
        // TODO: IMPLEMENT
    }

    tac_instruction *if_stm = create_instruction(err, TAC_IF_JUMP, (tac_argument){.type = ARG_EMPTY},
                                                 (tac_argument){.type = ARG_EMPTY}, expr);
    append_to_backpatch_list(err, if_stm, true_labels, PATCH_ARG1);
    append_to_backpatch_list(err, if_stm, false_labels, PATCH_ARG2);
}

tac_list *tac_generate(error *err, ast_node *node)
{
}
