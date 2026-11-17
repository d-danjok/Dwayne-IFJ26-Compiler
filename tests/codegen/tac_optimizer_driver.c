/* Test fixture only: compile with run_tests.py, never as part of the compiler. */
#include "ast_print.h"
#include "tac.h"
#include "tac_optimizer.h"
#ifdef TEST_CODEGEN
#include "codegen.h"
static bool codegen_freed_list;
void test_codegen_destruct(tac_list *list)
{
    codegen_freed_list = true;
    tac_list_destruct(list);
}
#endif
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static attribute_table *table;
static error err;
static unsigned allocations, fail_at;
static unsigned tac_allocations;
static bool tac_failure_injected;
void *test_tac_malloc(error *out, size_t size)
{
    const char *fail = getenv("TAC_TEST_FAIL_ALLOC");
    ++tac_allocations;
    if (fail && tac_allocations == strtoul(fail, NULL, 10))
    {
        tac_failure_injected = true;
        *out = INTERNAL_MEMORY_ALLOCATION;
        return NULL;
    }
    return utils_safe_malloc(out, size);
}
void *test_optimizer_malloc(error *out, size_t size)
{
    if (++allocations == fail_at)
    {
        *out = INTERNAL_MEMORY_ALLOCATION;
        return NULL;
    }
    return utils_safe_malloc(out, size);
}
static char *copy(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    assert(p);
    strcpy(p, s);
    return p;
}
static tac_argument empty(void)
{
    return (tac_argument){.type = ARG_EMPTY};
}
static tac_argument integer(long long x)
{
    return (tac_argument){.type = ARG_LITERAL, .value_type = ARG_INT, .const_int = x};
}
static tac_argument real(double x)
{
    return (tac_argument){.type = ARG_LITERAL, .value_type = ARG_FLOAT, .const_double = x};
}
static tac_argument boolean(bool x)
{
    return (tac_argument){.type = ARG_LITERAL, .value_type = ARG_BOOL, .const_bool = x};
}
static tac_argument label(unsigned x)
{
    return (tac_argument){.type = ARG_LABEL, .label_id = x};
}
static tac_argument temp(unsigned x, tac_argument_value_type type)
{
    return (tac_argument){.type = ARG_TEMPORARY_VAR, .value_type = type, .temporary_var_id = x};
}
static tac_argument variable(const char *name)
{
    return (tac_argument){.type = ARG_ID, .value_type = ARG_INT, .identifier_name = copy(name)};
}
static tac_argument function_name(char *name)
{
    return (tac_argument){.type = ARG_FUNC, .identifier_name = name};
}
static void add(tac_list *list, tac_operator op, tac_argument a, tac_argument b, tac_argument r)
{
    tac_instruction *n = calloc(1, sizeof(*n));
    assert(n);
    *n = (tac_instruction){.id = (unsigned)list->size, .op = op, .arg1 = a, .arg2 = b, .result = r};
    if (list->last_instruction)
        list->last_instruction->next_instruction = n;
    else
        list->first_instruction = list->curr_instruction = n;
    list->last_instruction = n;
    list->size++;
}
static void emit_print(tac_list *l, tac_argument a)
{
    add(l, TAC_CALL_PARAM, a, empty(), empty());
    add(l, TAC_CALL, function_name("print"), integer(1), temp(900, ARG_UNIT));
}
static tac_list *raw(const char *name, unsigned seed)
{
    tac_list *l = calloc(1, sizeof(*l));
    assert(l);
    if (!strcmp(name, "empty"))
        return l;
    if (!strncmp(name, "main_", 5))
    {
        int position = !strcmp(name, "main_first")    ? 0
                       : !strcmp(name, "main_middle") ? 1
                       : !strcmp(name, "main_last")   ? 2
                                                      : -1;
        for (int k = 0; k < 3; k++)
            add(l, k == position ? TAC_MAIN : TAC_LABEL, empty(), empty(),
                k == position ? function_name("main") : label((unsigned)k));
        return l;
    }
    if (!strcmp(name, "constant_chain") || !strcmp(name, "long_chain") || !strcmp(name, "random"))
    {
        unsigned count = !strcmp(name, "long_chain") ? 200 : !strcmp(name, "random") ? 8 : 3;
        long long start = !strcmp(name, "random") ? (long long)(seed % 31) - 15 : 4;
        add(l, TAC_ASSIGNMENT, integer(start), empty(), temp(0, ARG_INT));
        for (unsigned k = 1; k <= count; k++)
        {
            long long value = !strcmp(name, "random") ? (long long)((seed + k * 7) % 13) - 6 : (long long)k;
            add(l, k % 2 ? TAC_PLUS : TAC_MINUS, temp(k - 1, ARG_INT), integer(value), temp(k, ARG_INT));
        }
        add(l, TAC_RETURN, empty(), empty(), temp(count, ARG_INT));
        return l;
    }
    if (!strncmp(name, "alias", 5))
    {
        add(l, TAC_CALL, function_name("readInt"), integer(0), variable("y"));
        add(l, TAC_ASSIGNMENT, variable("y"), empty(), variable("x"));
        if (!strcmp(name, "alias_overwrite"))
            add(l, TAC_ASSIGNMENT, integer(99), empty(), variable("y"));
        if (!strcmp(name, "alias_cycle"))
            add(l, TAC_ASSIGNMENT, variable("x"), empty(), variable("y"));
        add(l, TAC_RETURN, empty(), empty(), variable("x"));
        return l;
    }
    if (!strcmp(name, "loop_sum"))
    {
        add(l, TAC_VAR_DEF, integer(0), empty(), variable("i"));
        add(l, TAC_VAR_DEF, integer(0), empty(), variable("sum"));
        add(l, TAC_LABEL, empty(), empty(), label(1));
        add(l, TAC_LESSER, variable("i"), integer(10), temp(1, ARG_BOOL));
        add(l, TAC_IF_JUMP, label(2), label(3), temp(1, ARG_BOOL));
        add(l, TAC_LABEL, empty(), empty(), label(2));
        add(l, TAC_PLUS, variable("sum"), variable("i"), temp(2, ARG_INT));
        add(l, TAC_ASSIGNMENT, temp(2, ARG_INT), empty(), variable("sum"));
        add(l, TAC_PLUS, variable("i"), integer(1), temp(3, ARG_INT));
        add(l, TAC_ASSIGNMENT, temp(3, ARG_INT), empty(), variable("i"));
        add(l, TAC_GOTO, empty(), empty(), label(1));
        add(l, TAC_LABEL, empty(), empty(), label(3));
        add(l, TAC_RETURN, empty(), empty(), variable("sum"));
        return l;
    }
    if (!strcmp(name, "branch_join"))
    {
        add(l, TAC_ASSIGNMENT, integer(10), empty(), variable("x"));
        add(l, TAC_IF_JUMP, label(1), label(2), boolean(false));
        add(l, TAC_LABEL, empty(), empty(), label(1));
        add(l, TAC_ASSIGNMENT, integer(20), empty(), variable("x"));
        add(l, TAC_GOTO, empty(), empty(), label(3));
        add(l, TAC_LABEL, empty(), empty(), label(2));
        add(l, TAC_ASSIGNMENT, integer(30), empty(), variable("x"));
        add(l, TAC_LABEL, empty(), empty(), label(3));
        add(l, TAC_RETURN, empty(), empty(), variable("x"));
        return l;
    }
    if (!strcmp(name, "dead_effect") || !strcmp(name, "live_effect") || !strcmp(name, "dead_declaration"))
    {
        if (strcmp(name, "live_effect"))
            add(l, TAC_GOTO, empty(), empty(), label(1));
        emit_print(l, integer(99));
        if (!strcmp(name, "dead_declaration"))
            add(l, TAC_VAR_DEF, integer(2), empty(), variable("declared"));
        add(l, TAC_LABEL, empty(), empty(), label(1));
        add(l, TAC_RETURN, empty(), empty(), integer(7));
        return l;
    }
    if (!strcmp(name, "call_barrier") || !strcmp(name, "param_barrier") || !strcmp(name, "label_barrier"))
    {
        add(l, TAC_ASSIGNMENT, integer(7), empty(), variable("x"));
        if (!strcmp(name, "call_barrier"))
            add(l, TAC_CALL, function_name("readInt"), integer(0), temp(1, ARG_INT));
        else if (!strcmp(name, "param_barrier"))
            add(l, TAC_CALL_PARAM, variable("x"), empty(), empty());
        else
            add(l, TAC_LABEL, empty(), empty(), label(1));
        add(l, TAC_RETURN, empty(), empty(), variable("x"));
        return l;
    }
    if (!strncmp(name, "convert", 7) || !strcmp(name, "ordinary_call"))
    {
        bool ordinary = !strcmp(name, "ordinary_call");
        add(l, TAC_ASSIGNMENT, integer(!strcmp(name, "convert_large") ? 9007199254740993LL : 42), empty(),
            variable("x"));
        if (!strcmp(name, "convert_boundary"))
            add(l, TAC_LABEL, empty(), empty(), label(1));
        if (!strcmp(name, "convert_nested"))
            add(l, TAC_CALL_PARAM, integer(5), empty(), empty());
        add(l, TAC_CALL_PARAM, !strcmp(name, "convert_nested") ? integer(42) : variable("x"), empty(), empty());
        add(l, TAC_CALL, function_name(ordinary ? "identity" : "INT_TO_FLOAT"), integer(1),
            temp(1, ordinary ? ARG_INT : ARG_FLOAT));
        if (!strcmp(name, "convert_nested"))
        {
            add(l, TAC_CALL_PARAM, temp(1, ARG_FLOAT), empty(), empty());
            add(l, TAC_CALL, function_name("sum"), integer(2), temp(2, ARG_FLOAT));
            add(l, TAC_RETURN, empty(), empty(), temp(2, ARG_FLOAT));
        }
        else
            add(l, TAC_RETURN, empty(), empty(), temp(1, ordinary ? ARG_INT : ARG_FLOAT));
        return l;
    }
    if (!strcmp(name, "owned_strings"))
    {
        add(l, TAC_ASSIGNMENT,
            (tac_argument){.type = ARG_LITERAL, .value_type = ARG_STRING, .const_str = "borrowed # string"}, empty(),
            temp(1, ARG_STRING));
        emit_print(l, temp(1, ARG_STRING));
        add(l, TAC_RETURN, empty(), empty(), temp(1, ARG_STRING));
        return l;
    }
    tac_operator op = TAC_PLUS;
    tac_argument a = integer(1), b = integer(2);
    tac_argument_value_type type = ARG_INT;
    if (!strcmp(name, "negative_div"))
    {
        op = TAC_DIV;
        a = integer(-7);
        b = integer(2);
    }
    else if (!strcmp(name, "zero_div") || !strcmp(name, "unused_trap"))
    {
        op = TAC_DIV;
        a = integer(1);
        b = integer(0);
    }
    else if (!strcmp(name, "int_max_add"))
    {
        a = integer(LLONG_MAX);
        b = integer(1);
    }
    else if (!strcmp(name, "int_min_sub"))
    {
        op = TAC_MINUS;
        a = integer(LLONG_MIN);
        b = integer(1);
    }
    else if (!strcmp(name, "int_min_neg"))
    {
        op = TAC_UNARY_MINUS;
        a = integer(LLONG_MIN);
        b = empty();
    }
    else if (!strcmp(name, "int_min_div"))
    {
        op = TAC_DIV;
        a = integer(LLONG_MIN);
        b = integer(-1);
    }
    else if (!strcmp(name, "mul_overflow"))
    {
        op = TAC_MUL;
        a = integer(LLONG_MAX);
        b = integer(2);
    }
    else if (!strcmp(name, "float_div"))
    {
        op = TAC_DIV;
        a = real(9);
        b = real(2);
        type = ARG_FLOAT;
    }
    else if (!strcmp(name, "float_zero"))
    {
        op = TAC_DIV;
        a = real(1);
        b = real(0);
        type = ARG_FLOAT;
    }
    else if (!strcmp(name, "float_overflow"))
    {
        op = TAC_MUL;
        a = real(1e308);
        b = real(1e308);
        type = ARG_FLOAT;
    }
    else if (!strcmp(name, "nan_equal"))
    {
        op = TAC_EQUALS;
        a = real(NAN);
        b = real(NAN);
        type = ARG_BOOL;
    }
    else if (!strcmp(name, "signed_zero"))
    {
        op = TAC_MUL;
        a = real(-0.0);
        b = real(2.0);
        type = ARG_FLOAT;
    }
    else if (!strcmp(name, "not"))
    {
        op = TAC_NOT;
        a = boolean(false);
        b = empty();
        type = ARG_BOOL;
    }
    else if (!strcmp(name, "bool_and"))
    {
        op = TAC_AND;
        a = boolean(true);
        b = boolean(false);
        type = ARG_BOOL;
    }
    else if (!strcmp(name, "bool_or"))
    {
        op = TAC_OR;
        a = boolean(false);
        b = boolean(true);
        type = ARG_BOOL;
    }
    else
    {
        fprintf(stderr, "unknown raw fixture %s\n", name);
        exit(64);
    }
    add(l, op, a, b, temp(1, type));
    add(l, TAC_RETURN, empty(), empty(), !strcmp(name, "unused_trap") ? integer(7) : temp(1, type));
    return l;
}

