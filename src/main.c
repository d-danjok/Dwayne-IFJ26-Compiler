// @file 	main.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "codegen.h"
#include "parser.h"
#include "semantic_analyser.h"
#include "tac.h"
#include "tac_optimiser.h"

static error convert_err_for_program_return(const error err)
// may be this won't work as intended, if some action will be required, should be replaced with propper logic
{
    if (error_is_internal(err))
    {
        return GENERAL_INTERNAL;
    }
    return err;
}

int main()
{
    error err = NONE; // main error handling var
    FILE *src_stream = stdin;
    attribute_table *attr_tab = new_attribute_table(&err);
    if (err != NONE)
    {
        return GENERAL_INTERNAL;
    }

    // actual program
    ast_node *parser_tree = parser_run(&err, src_stream, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);

        destruct_attribute_table(&attr_tab);

        return err;
    }

    ast_node *ast = semantic_analyser_run(&err, &parser_tree, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);

        destruct_attribute_table(&attr_tab);

        return err;
    }

    tac_list *tac_l = tac_generate(&err, &ast, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    tac_l = tac_optimise(&err, tac_l);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    codegen_run(&err, tac_l);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    destruct_attribute_table(&attr_tab);

    return 0;
}
