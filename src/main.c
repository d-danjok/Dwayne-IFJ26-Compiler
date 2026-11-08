// @file 	main.c
//
// @author 	Daniil Didenko (xdidend00)

#include <stdio.h>

#include "ast.h"
#include "attribute_table.h"
#include "parser.h"
#include "symtable.h"

// temporary placeholders for project structure representation TODO: implement and delete from here
typedef struct tac_s tac; // tmp placeholder, delete it

extern ast_node *semantic_analyser_run(error *err, ast_node **parser_tree, attribute_table *attr_tab);

// NOTE: takes ownership of parser_tree (should free it itself if no longer needs it)

extern tac *tac_generate(error *err, ast_node **ast_root, attribute_table *attr_tab);

// tac_generate NOTE: takes ownership of AST (should free it itself if no longer needs it)

extern tac *tac_optimise(error *err, tac **unoptimised_tac, attribute_table *attr_tab);

// tac_optimise NOTE: takes ownership of old tac (should free it itself if no longer needs it)

extern void codegen_run(error *err, tac **tac, attribute_table *attr_tab); // emits code to stdout

// codegen_run  NOTE: takes ownership of tac (should free it itself if no longer needs it)

error convert_err_for_program_return(error err)
// may be this wont work as intended, if some action will be required, should be replaced with propper logic
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

    tac *tac_list = tac_generate(&err, &ast, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    tac_list = tac_optimise(&err, &tac_list, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    codegen_run(&err, &tac_list, attr_tab);
    if (err != NONE)
    {
        err = convert_err_for_program_return(err);
        destruct_attribute_table(&attr_tab);

        return err;
    }

    destruct_attribute_table(&attr_tab);

    return 0;
}
