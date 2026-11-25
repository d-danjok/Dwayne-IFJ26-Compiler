// @file    semantic_analyser.c
//
// @author  Vladyslav Doroshenko (xdorosv00)

#include "semantic_analyser.h"

#include <string.h>

// section to battle strange things in code
#define AST_NODE_PARAM_LIST_R AST_NODE_PARAM_LIST_
#define AST_NODE_STAT_LIST_R AST_NODE_STAT_LIST_
#define AST_NODE_IF_LIST_R AST_NODE_IF_LIST_
#define AST_NODE_CALL_PARAMS_R AST_NODE_CALL_PARAMS_

// block index of the scope with function parameters (parser numbers real blocks from 1)
// Implies shadowing of function parameters by function local variables
// Can be changed later
#define PARAMS_BLOCK_INDEX 0

// current scope with depth
typedef struct
{
    symtable *table;
    long block_index;
} scope;

// needed to properly parse
// 1. recursion
// 2. functions with no return type
// 3. functions with definition below function call
typedef enum
{
    FUNC_NOT_CHECKED,
    FUNC_IN_PROGRESS,
    FUNC_CHECKED,
} function_state;

typedef struct
{
    int name_id;
    ast_node *definition; // FUNC_DEF node, NULL for builtins
    bool is_main;
    bool has_declared_return_type; // recursion is allowed only with declared return type
    ast_literal_type return_type;
    int arg_count;
    ast_literal_type *arg_types;
    function_state state;
} function_info;

// shared state of the whole analysis
typedef struct
{
    function_info *functions;
    int function_count;
    function_info *current_function;
    stack *scopes;
    error *err;
    attribute_table *attr_tab; // for names of builtin functions
    bool *calls;               // call graph: calls[caller * function_count + callee]
} sem_context;

// signature of a builtin function, AST_TYPE_NONE as a parameter type means Any
typedef struct
{
    const char *name;
    int arg_count;
    ast_literal_type arg_types[3];
    ast_literal_type return_type;
} builtin_function;

static const builtin_function builtins[] = {
    {"print", 1, {AST_TYPE_NONE}, AST_TYPE_UNIT},
    {"readLine", 0, {AST_TYPE_NONE}, AST_TYPE_STRING},
    {"readInt", 0, {AST_TYPE_NONE}, AST_TYPE_INT},
    {"readDouble", 0, {AST_TYPE_NONE}, AST_TYPE_DOUBLE},
    {"readBoolean", 0, {AST_TYPE_NONE}, AST_TYPE_BOOLEAN},
    {"toStr", 1, {AST_TYPE_NONE}, AST_TYPE_STRING},
    {"toInt", 1, {AST_TYPE_DOUBLE}, AST_TYPE_INT},
    {"strLength", 1, {AST_TYPE_STRING}, AST_TYPE_INT},
    {"subStr", 3, {AST_TYPE_STRING, AST_TYPE_INT, AST_TYPE_INT}, AST_TYPE_STRING},
    {"ord", 2, {AST_TYPE_STRING, AST_TYPE_INT}, AST_TYPE_INT},
    {"chr", 1, {AST_TYPE_INT}, AST_TYPE_STRING},
};
#define BUILTIN_COUNT ((int)(sizeof(builtins) / sizeof(builtins[0])))

// ==================- HELPER FUNCTIONS -==================
//                - FUNCTION FOR FUNCTIONS -
static bool is_numeric(ast_literal_type type);
static void insert_int_to_double(sem_context *ctx, ast_node **ast_int_node);
static void check_condition(sem_context *ctx, ast_node *condition);
static void check_if_list(sem_context *ctx, const ast_node *if_list);
// ==================- HELPER FUNCTIONS -==================

// ==================- Stack and Symtable Functions -==================
static void scopes_stack_destruct(stack **scopes);
static void scope_push(stack *scopes, error *err, long block_index);
static void scope_pop(stack *scopes, error *err);
static void scope_declare_id(const stack *scopes, error *err, int identifier_id, identifier_data **data);
static identifier_data *scope_lookup(const stack *scopes, error *err, int identifier_id, long *block_index);
static void declare_variable(const sem_context *ctx, ast_node *id_node, identifier_type id_type, ast_literal_type type);
// ==================- Stack and Symtable Functions -==================

// ==================- First run functions -==================
static void register_function(sem_context *ctx, const ast_node *def_list);
static void check_main(const sem_context *ctx);
// ==================- First run functions -==================

// ==================- Function calls -==================
static function_info *find_function(const sem_context *ctx, int name_id);
static const builtin_function *find_builtin(const sem_context *ctx, int name_id);
static void check_call_arguments(sem_context *ctx, ast_node *call_params, int arg_count,
                                 const ast_literal_type *arg_types);
static ast_literal_type check_call(sem_context *ctx, ast_node *call);
static void check_recursion(sem_context *ctx);
static bool reaches_function(const sem_context *ctx, bool *visited, int from, int target);
// ==================- Function calls -==================

// ==================- Return and function result -==================
static void check_return(sem_context *ctx, ast_node *return_node);
static ast_node *last_statement(const ast_node *block);
static bool block_always_returns(const ast_node *block);
static bool if_list_always_returns(const ast_node *if_list);
static bool block_result_is_unit(const ast_node *block);
static bool if_list_result_is_unit(const ast_node *if_list);
static void insert_implicit_return(sem_context *ctx, ast_node *statement);
static void check_function_result(sem_context *ctx, function_info *func, ast_node *func_body);
// ==================- Return and function result -==================

