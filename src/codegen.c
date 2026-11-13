// @file codegen.c
//
// @author Sereda Maksym (xseredm00)

#include "codegen.h"
// todo: define vars from TAC_VAR_DEF

// === OUT HELPERS ===
static void print_literal(const tac_argument *arg)
{
    switch (arg->value_type)
    {
    case ARG_INT:
        printf(" int@%lld", arg->const_int);
        break;
    case ARG_FLOAT:
        printf(" float@%a", arg->const_double);
        break;
    case ARG_BOOL:
        printf(" bool@%s", arg->const_bool ? "true" : "false");
        break;
    case ARG_STRING:
        printf(" string@");
        const unsigned char *c = (const unsigned char *)arg->const_str;
        while (*c != '\0')
        {
            if (*c <= 32 || *c == '#' || *c == '\\')
            {
                printf("\\%03d", *c);
                c++;
                continue;
            }
            putchar(*c);
            c++;
        }
        break;
    default:
        printf(" nil@nil");
        break;
    }
}

static void print_symbol(const tac_argument *arg)
{
    switch (arg->type)
    {
    case ARG_LITERAL:
        print_literal(arg);
        break;
    case ARG_ID:
        printf(" LF@%s", arg->identifier_name);
        break;
    case ARG_TEMPORARY_VAR:
        printf(" LF@$t%u", arg->temporary_var_id);
        break;
    case ARG_FUNC:
        printf(" $f$%s", arg->identifier_name);
        break;
    case ARG_LABEL:
        printf(" $L%u", arg->label_id);
        break;
    default:
        break;
    }
}

#define PRINT_3AC(op, arg1, arg2, result)                                                                              \
    printf(op);                                                                                                        \
    print_symbol(result);                                                                                              \
    print_symbol(arg1);                                                                                                \
    print_symbol(arg2);                                                                                                \
    putchar('\n');

#define PRINT_2AC(op, arg1, result)                                                                                    \
    printf(op);                                                                                                        \
    print_symbol(result);                                                                                              \
    print_symbol(arg1);                                                                                                \
    putchar('\n');

#define PRINT_1AC(op, arg)                                                                                             \
    printf(op);                                                                                                        \
    print_symbol(arg);                                                                                                 \
    putchar('\n');

// SLL for declared_vars_list vars
typedef struct declared_var_node_s
{
    const tac_argument *var;
    struct declared_var_node_s *next;
} declared_var_node;

