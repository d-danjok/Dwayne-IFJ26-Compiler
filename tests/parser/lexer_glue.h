// @file lexer_glue.h
//
// @brief test-only bridge between the parser and the lexer
//
// parser.c still calls the placeholder lexer_get_token(). This header is force-included (-include) when the
// tests compile parser.c and maps the placeholder to the real lexer, so the parser can be tested unmodified.
// Delete this file and the -include flag in run.py once parser_run() creates its own lexer.

#ifndef LEXER_GLUE_H
#define LEXER_GLUE_H

#include "lexer.h"

extern lexer *parser_test_lexer; // created by probe.c before parser_run()

#define lexer_get_token() lexer_get_next_token(parser_test_lexer, err)

#endif
