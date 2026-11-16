// @file    semantic_analyser.c
//
// @author  Vladyslav Doroshenko (xdorosv00)

#include "semantic_analyser.h"

// section to battle strange things in code
#define AST_NODE_PARAM_LIST_R AST_NODE_PARAM_LIST_
#define AST_NODE_STAT_LIST_R AST_NODE_STAT_LIST_
#define AST_NODE_IF_LIST_R AST_NODE_IF_LIST_
#define AST_NODE_CALL_PARAMS_R AST_NODE_CALL_PARAMS_

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
    error *err;
} sem_context;

static void scopes_destruct(stack **scopes)
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

static void scope_declare(stack *scopes, error *err, const int identifier_id, identifier_data **data)
{
    scope *current_scope = stack_peek(scopes, err, 0);
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
    for (int i = 0; i < ctx->function_count; i++)
    {
        if (ctx->functions[i].name_id == func_attribute_index)
        {
            *ctx->err = FUNC_MULTIPLE_DEFINITION;
            return;
        }
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

ast_node *parse_node(const ast_node *root_node, error *err)
{
    if (root_node == NULL)
        return 0;
    switch (root_node->type)
    {
    case AST_NODE_DEF_LIST: // NOLINT(bugprone-branch-clone)
        break;

    case AST_NODE_FUNC_DEF:
        break;

    case AST_NODE_MAIN_FUNC_DEF:
        break;

    case AST_NODE_PARAM_LIST:
        break;

    case AST_NODE_BLOCK: // NOLINT(bugprone-branch-clone)
        break;

    case AST_NODE_STATEMENT:
        break;

    case AST_NODE_DEFINITION:
        break;

    case AST_NODE_WHILE:
        break;

    case AST_NODE_IF_LIST:
        break;

    case AST_NODE_RETURN:
        break;

    case AST_NODE_FUNC_CALL:
        break;

    case AST_NODE_UNARY_OPERATION:
        break;

    case AST_NODE_BINARY_OPERATION:
        break;

    case AST_NODE_LITERAL:
        break;

    case AST_NODE_IDENTIFIER:
        break;

    default:
        break;
    }

    return 0;
}

ast_node *semantic_analyser_run(error *err, ast_node **parser_tree, attribute_table *attr_tab)
{
    (void)attr_tab; // needed in pass 2

    *err = NONE;
    ast_node *ast_root = *parser_tree;
    *parser_tree = NULL; // the analyser owns the tree now

    sem_context ctx = {.functions = NULL, .function_count = 0, .err = err};

    // pass 1
    // array with one record per function
    int def_count = 0;
    for (const ast_node *def_list_it = ast_root; def_list_it != NULL; def_list_it = def_list_it->right_child)
        def_count++;

    if (def_count > 0)
        ctx.functions = utils_safe_malloc(err, (size_t)def_count * sizeof(function_info));

    for (const ast_node *def_list_it = ast_root; *err == NONE && def_list_it != NULL;
         def_list_it = def_list_it->right_child)
    {
        register_function(&ctx, def_list_it);
    }

    if (*err == NONE)
        check_main(&ctx);

    // TODO: pass 2

    for (int i = 0; i < ctx.function_count; i++)
        UTILS_SAFE_FREE(&ctx.functions[i].arg_types);
    UTILS_SAFE_FREE(&ctx.functions);

    if (*err != NONE)
    {
        ast_node_destruct_recursively(&ast_root); // main.c does not free the tree after an error
        return NULL;
    }
    return ast_root;
}