void codegen_run(error *err, tac_list *tac_l, attribute_table *attr_tab)
{
    printf(".IFJcode26\n");

    tac_l->curr_instruction = tac_l->first_instruction;
    tac_instruction *t;
    unsigned fn_var_cnt = 0;
    bool last_was_FUNC_START = false;
    bool frame_open = false;

    for (int tac_instr_cnt = 0; tac_instr_cnt < tac_l->size; tac_instr_cnt++)
    {
        t = tac_get_curr_instruction(tac_l);
        switch (t->op)
        {
        case TAC_VAL_DEF: // temp
        case TAC_VAR_DEF:
        case TAC_ASSIGNMENT:
            PRINT_2AC("MOVE", &t->arg1, &t->result)
            break;
            // Arithmetics
        case TAC_PLUS:
            PRINT_3AC(t->arg1.value_type == ARG_STRING ? "CONCAT" : "ADD", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_MINUS:
            PRINT_3AC("SUB", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_UNARY_MINUS:
            printf("SUB");
            print_symbol(&t->result);
            printf(t->arg1.value_type == ARG_FLOAT ? " float@0x0p+0" : " int@0");
            print_symbol(&t->arg1);
            putchar('\n');
            break;
        case TAC_MUL:
            PRINT_3AC("MUL", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_DIV:
            PRINT_3AC(t->result.value_type == ARG_INT ? "IDIV" : "DIV", &t->arg1, &t->arg2, &t->result)
            break;
            // Bool
        case TAC_EQUALS:
            PRINT_3AC("EQ", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_GREATER:
            PRINT_3AC("GT", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_LESSER:
            PRINT_3AC("LT", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_NOT:
            PRINT_2AC("NOT", &t->arg1, &t->result)
            break;
        case TAC_OR:
            PRINT_3AC("OR", &t->arg1, &t->arg2, &t->result)
            break;
        case TAC_AND:
            PRINT_3AC("AND", &t->arg1, &t->arg2, &t->result)
            break;

            // Conversions
        case TAC_INT_TO_FLOAT:
            PRINT_2AC("INT2FLOAT", &t->arg1, &t->result)
            break;

            // Control
        case TAC_MAIN:
            printf("CREATEFRAME\n");
            PRINT_1AC("CALL", &t->result)
            printf("EXIT int@0\n"); // temptorary
            break;

        case TAC_LABEL:
            PRINT_1AC("LABEL", &t->result)
            break;

        case TAC_FUNC_START:
            PRINT_1AC("LABEL", &t->result)
            printf("PUSHFRAME\n");
            printf("DEFVAR LF@$res\n");
            printf("MOVE LF@$res nil@nil\n");

            // Go thrugh whole function only declaring vars once
            tac_instruction *tt = t->next_instruction;
            declared_var_node *d_vars_list = NULL;
            while (tt && tt->op != TAC_FUNC_END)
            {
                const tac_argument *args[] = {&tt->arg1, &tt->arg2, &tt->result};
                for (int i = 0; i < 3; i++)
                {
                    const tac_argument *arg = args[i];
                    if (arg->type != ARG_ID && arg->type != ARG_TEMPORARY_VAR)
                        continue;

                    bool already_in_list = false;
                    for (declared_var_node *curr_var = d_vars_list; curr_var != NULL; curr_var = curr_var->next)
                    {
                        if (curr_var->var->type != arg->type)
                            continue;
                        if (arg->type == ARG_TEMPORARY_VAR)
                            already_in_list = curr_var->var->temporary_var_id == arg->temporary_var_id;
                        else
                            already_in_list = strcmp(curr_var->var->identifier_name, arg->identifier_name) == 0;

                        if (already_in_list)
                            break;
                    }
                    if (already_in_list)
                        continue;
                    // add new declaration to list
                    declared_var_node *new_dec_var_node = utils_safe_malloc(err, sizeof(declared_var_node));
                    if (new_dec_var_node == NULL)
                        break;
                    new_dec_var_node->var = arg;
                    new_dec_var_node->next = d_vars_list;
                    d_vars_list = new_dec_var_node;

                    printf("DEFVAR");
                    print_symbol(arg);
                    putchar('\n');
                }
                tt = tt->next_instruction;
            }
            while (d_vars_list != NULL)
            {
                declared_var_node *next = d_vars_list->next;
                UTILS_SAFE_FREE(&d_vars_list);
                d_vars_list = next;
            }
            break;
        case TAC_FUNC_END:
            printf("POPFRAME\n");
            printf("RETURN\n");
            break;
        case TAC_PARAM:
            if (last_was_FUNC_START)
            {
                printf("MOVE");
                print_symbol(&t->arg1);
                printf(" LF@$f%u\n", fn_var_cnt++);
            }
            else
            {
                if (!frame_open)
                {
                    printf("CREATEFRAME\n");
                    frame_open = true;
                    fn_var_cnt = 0;
                }
                printf("DEFVAR TF@$f%u\n", fn_var_cnt);
                printf("MOVE TF@$f%u", fn_var_cnt++);
                print_symbol(&t->arg1);
                putchar('\n');
            }
            break;

        case TAC_CALL:
            if (!frame_open)
                printf("CREATEFRAME\n");
            frame_open = false;
            PRINT_1AC("CALL", &t->arg1)
            if (t->result.type != ARG_EMPTY)
            {
                printf("MOVE");
                print_symbol(&t->result);
                printf(" TF@$res\n");
            }
            break;
        case TAC_RETURN:
            if (t->result.type != ARG_EMPTY)
            {
                printf("MOVE LF@$res");
                print_symbol(&t->result);
                putchar('\n');
            }
            printf("POPFRAME\n");
            printf("RETURN\n");
            break;

        case TAC_GOTO:
            PRINT_1AC("JUMP", &t->result)
            break;

        case TAC_IF_JUMP:
            printf("JUMPIFEQ");
            print_symbol(&t->arg1);
            print_symbol(&t->result);
            printf(" bool@true\n");
            PRINT_1AC("JUMP", &t->arg2)

            break;
        }
        if (t->op != TAC_PARAM && t->op != TAC_FUNC_START)
            last_was_FUNC_START = false;
    }
}