// ==================- Second run functions -==================
static void require_type(sem_context *ctx, ast_node **parent_node, ast_literal_type expected, ast_literal_type actual,
                         error mismatch_error);
static void check_definition(sem_context *ctx, ast_node *definition_node);
static ast_literal_type check_binary_operation(sem_context *ctx, ast_node *expression);
static ast_literal_type check_unary_operation(sem_context *ctx, ast_node *expression);
static ast_literal_type check_expression(sem_context *ctx, ast_node *expression);
static void check_statement(sem_context *ctx, ast_node *statement_def);
static void check_block(sem_context *ctx, const ast_node *block);
static ast_literal_type check_function(sem_context *ctx, function_info *func);
static void check_all_bodies(sem_context *ctx);
static identifier_data *use_variable(const sem_context *ctx, ast_node *var);
// ==================- Second run functions -==================

ast_node *semantic_analyser_run(error *err, ast_node **parser_tree, attribute_table *attr_tab);

static bool is_numeric(const ast_literal_type type)
{
    return type == AST_TYPE_INT || type == AST_TYPE_DOUBLE;
}

static void insert_int_to_double(sem_context *ctx, ast_node **ast_int_node)
{
    ast_node *int_to_double = new_ast_node(ctx->err);
    if (*ctx->err != NONE)
        return;

    int_to_double->type = AST_NODE_INT_TO_DOUBLE_CONVERSION;
    int_to_double->literal_type = AST_TYPE_DOUBLE;
    int_to_double->src_line_number = (*ast_int_node)->src_line_number;

    int_to_double->left_child = *ast_int_node;
    *ast_int_node = int_to_double;
}

// check condition type in the loop, must always be boolean
static void check_condition(sem_context *ctx, ast_node *condition)
{
    const ast_literal_type condition_type = check_expression(ctx, condition);
    if (*ctx->err != NONE)
        return;

    if (condition_type != AST_TYPE_BOOLEAN)
        *ctx->err = GENERAL_SEMANTICS_TYPE_INCOMPATIBILITY;
}

// if / else if / else: every branch with a condition gets check_condition, every branch gets its own block
static void check_if_list(sem_context *ctx, const ast_node *if_list)
{
    //                IF_LIST
    //     L: IF                  R: IF_LIST_ or NULL (no more branches)
    // L: condition  R: BLOCK     L: ELSE                    R: IF_LIST_ or NULL
    //                            L: IF (else if) or BLOCK (else)
    const ast_node *branch = if_list->left_child; // first branch is always IF
    const ast_node *rest = if_list->right_child;

    while (*ctx->err == NONE)
    {
        if (branch->type == AST_NODE_IF)
        {
            check_condition(ctx, branch->left_child);
            if (*ctx->err != NONE)
                return;
            check_block(ctx, branch->right_child);
        }
        else // else: BLOCK without a condition
        {
            check_block(ctx, branch);
        }

        if (rest == NULL)
            return; // that was the last branch

        branch = rest->left_child->left_child; // IF_LIST_ -> ELSE -> IF or BLOCK
        rest = rest->right_child;
    }
}

static void scopes_stack_destruct(stack **scopes)
{
    if (*scopes == NULL)
        return;

    error err = NONE;
    while (true)
    {
        scope *top_scope = stack_pop(*scopes, &err);
        if (err != NONE)
            break; // INTERNAL_STACK_IS_EMPTY err

        symtable_destruct(&top_scope->table);
        UTILS_SAFE_FREE(&top_scope);
    }
    stack_destruct(scopes); // stack is empty now, free the stack itself
}

static void scope_push(stack *scopes, error *err, const long block_index)
{
    scope *new_scope = utils_safe_malloc(err, sizeof(scope));
    if (*err != NONE)
        return;

    new_scope->block_index = block_index;
    new_scope->table = new_symtable(err);
    if (*err != NONE)
    {
        UTILS_SAFE_FREE(&new_scope);
        return;
    }

    stack_push(scopes, err, (void **)&new_scope);
    if (*err != NONE)
    {
        symtable_destruct(&new_scope->table);
        UTILS_SAFE_FREE(&new_scope);
    }
}

static void scope_pop(stack *scopes, error *err)
{
    scope *top_scope = stack_pop(scopes, err);
    if (*err != NONE)
        return;

    symtable_destruct(&top_scope->table);
    UTILS_SAFE_FREE(&top_scope);
}

// inserts identifier into the symtable
static void scope_declare_id(const stack *scopes, error *err, const int identifier_id, identifier_data **data)
{
    const scope *current_scope = stack_peek(scopes, err, 0);
    if (*err != NONE || current_scope == NULL)
    {
        UTILS_SAFE_FREE(data);
        return;
    }

    if (symtable_search(current_scope->table, identifier_id))
    {
        *err = VAR_MULTIPLE_DEFINITION;
        UTILS_SAFE_FREE(data);
        return;
    }

    symtable_insert(current_scope->table, err, identifier_id, data);
    if (*err != NONE)
        UTILS_SAFE_FREE(data);
}