/* LL-shaped AST builders. The parser is deliberately bypassed. */
static ast_node *node(ast_node_type type, ast_node *left, ast_node *right)
{
    ast_node *n = new_ast_node(&err);
    assert(n);
    n->type = type;
    n->left_child = left;
    n->right_child = right;
    return n;
}
static ast_node *id(const char *name, ast_literal_type type)
{
    ast_node *n = node(AST_NODE_IDENTIFIER, NULL, NULL);
    char *s = copy(name);
    n->attribute_index = attribute_table_insert_identifier(table, &err, &s);
    assert(err == NONE);
    n->literal_type = type;
    n->block_index = 0;
    return n;
}
static ast_node *num(int x)
{
    ast_node *n = node(AST_NODE_LITERAL, NULL, NULL);
    n->literal_type = AST_TYPE_INT;
    n->attribute_index = attribute_table_insert_integer(table, &err, x);
    assert(err == NONE);
    return n;
}
static ast_node *truth(bool x)
{
    ast_node *n = node(AST_NODE_LITERAL, NULL, NULL);
    n->literal_type = AST_TYPE_BOOLEAN;
    n->attribute_index = x;
    return n;
}
static ast_node *str(const char *x)
{
    ast_node *n = node(AST_NODE_LITERAL, NULL, NULL);
    char *s = copy(x);
    n->attribute_index = attribute_table_insert_string(table, &err, &s);
    assert(err == NONE);
    n->literal_type = AST_TYPE_STRING;
    return n;
}
static ast_node *bin(ast_operator op, ast_literal_type type, ast_node *a, ast_node *b)
{
    ast_node *n = node(AST_NODE_BINARY_OPERATION, a, b);
    n->operator = op;
    n->literal_type = type;
    return n;
}
static ast_node *call(const char *name, ast_literal_type type, unsigned count, ...)
{
    ast_node *params = node(AST_NODE_CALL_PARAMS, NULL, NULL), *p = params;
    va_list ap;
    va_start(ap, count);
    for (unsigned k = 0; k < count; k++)
    {
        p->left_child = node(AST_NODE_CALL_PARAM, va_arg(ap, ast_node *), NULL);
        if (k + 1 < count)
        {
            p->right_child = node(AST_NODE_CALL_PARAMS_, NULL, NULL);
            p = p->right_child;
        }
    }
    va_end(ap);
    ast_node *n = node(AST_NODE_FUNC_CALL, id(name, type), params);
    n->literal_type = type;
    return n;
}
static ast_node *print(ast_node *a)
{
    return call("print", AST_TYPE_UNIT, 1, a);
}
static ast_node *block(unsigned count, ...)
{
    ast_node *list = node(AST_NODE_STAT_LIST, NULL, NULL), *p = list;
    va_list ap;
    va_start(ap, count);
    for (unsigned k = 0; k < count; k++)
    {
        ast_node *s = va_arg(ap, ast_node *);
        p->left_child = s->type == AST_NODE_STATEMENT ? s : node(AST_NODE_STATEMENT, s, NULL);
        if (k + 1 < count)
        {
            p->right_child = node(AST_NODE_STAT_LIST_, NULL, NULL);
            p = p->right_child;
        }
    }
    va_end(ap);
    return node(AST_NODE_BLOCK, list, NULL);
}
static ast_node *ret(ast_node *a)
{
    return node(AST_NODE_RETURN, a ? node(AST_NODE_ASSIGN_VAL, a, NULL) : NULL, NULL);
}
static ast_node *def(const char *name, ast_node *value)
{
    return node(AST_NODE_DEFINITION,
                node(AST_NODE_VAR_DEF, node(AST_NODE_DEF_BODY, id(name, value->literal_type), NULL), NULL),
                node(AST_NODE_ASSIGN_VAL, value, NULL));
}
static ast_node *assign(const char *name, ast_node *value)
{
    return node(AST_NODE_STATEMENT, id(name, value->literal_type), node(AST_NODE_ASSIGN_VAL, value, NULL));
}
static ast_node *branch(ast_node *condition, ast_node *yes, ast_node *no)
{
    return node(AST_NODE_IF_LIST, node(AST_NODE_IF, condition, yes),
                no ? node(AST_NODE_IF_LIST_, node(AST_NODE_ELSE, no, NULL), NULL) : NULL);
}
static ast_node *function(const char *name, ast_literal_type type, ast_node *body, const char *parameter)
{
    ast_node *params = node(AST_NODE_PARAM_LIST, NULL, NULL);
    if (parameter)
    {
        ast_node *t = node(AST_NODE_TYPE, NULL, NULL);
        t->literal_type = AST_TYPE_INT;
        params->left_child = node(AST_NODE_PARAM, id(parameter, AST_TYPE_INT), t);
    }
    ast_node *t = node(AST_NODE_TYPE, NULL, NULL);
    t->literal_type = type;
    return node(
        AST_NODE_FUNC_DEF,
        node(AST_NODE_FUNC_START, node(AST_NODE_FUNC_NAME, id(name, type), NULL), node(AST_NODE_FUNC_HEAD, params, t)),
        node(AST_NODE_FUNC_BODY, body, NULL));
}
static ast_node *program(ast_node *body, ast_node *extra, bool main_last)
{
    if (body->type != AST_NODE_BLOCK)
        body = block(1, body);
    ast_node *main = node(AST_NODE_MAIN_FUNC_DEF, function("main", AST_TYPE_UNIT, body, NULL), NULL);
    if (main_last)
        return node(AST_NODE_DEF_LIST, extra, node(AST_NODE_DEF_LIST, main, NULL));
    return node(AST_NODE_DEF_LIST, main, extra ? node(AST_NODE_DEF_LIST, extra, NULL) : NULL);
}
static ast_node *ast_fixture(const char *name)
{
    if (!strcmp(name, "arithmetic_output"))
        return program(
            print(bin(AST_OP_MULTIPLY, AST_TYPE_INT, bin(AST_OP_PLUS, AST_TYPE_INT, num(2), num(3)), num(4))), NULL,
            false);
    if (!strcmp(name, "negative_division"))
        return program(print(bin(AST_OP_DIV, AST_TYPE_INT, num(-7), num(2))), NULL, false);
    if (!strcmp(name, "string_output"))
        return program(print(str("line 1\n# \\ end")), NULL, false);
    if (!strcmp(name, "plain_call"))
        return program(print(call("f", AST_TYPE_INT, 0)), function("f", AST_TYPE_INT, num(42), NULL), false);
    if (!strcmp(name, "parameter_output"))
        return program(print(call("f", AST_TYPE_INT, 1, num(7))),
                       function("f", AST_TYPE_INT, id("x", AST_TYPE_INT), "x"), false);
    if (!strcmp(name, "early_return"))
        return program(print(call("f", AST_TYPE_INT, 0)),
                       function("f", AST_TYPE_INT, block(2, ret(num(42)), print(str("BAD"))), NULL), false);
    if (!strcmp(name, "no_else"))
        return program(block(2, branch(truth(false), block(1, print(str("BAD"))), NULL), print(str("ok"))), NULL,
                       false);
    if (!strcmp(name, "nested_builtin"))
    {
        ast_node *number = node(AST_NODE_LITERAL, NULL, NULL);
        number->literal_type = AST_TYPE_DOUBLE;
        number->attribute_index = attribute_table_insert_double(table, &err, 1.0);
        assert(err == NONE);
        return program(print(call("ord", AST_TYPE_INT, 2, str("AZ"), call("toInt", AST_TYPE_INT, 1, number))), NULL,
                       false);
    }
    if (!strcmp(name, "print_value"))
        return program(print(print(num(1))), NULL, false);
    if (!strcmp(name, "boolean_text"))
        return program(print(call("strLength", AST_TYPE_INT, 1, call("toStr", AST_TYPE_STRING, 1, truth(true)))), NULL,
                       false);
    if (!strcmp(name, "read_output"))
        return program(print(call("readInt", AST_TYPE_INT, 0)), NULL, false);
    if (!strcmp(name, "ord_output"))
        return program(print(call("ord", AST_TYPE_INT, 2, str("AZ"), num(1))), NULL, false);
    if (!strcmp(name, "substring_output"))
        return program(print(call("subStr", AST_TYPE_STRING, 3, str("abcd"), num(1), num(3))), NULL, false);

    if (!strcmp(name, "invoice"))
        return program(
            block(3, def("price", num(25)), def("count", num(4)),
                  print(bin(AST_OP_MINUS, AST_TYPE_INT,
                            bin(AST_OP_MULTIPLY, AST_TYPE_INT, id("price", AST_TYPE_INT), id("count", AST_TYPE_INT)),
                            num(10)))),
            NULL, false);
    if (!strcmp(name, "sum_loop"))
    {
        ast_node *step = assign("i", bin(AST_OP_PLUS, AST_TYPE_INT, id("i", AST_TYPE_INT), num(1)));
        ast_node *accumulate =
            assign("sum", bin(AST_OP_PLUS, AST_TYPE_INT, id("sum", AST_TYPE_INT), id("i", AST_TYPE_INT)));
        ast_node *loop = node(AST_NODE_WHILE, bin(AST_OP_LESS, AST_TYPE_BOOLEAN, id("i", AST_TYPE_INT), num(10)),
                              block(2, accumulate, step));
        return program(block(4, def("i", num(0)), def("sum", num(0)), loop, print(id("sum", AST_TYPE_INT))), NULL,
                       false);
    }
    if (!strcmp(name, "nested_loops"))
    {
        ast_node *accumulate = assign("total", bin(AST_OP_PLUS, AST_TYPE_INT, id("total", AST_TYPE_INT), num(1)));
        ast_node *step_j = assign("j", bin(AST_OP_PLUS, AST_TYPE_INT, id("j", AST_TYPE_INT), num(1)));
        ast_node *inner = node(AST_NODE_WHILE, bin(AST_OP_LESS, AST_TYPE_BOOLEAN, id("j", AST_TYPE_INT), num(4)),
                               block(2, accumulate, step_j));
        ast_node *step_i = assign("i", bin(AST_OP_PLUS, AST_TYPE_INT, id("i", AST_TYPE_INT), num(1)));
        ast_node *outer = node(AST_NODE_WHILE, bin(AST_OP_LESS, AST_TYPE_BOOLEAN, id("i", AST_TYPE_INT), num(3)),
                               block(3, def("j", num(0)), inner, step_i));
        return program(block(4, def("i", num(0)), def("total", num(0)), outer, print(id("total", AST_TYPE_INT))), NULL,
                       false);
    }
    if (!strcmp(name, "input_branch"))
        return program(block(2, def("x", call("readInt", AST_TYPE_INT, 0)),
                             branch(bin(AST_OP_GREATER, AST_TYPE_BOOLEAN, id("x", AST_TYPE_INT), num(0)),
                                    block(1, print(str("positive"))), block(1, print(str("other"))))),
                       NULL, false);
    if (!strcmp(name, "dead_branch"))
        return program(block(1, branch(truth(false), block(1, print(str("bad"))), block(1, print(str("ok"))))), NULL,
                       false);
    if (!strcmp(name, "scope_shadow"))
    {
        ast_node *inner_def = def("x", num(9));
        inner_def->left_child->left_child->left_child->block_index = 1;
        ast_node *inner_ref = id("x", AST_TYPE_INT);
        inner_ref->block_index = 1;
        return program(block(3, def("x", num(7)), branch(truth(true), block(2, inner_def, print(inner_ref)), NULL),
                             print(id("x", AST_TYPE_INT))),
                       NULL, false);
    }
    if (!strcmp(name, "factorial") || !strcmp(name, "main_after_function"))
    {
        ast_node *recursive =
            call("fact", AST_TYPE_INT, 1, bin(AST_OP_MINUS, AST_TYPE_INT, id("n", AST_TYPE_INT), num(1)));
        ast_node *body = block(
            1, branch(bin(AST_OP_LESS_EQUAL, AST_TYPE_BOOLEAN, id("n", AST_TYPE_INT), num(1)), block(1, ret(num(1))),
                      block(1, ret(bin(AST_OP_MULTIPLY, AST_TYPE_INT, id("n", AST_TYPE_INT), recursive)))));
        return program(print(call("fact", AST_TYPE_INT, 1, num(6))), function("fact", AST_TYPE_INT, body, "n"),
                       !strcmp(name, "main_after_function"));
    }
    if (!strcmp(name, "implicit_return"))
        return program(print(call("f", AST_TYPE_INT, 0)), function("f", AST_TYPE_INT, block(1, num(42)), NULL), false);
    if (!strcmp(name, "unit_return"))
    {
        ast_node *u = node(AST_NODE_LITERAL, NULL, NULL);
        u->literal_type = AST_TYPE_UNIT;
        return program(print(bin(AST_OP_EQUAL, AST_TYPE_BOOLEAN, call("f", AST_TYPE_UNIT, 0), u)),
                       function("f", AST_TYPE_UNIT, num(42), NULL), false);
    }
    if (!strcmp(name, "short_or") || !strcmp(name, "short_and"))
    {
        bool is_or = !strcmp(name, "short_or");
        return program(print(bin(is_or ? AST_OP_OR : AST_OP_AND, AST_TYPE_BOOLEAN, truth(is_or),
                                 call("probe", AST_TYPE_BOOLEAN, 0))),
                       function("probe", AST_TYPE_BOOLEAN, block(2, print(str("SIDE")), ret(truth(true))), NULL),
                       false);
    }
    if (!strcmp(name, "nested_calls"))
        return program(print(call("sum", AST_TYPE_INT, 2, num(5), call("identity", AST_TYPE_INT, 1, num(7)))), NULL,
                       false);
    if (!strcmp(name, "conversion"))
    {
        ast_node *n = node(AST_NODE_INT_TO_DOUBLE_CONVERSION, bin(AST_OP_PLUS, AST_TYPE_INT, num(2), num(3)), NULL);
        n->literal_type = AST_TYPE_DOUBLE;
        return program(print(call("toStr", AST_TYPE_STRING, 1, n)), NULL, false);
    }
    if (!strcmp(name, "concat"))
        return program(print(bin(AST_OP_CONCAT, AST_TYPE_STRING, str("Hello "), str("world"))), NULL, false);
    fprintf(stderr, "unknown AST fixture %s\n", name);
    exit(64);
}
static void quoted(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        if (*p == '"' || *p == '\\')
            printf("\\%c", *p);
        else if (*p < 32 || *p >= 127)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    putchar('"');
}
static const char *value_type(tac_argument a)
{
    switch (a.value_type)
    {
    case ARG_INT:
        return "int";
    case ARG_FLOAT:
        return "float";
    case ARG_BOOL:
        return "bool";
    case ARG_STRING:
        return "string";
    case ARG_UNIT:
        return "unit";
    default:
        return "none";
    }
}
static void argument(tac_argument a)
{
    const char *kind = a.type == ARG_ID              ? "id"
                       : a.type == ARG_TEMPORARY_VAR ? "temp"
                       : a.type == ARG_LITERAL       ? "literal"
                       : a.type == ARG_LABEL         ? "label"
                       : a.type == ARG_FUNC          ? "func"
                       : a.type == ARG_EMPTY         ? "empty"
                                                     : "error";
    putchar('[');
    quoted(kind);
    putchar(',');
    quoted(value_type(a));
    putchar(',');
    if (a.type == ARG_ID || a.type == ARG_FUNC)
        quoted(a.identifier_name);
    else if (a.type == ARG_LABEL)
        printf("%u", a.label_id);
    else if (a.type == ARG_TEMPORARY_VAR)
        printf("%u", a.temporary_var_id);
    else if (a.type != ARG_LITERAL)
        printf("null");
    else if (a.value_type == ARG_INT)
        printf("%lld", a.const_int);
    else if (a.value_type == ARG_FLOAT)
    {
        char buf[80];
        snprintf(buf, sizeof buf, "%a", a.const_double);
        quoted(buf);
    }
    else if (a.value_type == ARG_BOOL)
        printf(a.const_bool ? "true" : "false");
    else if (a.value_type == ARG_STRING)
        quoted(a.const_str);
    else
        printf("null");
    putchar(']');
}
static const char *operation(tac_operator op)
{
#define OP(x)                                                                                                          \
    case TAC_##x:                                                                                                      \
        return #x;
    switch (op)
    {
        OP(MAIN)
        OP(PLUS)
        OP(MINUS)
        OP(UNARY_MINUS) OP(MUL) OP(DIV) OP(CALL) OP(GOTO) OP(PARAM) OP(ASSIGNMENT) OP(RETURN) OP(IF_JUMP) OP(LABEL)
            OP(EQUALS) OP(GREATER) OP(LESSER) OP(NOT) OP(FUNC_START) OP(FUNC_END) OP(OR) OP(AND) OP(VAR_DEF) OP(VAL_DEF) OP(CALL_PARAM)
    }
#undef OP
    return "UNKNOWN";
}
static bool metadata(tac_list *list)
{
    tac_instruction *slow = list->first_instruction, *fast = slow;
    while (fast && fast->next_instruction)
    {
        slow = slow->next_instruction;
        fast = fast->next_instruction->next_instruction;
        if (slow == fast)
        {
            fprintf(stderr, "cycle in TAC list\n");
            exit(65);
        }
    }
    long count = 0;
    tac_instruction *last = NULL;
    for (tac_instruction *n = list->first_instruction; n; n = n->next_instruction)
    {
        if (n->id != (unsigned)count)
            return false;
        count++;
        last = n;
    }
    return count == list->size && last == list->last_instruction && list->curr_instruction == list->first_instruction;
}
static void dump(tac_list *list)
{
    putchar('[');
    bool comma = false;
    for (tac_instruction *n = list->first_instruction; n; n = n->next_instruction)
    {
        if (comma)
            putchar(',');
        comma = true;
        putchar('[');
        quoted(operation(n->op));
        putchar(',');
        argument(n->arg1);
        putchar(',');
        argument(n->arg2);
        putchar(',');
        argument(n->result);
        putchar(']');
    }
    putchar(']');
}
int main(int argc, char **argv)
{
    if (argc < 3)
        return 64;
    const char *name = argv[2];
    unsigned seed = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 1;
    const char *fail = getenv("OPT_TEST_FAIL_ALLOC");
    fail_at = fail ? (unsigned)strtoul(fail, NULL, 10) : 0;
    if (!strcmp(name, "null"))
    {
        tac_list *r = tac_optimize(&err, NULL);
        printf("{\"null_ok\":%s}\n", r == NULL && err != NONE ? "true" : "false");
        return 0;
    }
    table = new_attribute_table(&err);
    assert(table && err == NONE);
    tac_list *list;
    bool consumed = true;
    bool generating_code = !strcmp(argv[1], "codegen") || !strcmp(argv[1], "codegen-opt");
    if (!strcmp(argv[1], "ast") || generating_code)
    {
        ast_node *root = ast_fixture(name);
        if (getenv("DUMP_AST"))
            ast_print(stderr, root, table);
        list = tac_generate(&err, &root, table);
        consumed = root == NULL;
    }
    else
        list = raw(name, seed);
    if (!list || err != NONE)
    {
        if (generating_code)
        {
            fprintf(stderr, "TAC generation failed: error=%d\n", err);
            destruct_attribute_table(&table);
            return 2;
        }
        printf("{\"generation_error\":%d,\"ast_consumed\":%s,\"tac_allocations\":%u,\"tac_failure_injected\":%s}\n",
               err, consumed ? "true" : "false", tac_allocations, tac_failure_injected ? "true" : "false");
        destruct_attribute_table(&table);
        return 0;
    }
#ifdef TEST_CODEGEN
    if (generating_code)
    {
        if (!strcmp(argv[1], "codegen-opt") && tac_optimize(&err, list) == NULL)
        {
            fprintf(stderr, "Optimization failed: error=%d\n", err);
            tac_list_destruct(list);
            destruct_attribute_table(&table);
            return 3;
        }
        codegen_run(&err, list, table);
        if (!codegen_freed_list)
            tac_list_destruct(list);
        destruct_attribute_table(&table);
        if (err != NONE)
            fprintf(stderr, "Codegen failed: error=%d\n", err);
        return err == NONE ? 0 : 4;
    }
#endif
    printf("{\"ast_consumed\":%s,\"tac_allocations\":%u,\"before\":", consumed ? "true" : "false", tac_allocations);
    dump(list);
    allocations = 0;
    tac_list *result = tac_optimize(&err, list);
    unsigned first_allocations = allocations;
    printf(",\"optimizer_error\":%d,\"failed\":%s,\"allocations\":%u,\"metadata\":%s,\"after\":", err,
           result ? "false" : "true", first_allocations, metadata(list) ? "true" : "false");
    dump(list);
    if (result)
    {
        fail_at = 0;
        result = tac_optimize(&err, list);
        printf(",\"again_error\":%d,\"again_metadata\":%s,\"again\":", err, metadata(list) ? "true" : "false");
        dump(list);
    }
    printf("}\n");
    tac_list_destruct(list);
    destruct_attribute_table(&table);
    return 0;
}
