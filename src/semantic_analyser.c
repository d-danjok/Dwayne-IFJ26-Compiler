// @file    semantic_analyser.c
//
// @author  Vladyslav Doroshenko (xdorosv00)

#include "semantic_analyser.h"

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
} sem_context;


static void check_block(sem_context *ctx, const ast_node *block);
static identifier_data *use_variable(const sem_context *ctx, ast_node *var);
static void check_function(sem_context *ctx, function_info *func);
static ast_literal_type check_expression(sem_context *ctx, ast_node **expression);


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
static void declare_variable(const sem_context *ctx, ast_node *id_node, const identifier_type kind,
                             const ast_literal_type type)
{
    identifier_data *data = NEW_IDENTIFIER_DATA(ctx->err, 0);
    if (*ctx->err != NONE)
        return;

    data->value_type = (data_type)type; // symtable stores data_type, enum values are the same
    data->type = kind;
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


static ast_literal_type check_binary_operation(sem_context *ctx, ast_node *expression)
{
    ast_node *binary_op = expression;

    ast_literal_type left_literal = check_expression(ctx, &binary_op->left_child);
    if (*ctx->err != NONE)
        return AST_TYPE_NONE;

    ast_literal_type right_literal = check_expression(ctx, &binary_op->right_child);
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

        if (both_numeric && left_literal == right_literal)
        {
            result = AST_TYPE_BOOLEAN;
            break;
        }

        if (left_literal == AST_TYPE_STRING && right_literal == AST_TYPE_STRING)
        {
            result = AST_TYPE_BOOLEAN;
            break;
        }
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

// finds out the type of an expression and writes it into its nodes (TAC reads them)
// gets the address of the parent's pointer, so later an INT_TO_DOUBLE node can be inserted above the expression
// returns AST_TYPE_NONE after an error
static ast_literal_type check_expression(sem_context *ctx, ast_node **expression)
{
    ast_node *node = *expression;

    switch (node->type)
    {
    case AST_NODE_LITERAL:
        return node->literal_type;

    case AST_NODE_IDENTIFIER:
        use_variable(ctx, node);
        if (*ctx->err != NONE)
            return AST_TYPE_NONE;
        return node->literal_type;

        // TODO: AST_NODE_UNARY_OPERATION, AST_NODE_BINARY_OPERATION, AST_NODE_FUNC_CALL

    case AST_NODE_UNARY_OPERATION:
        // check_unary_operation(ctx, expression);
        return AST_TYPE_NONE;

    case AST_NODE_BINARY_OPERATION:
        return check_binary_operation(ctx, *expression);

    default:
        return AST_TYPE_NONE;
    }
}

// TODO: checks one statement
static void check_statement(sem_context *ctx, ast_node *statement_def)
{
    //            x = expr
    // L: IDENTIFIER     R: ASSIGN_VAL

    if (statement_def->right_child != NULL)
    {
        // TODO: check_assignment
        return;
    }

    ast_node *inner = statement_def->left_child;

    switch (inner->type)
    {
    case AST_NODE_DEFINITION: // yes
        break;

    case AST_NODE_WHILE: // yes
        // TODO: condition must be boolean
        check_block(ctx, inner->right_child);
        break;

    case AST_NODE_IF_LIST: // yes
        break;

    case AST_NODE_RETURN: // yes
        break;

    default: // plain expression: print(x), f(1)
             // TODO: check_expression
        check_expression(ctx, &statement_def->left_child);
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
static void check_function(sem_context *ctx, function_info *func)
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
        return;
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
    const ast_node *body = func->definition->right_child->left_child;
    if (*ctx->err == NONE && body->type == AST_NODE_BLOCK)
        check_block(ctx, body);
    // TODO: one-line body (expression)

    scopes_stack_destruct(&ctx->scopes); // frees all scopes that are left, also after an error
    ctx->scopes = caller_scopes;
    ctx->current_function = caller;

    if (*ctx->err == NONE)
        func->state = FUNC_CHECKED;
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

    sem_context ctx =
    {
        .functions = NULL,
        .function_count = 0,
        .current_function = NULL,
        .scopes = NULL,
        .err = err
    };

    // pass 1
    // array with one record per function
    int def_count = 0;
    for (const ast_node *def_list_it = ast_root; def_list_it != NULL; def_list_it = def_list_it->right_child)
        def_count++;

    if (def_count > 0)
        ctx.functions = utils_safe_malloc(err, (size_t)def_count * sizeof(function_info));

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

    if (*err != NONE)
    {
        ast_node_destruct_recursively(&ast_root); // main.c does not free the tree after an error
        return NULL;
    }
    return ast_root;
}