static identifier_data *scope_lookup(const stack *scopes, error *err, const int identifier_id, long *block_index)
{
    const int stack_size = stack_get_item_count(scopes);

    for (int depth = 0; depth < stack_size; depth++)
    {
        const scope *current_scope = stack_peek(scopes, err, depth);
        if (*err != NONE || current_scope == NULL)
            return NULL;

        identifier_data *id_data = symtable_getdata(current_scope->table, identifier_id);
        if (id_data != NULL)
        {
            *block_index = current_scope->block_index;
            return id_data;
        }
    }
    *err = VAR_USE_OUT_OF_SCOPE;
    return NULL;
}

// puts a variable into the innermost scope and writes its block index and type into the IDENTIFIER node
static void declare_variable(const sem_context *ctx, ast_node *id_node, const identifier_type id_type,
                             const ast_literal_type type)
{
    identifier_data *data = NEW_IDENTIFIER_DATA(ctx->err, 0);
    if (*ctx->err != NONE)
        return;

    data->value_type = (data_type)type; // symtable stores data_type, enum values are the same
    data->type = id_type;
    data->function_arg_count = 0;

    scope_declare_id(ctx->scopes, ctx->err, id_node->attribute_index, &data); // takes ownership of data
    if (*ctx->err != NONE)
        return;

    const scope *current_scope = stack_peek(ctx->scopes, ctx->err, 0);
    if (*ctx->err != NONE)
        return;

    // for TAC
    id_node->block_index = current_scope->block_index;
    id_node->literal_type = type;
}

static void register_function(sem_context *ctx, const ast_node *def_list)
{
    // we passed DEF_LIST
    // left func_def -> func_start -> func_name -> identifier
    // right def_list (self)

    ast_node *func_def = def_list->left_child;
    const ast_node *func_start = func_def->left_child;
    const ast_node *func_name = func_start->left_child;
    const ast_node *func_head = func_start->right_child;

    const int func_attribute_index = func_name->left_child->attribute_index; // key: FUNC_CALL finds the function by it
    const ast_node *func_param_list = func_head->left_child;                 // first ever param_list

    const bool has_return_type = func_head->right_child != NULL;
    const ast_literal_type return_type = has_return_type ? func_head->right_child->literal_type : AST_TYPE_NONE;

    // checking if the same name defined twice
    // builtin function can't be redefined
    const bool is_builtin_name = find_builtin(ctx, func_attribute_index) != NULL;
    if (*ctx->err != NONE)
        return;

    if (is_builtin_name || find_function(ctx, func_attribute_index) != NULL)
    {
        *ctx->err = FUNC_MULTIPLE_DEFINITION;
        return;
    }

    // count function parameters
    // function without parameters has a single PARAM_LIST with no children
    int arg_count = 0;
    for (const ast_node *item = func_param_list; item != NULL && item->left_child != NULL; item = item->right_child)
        arg_count++;

    // allocate memory for argument types
    ast_literal_type *arg_types = NULL;
    if (arg_count > 0)
    {
        arg_types = utils_safe_malloc(ctx->err, (size_t)arg_count * sizeof(ast_literal_type));
        if (*ctx->err != NONE)
            return;
    }

    // copy argument types
    // FUNC_HEAD --LEFT-> func_param_list
    // func_param_list --LEFT-> PARAM
    // func_param_list --RIGHT-> PARAM_LIST_R
    int arg_index = 0;
    for (const ast_node *item = func_param_list; item != NULL && item->left_child != NULL; item = item->right_child)
    {
        const ast_node *param = item->left_child; // PARAM -> L: IDENTIFIER, R: TYPE
        arg_types[arg_index++] = param->right_child->literal_type;
    }

    // Fill the function context array in the order
    // functions appear in the code top to bottom
    // starting from index 0
    function_info *info = &ctx->functions[ctx->function_count];
    info->name_id = func_attribute_index;
    info->definition = func_def;
    info->is_main = func_def->type == AST_NODE_MAIN_FUNC_DEF;
    info->has_declared_return_type = has_return_type;
    info->return_type = return_type;
    info->arg_count = arg_count;
    info->arg_types = arg_types;
    info->state = FUNC_NOT_CHECKED;

    ctx->function_count++;
}

static void check_main(const sem_context *ctx)
{
    const function_info *main_func = NULL;
    for (int i = 0; i < ctx->function_count; i++)
    {
        if (!ctx->functions[i].is_main)
            continue;

        if (main_func != NULL)
        {
            *ctx->err = ENTER_FUNC_MORE_THEN_ONE;
            return;
        }
        main_func = &ctx->functions[i];
    }

    if (main_func == NULL)
    {
        *ctx->err = ENTER_FUNC_MISSING;
        return;
    }
    if (main_func->arg_count != 0)
    {
        *ctx->err = ENTER_FUNC_PARAMS_FOR;
        return;
    }
    // without declared return type is inferred in pass 2, so it can be checked only there
    if (main_func->has_declared_return_type && main_func->return_type != AST_TYPE_UNIT)
        *ctx->err = ENTER_FUNC_INVALID_RETURN_TYPE;
}

// check type compatibility
static void require_type(sem_context *ctx, ast_node **parent_node, const ast_literal_type expected,
                         const ast_literal_type actual, const error mismatch_error)
{
    if (expected == actual)
        return;

    if (expected == AST_TYPE_DOUBLE && actual == AST_TYPE_INT)
    {
        insert_int_to_double(ctx, parent_node);
        return;
    }

    *ctx->err = mismatch_error;
}

