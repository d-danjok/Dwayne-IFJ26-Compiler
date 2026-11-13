// @file codegen.h
//
// @author Sereda Maksym (xseredm00)

#ifndef CODEGEN_H
#define CODEGEN_H

#include "attribute_table.h"
#include "error.h"
#include "stack.h"
#include "tac.h"
#include "utils.h"

#include <stdbool.h>
#include <stdio.h>

static enum // not used in code, just for definition
{
    MOVE, // DEST_SYM SRC_VAR

    CREATEFRAME,
    PUSHFRAME, // (TF -> LF)
    POPFRAME,  // (LF -> TF, no TF -> err55)

    DEFVAR, // VAR

    CALL,   // LABEL
    RETURN, // (empty stack -> err56)

    LABEL,     // LITETAL (SAME LABEL -> err52)
    JUMP,      // LITERAL (NO LABEL -> err52)
    JUMPIFEQ,  // LITARAL SRC_SYM1 SRC_SYM2 (different types -> err53)
    JUMPIFNEQ, // LITARAL SRC_SYM1 SRC_SYM2 (different types -> err53)

    EXIT, // SRC_SYM

    BREAK,  // print state(position, GF,LF,TF, N of executed instructions) to @2
    DPRINT, // SRC_SYM (prints SYM to @2, could be disabled)

    /*Arithmetics*/
    ADD,  // DEST_VAR SRC_SYM1 SRC_SYM2 (SRC must be INT or FLOAT)
    SUB,  // DEST_VAR SRC_SYM1 SRC_SYM2 (SRC must be INT or FLOAT)
    MUL,  // DEST_VAR SRC_SYM1 SRC_SYM2 (SRC must be INT or FLOAT)
    DIV,  // DEST_VAR SRC_SYM1 SRC_SYM2 (only FLOAT, SRC_SYM2 == 0 -> err57)
    IDIV, // DEST_VAR SRC_SYM1 SRC_SYM2 (only INT, cuts remainder, SRC_SYM2 == 0 -> err57)
    LT,   // DEST_VAR SRC_SYM1 SRC_SYM2 (T/F -> DEST_VAR, SRC must be SAME type, cant compare nil -> err53)
    GT,   // DEST_VAR SRC_SYM1 SRC_SYM2 (T/F -> DEST_VAR, SRC must be SAME type, cant compare nil -> err53)
    EQ,   // DEST_VAR SRC_SYM1 SRC_SYM2 (T/F -> DEST_VAR, SRC must be SAME type)
    AND,  // DEST_VAR SRC_SYM1 SRC_SYM2 (T/F -> DEST_VAR, SRC must be BOOL)
    OR,   // DEST_VAR SRC_SYM1 SRC_SYM2 (T/F -> DEST_VAR, SRC must be BOOL)
    NOT,  // DEST_VAR SRC_SYM           (SRC_SYM must be BOOL)
    /*Conversions*/
    INT2FLOAT, // DEST_VAR SRC_SYM
    FLOAT2INT, // DEST_VAR SRC_SYM (cuts remainder)
    INT2CHAR,  // DEST_VAR SRC_SYM (should be from 0 to 127, else -> err58)
    STRI2INT,  // DEST_VAR SRC_SYM1 SRC_SYM2 (DEST <- ASCII.str of SRC_SYM1[SRC_SYM2], SYM2 not in SYM1.len -> err58)
    FLOAT2STR, // DEST_VAR SRC_SYM
    INT2STR,   // DEST_VAR SRC_SYM

    /*String*/
    CONCAT,  // DEST_VAR SRC_SYM1 SRC_SYM2
    STRLEN,  // DEST_VAR SRC_SYM
    GETCHAR, // DEST_VAR SRC_SYM1 SRC_SYM2 (DEST_VAR = SRC_SYM1[SRC_SYM2])
    SETCHAR, // DEST_VAR SRC_SYM1 SRC_SYM2 (DEST_VAR[SRC_SYM2] = SRC_SYM1[SRC_SYM2])

    /*Array*/
    NEWARRAY, // DEST_VAR SRC_SYM (SRC_SYM - size)
    ARRAYGET, // DEST_VAR SRC_VAR SRC_SYM (DEST_VAR = SRC_VAR[SRC_SYM], SRC_SYM not in SRC_VAR.len -> err58)
    ARRAYSET, // DEST_VAR DEST_SYM SRC_SYM (DEST_VAR[DEST_SYM1] = SRC_SYM, DEST_SYM not in SRC_VAR.len -> err58)

    /*Types*/
    TYPE,  // DEST_VAR SRC_SYM (writes INT|BOOL|FLOAT|STRING|NIL|ARRAY)
    ISINT, // DEST_VAR SRC_SYM (DEST_VAR <- decimal(SRC_SYM))

    /*Stack*/
    PUSHS, // SYMB
    POPS,  // VAR (stack -> VAR, empty stack -> err56)
    CLEARS,

    ADDS,
    SUBS,
    MULS,
    DIVS,
    IDIVS,
    LTS,
    GTS,
    EQS,
    ANDS,
    ORS,
    NOTS,
    INT2FLOATS,
    FLOAT2INTS,
    INT2CHARS,
    STRI2INTS,
    FLOAT2STRS,
    INT2STRS,
    TYPES,
    ISINTS,
} cg_operator;

/* @note:
        X - union
        label: LX
        func label: $f$X
        function arguments names: @$fX
        function result name: @$res
        temporary var: LF@$tX
*/

// @brief translates TAC into IFJcode26 and prints it to stdout (@1)
//
// @param err - pointer to error handling var
// @param tac - TAC list, function takes ownership and frees it
// @param attr_tab - attribute table for looking up identifiers and literals
void codegen_run(error *err, tac_list *tac_l, attribute_table *attr_tab);

#endif /* CODEGEN_H */