static void check_definition(sem_context *ctx, ast_node *definition_node)
{

    ast_node *assign_val = definition_node->right_child;
    ast_node *def_body = definition_node->left_child->left_child;
    ast_node *id_node = def_body->left_child;
    ast_node *type_notation = def_body->right_child;

    const identifier_type id_type =
        definition_node->left_child->type == AST_NODE_VAL_DEF ? ID_TYPE_CONSTANT : ID_TYPE_VARIABLE;

    // val x = x + 1
    ast_literal_type value_type = check_expression(ctx, assign_val->left_child);
    if (*ctx->err != NONE)
        return;

    // no type written.
    // variable gets the type of the value
    ast_literal_type var_type = value_type;
    if (type_notation != NULL)
    {
        var_type = type_notation->left_child->literal_type;
        require_type(ctx, &assign_val->left_child, var_type, value_type, VAR_WRONG_ASSIGN_TYPE);
        if (*ctx->err != NONE)
            return;
    }

    declare_variable(ctx, id_node, id_type, var_type);
}

static void check_assignment(sem_context *ctx, ast_node *statement)
{
    ast_node *identifier = statement->left_child;
    ast_node *assign_val = statement->right_child;

    identifier_data *id_data = use_variable(ctx, identifier);

    if (*ctx->err != NONE)
        return;

    ast_literal_type assign_expression_type = check_expression(ctx, assign_val->left_child);
    if (*ctx->err != NONE)
        return;

    /*
     * a = 5            int to int                      | require_type
     * a = a + 1        old value                       | require_type
     * d = 1            int to double                   | require_type
     * a = 1.5          cannot convert double to int    | require_type
     * a = "s"          variable type is immutable      | require_type
     * a = 1 < 2        boolean into int                | require_type
     * c = 2            constants are immutable         | first if
     * p = 2            parameters are immutable        | first if
     * zz = 1           var doesn't exist               | scope_lookup undefined error
     * a = zz           var doesn't exist               | check_expression -> use_var -> error undefined
     */

    //  TODO: IMPORTANT! Check error order
    //  zz = a -> error 3 or 7? We need to check error priority

    if (id_data->type == ID_TYPE_CONSTANT)
    {
        *ctx->err = GENERAL_SEMANTICS_IMMUTABLE_CHANGE_ATTEMPT;
        return;
    }

    require_type(ctx, &assign_val->left_child, (ast_literal_type)id_data->value_type, assign_expression_type,
                 VAR_WRONG_ASSIGN_TYPE);
}

static function_info *find_function(const sem_context *ctx, const int name_id)
{
    for (int i = 0; i < ctx->function_count; i++)
    {
        if (ctx->functions[i].name_id == name_id)
            return &ctx->functions[i];
    }
    return NULL;
}

static const builtin_function *find_builtin(const sem_context *ctx, const int name_id)
{
    const char *name = attribute_table_get_identifier(ctx->attr_tab, ctx->err, name_id);
    if (*ctx->err != NONE)
        return NULL;

    for (int i = 0; i < BUILTIN_COUNT; i++)
    {
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    }
    return NULL;
}

// wrong count or type of arguments -> error 5, AST_TYPE_NONE as a parameter type means Any
static void check_call_arguments(sem_context *ctx, ast_node *call_params, const int arg_count,
                                 const ast_literal_type *arg_types)
{
    //          CALL_PARAMS (without children for f())
    //    L: CALL_PARAM         R: CALL_PARAMS_R -> ...
    // L: expression
    int arg_index = 0;
    for (ast_node *item = call_params; item != NULL && item->left_child != NULL; item = item->right_child)
    {
        if (arg_index >= arg_count)
        {
            *ctx->err = FUNC_WRONG_ARGUMENT_COUNT;
            return;
        }

        ast_node *call_param = item->left_child;
        const ast_literal_type arg_type = check_expression(ctx, call_param->left_child);
        if (*ctx->err != NONE)
            return;

        if (arg_types[arg_index] != AST_TYPE_NONE)
        {
            require_type(ctx, &call_param->left_child, arg_types[arg_index], arg_type, FUNC_WRONG_ARGUMENT_TYPE);
            if (*ctx->err != NONE)
                return;
        }
        arg_index++;
    }

    if (arg_index != arg_count)
        *ctx->err = FUNC_WRONG_ARGUMENT_COUNT;
}

// user function with unknown return type is checked first (a "jump")
static ast_literal_type check_call(sem_context *ctx, ast_node *call)
{
    //            FUNC_CALL
    // L: IDENTIFIER (name)     R: CALL_PARAMS
    ast_node *name = call->left_child;
    ast_literal_type result = AST_TYPE_NONE;

    function_info *func = find_function(ctx, name->attribute_index);
    if (func == NULL)
    {
        const builtin_function *builtin = find_builtin(ctx, name->attribute_index);
        if (*ctx->err != NONE)
            return AST_TYPE_NONE;

        if (builtin == NULL)
        {
            *ctx->err = GENERAL_SEMANTICS_USE_WITHOUT_DEFINITION;
            return AST_TYPE_NONE;
        }

        check_call_arguments(ctx, call->right_child, builtin->arg_count, builtin->arg_types);
        result = builtin->return_type;
    }
    else
    {
        check_call_arguments(ctx, call->right_child, func->arg_count, func->arg_types);
        if (*ctx->err != NONE)
            return AST_TYPE_NONE;

        // for check_recursion
        const int caller_index = (int)(ctx->current_function - ctx->functions);
        const int callee_index = (int)(func - ctx->functions);
        ctx->calls[caller_index * ctx->function_count + callee_index] = true;

        if (func->has_declared_return_type || func->state == FUNC_CHECKED)
            result = func->return_type;
        else if (func->state == FUNC_IN_PROGRESS)
            *ctx->err = FUNC_WRONG_RETURN_TYPE; // recursion requires an explicit return type
        else
            result = check_function(ctx, func); // jump
    }

    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    // for TAC
    name->literal_type = result;
    call->literal_type = result;
    return result;
}

// every function in a call cycle needs an explicit return type
static void check_recursion(sem_context *ctx)
{
    const int count = ctx->function_count;
    bool *visited = utils_safe_malloc(ctx->err, (size_t)count * sizeof(bool));
    if (*ctx->err != NONE)
        return;

    for (int i = 0; i < count && *ctx->err == NONE; i++)
    {
        if (ctx->functions[i].has_declared_return_type)
            continue;

        memset(visited, 0, (size_t)count * sizeof(bool));
        if (reaches_function(ctx, visited, i, i))
            *ctx->err = FUNC_WRONG_RETURN_TYPE;
    }

    UTILS_SAFE_FREE(&visited);
}

static bool reaches_function(const sem_context *ctx, bool *visited, const int from, const int target)
{
    for (int to = 0; to < ctx->function_count; to++)
    {
        if (!ctx->calls[from * ctx->function_count + to])
            continue;
        if (to == target)
            return true;
        if (visited[to])
            continue;

        visited[to] = true;
        if (reaches_function(ctx, visited, to, target))
            return true;
    }
    return false;
}

// return anywhere in the body needs an explicit return type
static void check_return(sem_context *ctx, ast_node *return_node)
{
    //          RETURN
    // L: ASSIGN_VAL or NULL (bare return)
    // L: expression
    const function_info *func = ctx->current_function;
    if (!func->has_declared_return_type)
    {
        *ctx->err = FUNC_WRONG_RETURN_TYPE;
        return;
    }

    ast_node *assign_val = return_node->left_child;
    if (assign_val == NULL)
    {
        // bare return only in a function declared as Unit
        if (func->return_type != AST_TYPE_UNIT)
            *ctx->err = FUNC_WRONG_RETURN_TYPE;
        return;
    }

    const ast_literal_type value_type = check_expression(ctx, assign_val->left_child);
    if (*ctx->err != NONE)
        return;

    // in a Unit function the value is thrown away
    if (func->return_type != AST_TYPE_UNIT)
        require_type(ctx, &assign_val->left_child, func->return_type, value_type, FUNC_WRONG_RETURN_TYPE);
}

// last statement written directly in the block, not in its sub-blocks
static ast_node *last_statement(const ast_node *block)
{
    const ast_node *stat_list = block->left_child;
    while (stat_list->right_child != NULL)
        stat_list = stat_list->right_child;
    return stat_list->left_child;
}

static bool block_always_returns(const ast_node *block)
{
    const ast_node *last = last_statement(block);
    if (last->right_child != NULL)
        return false; // assignment

    if (last->left_child->type == AST_NODE_RETURN)
        return true;
    if (last->left_child->type == AST_NODE_IF_LIST)
        return if_list_always_returns(last->left_child);
    return false;
}

// missing else does not return
static bool if_list_always_returns(const ast_node *if_list)
{
    const ast_node *branch = if_list->left_child;
    const ast_node *rest = if_list->right_child;

    while (true)
    {
        const ast_node *block = branch->type == AST_NODE_IF ? branch->right_child : branch;
        if (!block_always_returns(block))
            return false;

        if (branch->type != AST_NODE_IF)
            return true; // else
        if (rest == NULL)
            return false; // no else

        branch = rest->left_child->left_child; // IF_LIST_ -> ELSE -> IF or BLOCK
        rest = rest->right_child;
    }
}

static bool block_result_is_unit(const ast_node *block)
{
    const ast_node *last = last_statement(block);
    if (last->right_child != NULL)
        return true; // assignment

    switch (last->left_child->type)
    {
    case AST_NODE_DEFINITION:
    case AST_NODE_WHILE:
        return true;

    case AST_NODE_RETURN:
        return false;

    case AST_NODE_IF_LIST:
        return if_list_result_is_unit(last->left_child);

    default: // expression
        return last->left_child->literal_type == AST_TYPE_UNIT;
    }
}

static bool if_list_result_is_unit(const ast_node *if_list)
{
    const ast_node *branch = if_list->left_child;
    const ast_node *rest = if_list->right_child;

    while (true)
    {
        const ast_node *block = branch->type == AST_NODE_IF ? branch->right_child : branch;
        if (!block_result_is_unit(block))
            return false;

        if (branch->type != AST_NODE_IF || rest == NULL)
            return true;

        branch = rest->left_child->left_child; // IF_LIST_ -> ELSE -> IF or BLOCK
        rest = rest->right_child;
    }
}

// STATEMENT -> L: expression   becomes   STATEMENT -> L: RETURN -> L: ASSIGN_VAL -> L: expression
static void insert_implicit_return(sem_context *ctx, ast_node *statement)
{
    ast_node *assign_val = new_ast_node(ctx->err);
    if (*ctx->err != NONE)
        return;

    ast_node *return_node = new_ast_node(ctx->err);
    if (*ctx->err != NONE)
    {
        ast_node_destruct_recursively(&assign_val);
        return;
    }

    assign_val->type = AST_NODE_ASSIGN_VAL;
    assign_val->src_line_number = statement->src_line_number;
    assign_val->left_child = statement->left_child;

    return_node->type = AST_NODE_RETURN;
    return_node->src_line_number = statement->src_line_number;
    return_node->left_child = assign_val;

    statement->left_child = return_node;
}

static void check_function_result(sem_context *ctx, function_info *func, ast_node *func_body)
{
    //      FUNC_BODY
    // L: BLOCK or expression
    const bool declared = func->has_declared_return_type;

    if (func_body->left_child->type != AST_NODE_BLOCK)
    {
        const ast_literal_type value_type = func_body->left_child->literal_type;
        if (!declared)
            func->return_type = value_type;
        else if (func->return_type != AST_TYPE_UNIT)
            require_type(ctx, &func_body->left_child, func->return_type, value_type, FUNC_WRONG_RETURN_TYPE);
        return;
    }

    ast_node *last = last_statement(func_body->left_child);
    const ast_node_type last_type = last->right_child != NULL ? AST_NODE_ASSIGN_VAL : last->left_child->type;

    if (last_type == AST_NODE_RETURN)
        return; // already checked by check_return

    if (last_type == AST_NODE_IF_LIST)
    {
        if (declared && func->return_type == AST_TYPE_UNIT)
            return;
        if (declared && if_list_always_returns(last->left_child))
            return;
        if (!declared && if_list_result_is_unit(last->left_child))
        {
            func->return_type = AST_TYPE_UNIT;
            return;
        }
        *ctx->err = FUNC_WRONG_RETURN_TYPE;
        return;
    }

    const bool ends_with_expression =
        last_type != AST_NODE_ASSIGN_VAL && last_type != AST_NODE_DEFINITION && last_type != AST_NODE_WHILE;
    const ast_literal_type result = ends_with_expression ? last->left_child->literal_type : AST_TYPE_UNIT;

    if (!declared)
        func->return_type = result;
    else if (func->return_type != AST_TYPE_UNIT)
    {
        if (!ends_with_expression)
        {
            *ctx->err = FUNC_WRONG_RETURN_TYPE;
            return;
        }
        require_type(ctx, &last->left_child, func->return_type, result, FUNC_WRONG_RETURN_TYPE);
        if (*ctx->err != NONE)
            return;
    }

    if (ends_with_expression && func->return_type != AST_TYPE_UNIT)
        insert_implicit_return(ctx, last);
}

static ast_literal_type check_binary_operation(sem_context *ctx, ast_node *expression)
{
    ast_node *binary_op = expression;

    ast_literal_type left_literal = check_expression(ctx, binary_op->left_child);
    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    ast_literal_type right_literal = check_expression(ctx, binary_op->right_child);
    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    ast_literal_type result = AST_TYPE_NONE; // stays NONE if the operand types do not fit
    const bool both_numeric = is_numeric(left_literal) && is_numeric(right_literal);
    switch (binary_op->operator)
    {
    case AST_OP_PLUS:
        if (left_literal == AST_TYPE_STRING && right_literal == AST_TYPE_STRING)
        {
            result = AST_TYPE_STRING;
            break;
        }
        /* fall through */
    case AST_OP_MINUS:
    case AST_OP_MULTIPLY:
    case AST_OP_DIV:
        // two numbers with the same type -> that same type
        // two numbers with different types -> double
        if (!is_numeric(left_literal) || !is_numeric(right_literal))
            break;
        if (left_literal == right_literal)
            result = left_literal; // int+int -> int, double+double -> double
        else
        {
            if (left_literal == AST_TYPE_INT)
                insert_int_to_double(ctx, &binary_op->left_child);
            else
                insert_int_to_double(ctx, &binary_op->right_child);

            if (*ctx->err != NONE)
                return AST_TYPE_NONE;

            result = AST_TYPE_DOUBLE;
        }
        break;

    case AST_OP_LESS:
    case AST_OP_LESS_EQUAL:
    case AST_OP_GREATER:
    case AST_OP_GREATER_EQUAL:
        // two numbers with the same type or two values of the same type -> boolean
        // two numbers with different types -> (adding conversion node to int) -> double
        if (left_literal == AST_TYPE_STRING && right_literal == AST_TYPE_STRING)
        {
            result = AST_TYPE_BOOLEAN;
            break;
        }
        if (both_numeric && left_literal != right_literal)
        {
            if (left_literal == AST_TYPE_INT)
                insert_int_to_double(ctx, &binary_op->left_child);
            else
                insert_int_to_double(ctx, &binary_op->right_child);
            if (*ctx->err != NONE)
                return AST_TYPE_NONE;
            result = AST_TYPE_BOOLEAN;
            break;
        }
        if (both_numeric && left_literal == right_literal)
        {
            result = AST_TYPE_BOOLEAN;
            break;
        }
        break;

    case AST_OP_EQUAL:
    case AST_OP_NOT_EQUAL:
        // two numbers or two values of the same type -> boolean
        if (both_numeric && left_literal != right_literal)
        {
            if (left_literal == AST_TYPE_INT)
                insert_int_to_double(ctx, &binary_op->left_child);
            else
                insert_int_to_double(ctx, &binary_op->right_child);

            if (*ctx->err != NONE)
                return AST_TYPE_NONE;

            result = AST_TYPE_BOOLEAN;
            break;
        }

        // two values of the same type: Int, Double, String, Boolean, Unit
        if (left_literal == right_literal)
            result = AST_TYPE_BOOLEAN;
        break;

    case AST_OP_AND:
    case AST_OP_OR:
        if (left_literal == AST_TYPE_BOOLEAN && right_literal == AST_TYPE_BOOLEAN)
            result = AST_TYPE_BOOLEAN;
        break;

    default:
        break;
    }

    if (result == AST_TYPE_NONE)
    {
        *ctx->err = OPERATOR_WRONG_OPERANDS;
        return AST_TYPE_NONE;
    }

    binary_op->literal_type = result;
    return result;
}

static ast_literal_type check_unary_operation(sem_context *ctx, ast_node *expression)
{
    //                  statement
    //          definition
    //                  assign_val
    //            expression
    //
    // in our case always unary
    ast_node *unary_op = expression;
    const ast_literal_type operand_type = check_expression(ctx, unary_op->left_child);
    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    ast_literal_type result = AST_TYPE_NONE;
    switch (unary_op->operator)
    {
    case AST_OP_MINUS_UNARY:
        if (operand_type != AST_TYPE_INT && operand_type != AST_TYPE_DOUBLE)
        {
            result = AST_TYPE_NONE;
            break;
        }
        result = operand_type == AST_TYPE_INT ? AST_TYPE_INT : AST_TYPE_DOUBLE;
        break;

    case AST_OP_NOT:
        if (operand_type != AST_TYPE_BOOLEAN)
        {
            result = AST_TYPE_NONE;
            break;
        }
        result = AST_TYPE_BOOLEAN;
        break;

    default:
        break;
    }

    if (result == AST_TYPE_NONE)
    {
        *ctx->err = OPERATOR_WRONG_OPERANDS;
        return AST_TYPE_NONE;
    }

    unary_op->literal_type = result;
    return result;
}

// finds out the type of an expression and writes it into its nodes (TAC reads them)
// returns AST_TYPE_NONE after an error
static ast_literal_type check_expression(sem_context *ctx, ast_node *expression)
{
    switch (expression->type)
    {
    case AST_NODE_LITERAL:
        return expression->literal_type;

    case AST_NODE_IDENTIFIER:
        use_variable(ctx, expression);
        if (*ctx->err != NONE)
            return AST_TYPE_NONE;
        return expression->literal_type;

    case AST_NODE_FUNC_CALL:
        return check_call(ctx, expression);

    case AST_NODE_UNARY_OPERATION:
        return check_unary_operation(ctx, expression);

    case AST_NODE_BINARY_OPERATION:
        return check_binary_operation(ctx, expression);

    default:
        return AST_TYPE_NONE;
    }
}

static void check_statement(sem_context *ctx, ast_node *statement_def)
{
    //            x = expr
    // L: IDENTIFIER     R: ASSIGN_VAL

    if (statement_def->right_child != NULL)
    {
        check_assignment(ctx, statement_def);
        return;
    }

    ast_node *inner = statement_def->left_child;

    switch (inner->type)
    {
    case AST_NODE_DEFINITION:
        check_definition(ctx, inner);
        break;

    case AST_NODE_WHILE:
        check_condition(ctx, inner->left_child);
        if (*ctx->err != NONE)
            return;
        check_block(ctx, inner->right_child);
        break;

    case AST_NODE_IF_LIST:
        check_if_list(ctx, inner);
        break;

    case AST_NODE_RETURN:
        check_return(ctx, inner);
        break;

    default: // plain expression: print(x), f(1)
        check_expression(ctx, statement_def->left_child);
        break;
    }
}

// opens a new scope for the block, checks every statement in it and closes the scope
// so variables declared inside the block are visible only until its end
static void check_block(sem_context *ctx, const ast_node *block)
{
    scope_push(ctx->scopes, ctx->err, block->block_index);
    if (*ctx->err != NONE)
        return;

    //            BLOCK
    //      L: STAT_LIST
    // L: STATEMENT     R: STAT_LIST_R -> ...
    for (const ast_node *stat_list = block->left_child; *ctx->err == NONE && stat_list != NULL;
         stat_list = stat_list->right_child)
    {
        check_statement(ctx, stat_list->left_child);
    }

    if (*ctx->err != NONE)
        return; // the scope is freed by scopes_stack_destruct in check_function

    scope_pop(ctx->scopes, ctx->err);
}

// pass 2: checks the body of one function
// called from check_all_bodies and later also from a call of a not yet checked function (a "jump"),
// that is why it saves and restores the state of whoever called it
// returns the declared or inferred return type, AST_TYPE_NONE after an error
static ast_literal_type check_function(sem_context *ctx, function_info *func)
{
    // we need to remember the state of the caller
    // so we can use "jump" to parse another function while parsing the body of the caller
    stack *caller_scopes = ctx->scopes;
    function_info *caller = ctx->current_function;

    func->state = FUNC_IN_PROGRESS;
    ctx->current_function = func;
    ctx->scopes = new_stack(ctx->err); // empty: variables of the caller must stay invisible
    if (*ctx->err != NONE)
    {
        // "pop" the caller's scope and return
        ctx->scopes = caller_scopes;
        ctx->current_function = caller;
        return AST_TYPE_NONE;
    }

    //                      FUNC_DEF
    //    L: FUNC_START                R: FUNC_BODY
    //         R: FUNC_HEAD       L: BLOCK or expression
    //    L: PARAM_LIST
    const ast_node *func_head = func->definition->left_child->right_child; // TODO: add NULL check for builtins

    // parse all function parameters
    scope_push(ctx->scopes, ctx->err, PARAMS_BLOCK_INDEX);
    for (const ast_node *param_list = func_head->left_child;
         *ctx->err == NONE && param_list != NULL && param_list->left_child != NULL;
         param_list = param_list->right_child)
    {
        //              PARAM
        // L: IDENTIFIER     R: TYPE
        const ast_node *param = param_list->left_child;
        declare_variable(ctx, param->left_child, ID_TYPE_CONSTANT, param->right_child->literal_type);
    }

    // check the body
    //      FUNC_BODY
    // L: BLOCK or expression
    ast_node *func_body = func->definition->right_child;
    if (*ctx->err == NONE)
    {
        if (func_body->left_child->type == AST_NODE_BLOCK)
            check_block(ctx, func_body->left_child);
        else
            check_expression(ctx, func_body->left_child); // one-line body
    }

    if (*ctx->err == NONE)
        check_function_result(ctx, func, func_body);

    scopes_stack_destruct(&ctx->scopes); // frees all scopes that are left, also after an error
    ctx->scopes = caller_scopes;
    ctx->current_function = caller;

    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    // also inferred type of @main must be Unit
    if (func->is_main && func->return_type != AST_TYPE_UNIT)
    {
        *ctx->err = ENTER_FUNC_INVALID_RETURN_TYPE;
        return AST_TYPE_NONE;
    }

    // for TAC: FUNC_DEF -> L: FUNC_START -> L: FUNC_NAME -> L: IDENTIFIER
    func->definition->left_child->left_child->left_child->literal_type = func->return_type;

    func->state = FUNC_CHECKED;
    return func->return_type;
}

// pass 2
// every function in the order of the source code
static void check_all_bodies(sem_context *ctx)
{
    for (int i = 0; i < ctx->function_count && *ctx->err == NONE; i++)
    {
        function_info *curr_func = &ctx->functions[i];
        if (curr_func->state == FUNC_CHECKED)
            continue; // already checked through a jump from an earlier call

        check_function(ctx, curr_func);
    }

    if (*ctx->err == NONE)
        check_recursion(ctx);
}

// checks if var exists
// writes block_index in IDENTIFIER node and literal_type
// called in `x = expr` cases
// returns identifier_data*
static identifier_data *use_variable(const sem_context *ctx, ast_node *var)
{
    long block_index = 0;
    identifier_data *id_data = scope_lookup(ctx->scopes, ctx->err, var->attribute_index, &block_index);

    if (id_data == NULL)
    {
        // *ctx->err = GENERAL_SEMANTICS_USE_WITHOUT_DEFINITION; scope_lookup already sets the error.
        //                                                       error will be different, but both have enum value 3
        return NULL;
    }
    var->block_index = block_index;
    var->literal_type = (ast_literal_type)id_data->value_type;

    return id_data;
}

ast_node *semantic_analyser_run(error *err, ast_node **parser_tree, attribute_table *attr_tab)
{
    *err = NONE;
    ast_node *ast_root = *parser_tree;
    *parser_tree = NULL; // the analyser owns the tree now

    sem_context ctx = {.functions = NULL,
                       .function_count = 0,
                       .current_function = NULL,
                       .scopes = NULL,
                       .err = err,
                       .attr_tab = attr_tab,
                       .calls = NULL};

    // pass 1
    // array with one record per function
    int def_count = 0;
    for (const ast_node *def_list_it = ast_root; def_list_it != NULL; def_list_it = def_list_it->right_child)
        def_count++;

    if (def_count > 0)
        ctx.functions = utils_safe_malloc(err, (size_t)def_count * sizeof(function_info));

    if (*err == NONE && def_count > 0)
    {
        const size_t calls_size = (size_t)def_count * (size_t)def_count * sizeof(bool);
        ctx.calls = utils_safe_malloc(err, calls_size);
        if (*err == NONE)
            memset(ctx.calls, 0, calls_size);
    }

    // pass 1
    for (const ast_node *def_list_it = ast_root; *err == NONE && def_list_it != NULL;
         def_list_it = def_list_it->right_child)
    {
        register_function(&ctx, def_list_it);
    }

    if (*err == NONE)
        check_main(&ctx);

    // pass 2
    if (*err == NONE)
        check_all_bodies(&ctx);

    // free all allocations for semantic analysis
    for (int i = 0; i < ctx.function_count; i++)
        UTILS_SAFE_FREE(&ctx.functions[i].arg_types);
    UTILS_SAFE_FREE(&ctx.functions);
    UTILS_SAFE_FREE(&ctx.calls);

    if (*err != NONE)
    {
        ast_node_destruct_recursively(&ast_root); // main.c does not free the tree after an error
        return NULL;
    }
    return ast_root;
}
