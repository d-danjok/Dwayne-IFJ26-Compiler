#!/usr/bin/env python3
"""End-to-end builtin tests with exact output and exit-code diagnostics."""

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "codegen"))
from target_executor import Executor, TargetError, UnsupportedInstruction

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCES = (
    "main.c", "ast.c", "attribute_table.c", "bst.c", "error.c", "lexer.c",
    "parser.c", "stack.c", "symtable.c", "utils.c", "semantic_analyser.c",
    "tac.c", "tac_optimiser.c", "codegen.c",
)


@dataclass(frozen=True)
class Case:
    name: str
    file: str
    input: str
    output: str
    interpreter_exit: int | None
    description: str
    failure_location: str
    compiler_exit: int = 0


CASES = (
    Case("nested_boolean", "nested_boolean.ifj26", "", "slice=BCD; value=69; guard=false", 0,
         "subStr produces BCD; nested strLength + ord produces 69; false && skips an invalid ord call.",
         "If output stops before 'slice=', inspect subStr; before 'value=69', inspect nested calls or ord; "
         "before 'guard=false', inspect Boolean short-circuiting."),
    Case("ord_past_end", "ord_past_end.ifj26", "", "before=67; past_end=-1; after=done", 0,
         "The index equal to a nonempty string's length must return -1, not raise an interpreter error.",
         "An exit 58 after 'past_end=' points to ord's missing index check before STRI2INT."),
    Case("ord_empty", "ord_empty.ifj26", "", "empty=-1; after=done", 0,
         "ord on an empty string must return -1.",
         "An exit 58 after 'empty=' points to ord's empty-string handling."),
    Case("substring_invalid", "substring_invalid.ifj26", "", "before=BCD; invalid=", 40,
         "subStr accepts [1,4) and must reject start == length with runtime exit 40.",
         "If 'after=unreachable' prints, subStr missed its bounds check; exit 58 means GETCHAR failed before the required exit 40."),
    Case("read_boolean_true", "read_boolean.ifj26", "true\n", "flag=true; branch=true", 0,
         "readBoolean reads true; toStr and an if condition consume the Boolean value.",
         "A failure before 'flag=' is in reading; after 'flag=', inspect toStr or Boolean condition handling."),
    Case("read_boolean_invalid", "read_boolean.ifj26", "invalid\n", "flag=false; branch=false", 0,
         "Invalid Boolean input must become false; the Boolean branch must take its else arm.",
         "A nonzero interpreter exit points to the READ nil-to-false conversion."),
    Case("read_boolean_eof", "read_boolean.ifj26", "", "flag=false; branch=false", 0,
         "EOF while reading a Boolean must become false.",
         "A nonzero interpreter exit points to the READ nil-to-false conversion."),
    Case("read_int_pair", "read_int_pair.ifj26", "7\n11\n", "sum=18", 0,
         "Two readInt calls consume two lines in order and their results can be added in an expression.",
         "A wrong sum points to input consumption order, call results, or expression evaluation."),
    Case("read_int_invalid", "read_int_pair.ifj26", "invalid\n11\n", "sum=11", 0,
         "Invalid integer input becomes zero without consuming the next readInt value.",
         "A nonzero exit points to the READ nil-to-zero conversion; a wrong sum points to input line handling."),
    Case("read_int_eof", "read_int_pair.ifj26", "", "sum=0", 0,
         "Both readInt calls at EOF return zero.",
         "A nonzero exit points to the READ nil-to-zero conversion."),
    Case("read_double_valid", "read_double.ifj26", "1.25\n", "double=1.25", 0,
         "readDouble parses a valid line and toStr formats the value to two decimal places.",
         "A failure after 'double=' points to READ float or FLOAT2STR."),
    Case("read_double_invalid", "read_double.ifj26", "1.25x\n", "double=0.00", 0,
         "A partially valid floating-point line is invalid and readDouble returns zero.",
         "A nonzero exit points to READ nil-to-zero conversion; wrong text points to FLOAT2STR."),
    Case("read_line_newline", "read_line.ifj26", "hello\n", "line=hello\n", 0,
         "readLine preserves the terminating LF in its returned string.",
         "A missing trailing newline points to READ string handling."),
    Case("read_line_eof", "read_line.ifj26", "", "line=", 0,
         "readLine at EOF returns an empty string.",
         "A nonzero exit points to READ nil-to-empty-string conversion."),
    Case("conversions", "conversions.ifj26", "", "int=-12; bool=true; trunc=-3; chr=A; length=3", 0,
         "toStr handles Int and Boolean, toInt truncates toward zero, chr decodes ASCII, and strLength counts a newline.",
         "The labelled segments isolate INT2STR, Boolean conversion, FLOAT2INT, INT2CHAR, and STRLEN."),
    Case("chr_invalid", "chr_invalid.ifj26", "", "before=ok; invalid=", 58,
         "chr(128) is outside ASCII 0..127 and must raise interpreter string error 58.",
         "If 'after=unreachable' prints, INT2CHAR failed to reject the invalid code."),
    Case("ord_negative", "ord_negative.ifj26", "", "negative=-1; after=done", 0,
         "ord with a negative index must return -1.",
         "An exit 58 after 'negative=' points to ord's missing index check."),
    Case("substring_end_too_large", "substring_end_too_large.ifj26", "", "before=ok; invalid=", 40,
         "subStr must reject an end index beyond the string length with language runtime error 40.",
         "A different exit points to subStr bounds handling or GETCHAR."),
    Case("substring_reversed", "substring_reversed.ifj26", "", "before=ok; invalid=", 40,
         "subStr must reject start > end with language runtime error 40.",
         "If it produces a value, the start/end ordering check was skipped."),
    Case("substring_empty_slice", "substring_empty_slice.ifj26", "", "empty=[]; next=ABCDE", 0,
         "Equal in-range start/end indices yield an empty string; [0,length) yields the full string.",
         "Failure before 'next=' points to the empty-slice boundary; afterward to the full-length boundary."),
    Case("to_str_unit", "to_str_unit.ifj26", "", "unit=(); after=done", 0,
         "toStr converts the Unit value to the literal text ().",
         "A failure after 'unit=' points to the Unit conversion path in toStr."),
    Case("argument_order", "argument_order.ifj26", "", "order=14BCD; done", 0,
         "Function arguments evaluate from left to right before subStr receives their results.",
         "Wrong marker order points to TAC argument evaluation; wrong slice points to parameter passing."),
    Case("read_int_plus", "read_int_pair.ifj26", "+7\n11\n", "sum=18", 0,
         "A leading plus is a valid decimal Int input.",
         "A wrong sum points to READ int parsing."),
    Case("read_int_whitespace", "read_int_pair.ifj26", " 7\n11\n", "sum=11", 0,
         "Leading whitespace invalidates a readInt line; the next line still reads as 11.",
         "A sum of 18 means READ incorrectly ignored surrounding whitespace."),
    Case("read_int_underscore", "read_int_pair.ifj26", "1_0\n11\n", "sum=11", 0,
         "Underscores in readInt input are invalid, even though source literals may allow them.",
         "A sum of 21 means READ incorrectly accepted an underscored input value."),
    Case("read_int_overflow", "read_int_pair.ifj26", "9223372036854775808\n11\n", "sum=11", 0,
         "An Int above the 64-bit maximum becomes zero; the following input remains readable.",
         "A wrong sum points to READ integer range checks or input consumption."),
    Case("read_double_exponent", "read_double.ifj26", "1e2\n", "double=100.00", 0,
         "readDouble accepts exponent notation and toStr formats two decimal places.",
         "A zero result points to READ float parsing; wrong text points to FLOAT2STR."),
    Case("read_double_whitespace", "read_double.ifj26", "1.25 \n", "double=0.00", 0,
         "Trailing whitespace invalidates a readDouble line.",
         "A result of 1.25 means READ ignored forbidden whitespace."),
    Case("read_line_no_newline", "read_line.ifj26", "hello", "line=hello", 0,
         "readLine preserves a final nonempty line even without LF.",
         "An empty result points to EOF handling in READ string."),
    Case("substring_negative_start", "substring_negative_start.ifj26", "", "before=ok; invalid=", 40,
         "subStr rejects a negative start with language runtime error 40.",
         "A different exit points to the start-index guard."),
    Case("substring_negative_end", "substring_negative_end.ifj26", "", "before=ok; invalid=", 40,
         "subStr rejects a negative end with language runtime error 40.",
         "A different exit points to the end-index or start/end ordering guard."),
    Case("substring_empty_source", "substring_empty_source.ifj26", "", "before=ok; invalid=", 40,
         "Even [0,0) is invalid when the source string is empty, because start >= length.",
         "A normal exit means the empty-source boundary was accepted incorrectly."),
    Case("substring_last_character", "substring_last_character.ifj26", "", "last=E; code=69", 0,
         "The final valid character is available through subStr and ord.",
         "A failure after 'last=' points to subStr end == length; after 'code=' to ord index length - 1."),
    Case("chr_negative", "chr_negative.ifj26", "", "before=ok; invalid=", 58,
         "chr rejects a negative ASCII code with interpreter error 58.",
         "A normal exit means INT2CHAR accepted an invalid code."),
    Case("semantic_wrong_arity", "semantic_wrong_arity.ifj26", "", "", None,
         "ord called with one argument must fail at compile time with code 5.",
         "A different compiler code points to built-in arity checking.", 5),
    Case("semantic_wrong_type", "semantic_wrong_type.ifj26", "", "", None,
         "subStr with a String end argument must fail at compile time with code 5.",
         "A different compiler code points to built-in parameter type checking.", 5),
    Case("semantic_any_restriction", "semantic_any_restriction.ifj26", "", "", None,
         "toInt accepts Double, not String, and must fail at compile time with code 5.",
         "A different compiler code points to built-in parameter type checking.", 5),
    Case("semantic_noarg_arity", "semantic_noarg_arity.ifj26", "", "", None,
         "readInt takes no arguments and must fail at compile time with code 5.",
         "A different compiler code points to built-in arity checking.", 5),
    Case("algorithm_pipeline_valid", "algorithm_pipeline.ifj26", "2\n", "letter=C", 0,
         "readInt, range checks, Boolean &&, a user-defined function, chr, return, and print work together.",
         "A wrong result can originate in input, branch selection, call/return, or chr; the final letter identifies the value."),
    Case("algorithm_pipeline_invalid", "algorithm_pipeline.ifj26", "-1\n", "letter=invalid", 0,
         "An out-of-range input takes the else branch without calling chr.",
         "A runtime 58 means the invalid input reached chr; wrong output points to Boolean branch handling."),
    Case("nested_read_order", "nested_read_order.ifj26", "9\n2\n", "difference=7", 0,
         "Nested readInt calls in subtraction evaluate left to right, giving 9 - 2.",
         "A result of -7 indicates reversed evaluation or argument order."),
    Case("boolean_or_guard", "boolean_or_guard.ifj26", "", "or=true", 0,
         "true || skips an out-of-range ord on its right-hand side.",
         "Exit 58 points to missing Boolean OR short-circuiting."),
    Case("print_escaping", "print_escaping.ifj26", "", "line 1\n# \\ end; rounded=1.23", 0,
         "String escapes, literal # and backslash, exact print bytes, and two-decimal toStr output survive code generation.",
         "A code parse error points to string escaping; a numeric mismatch points to FLOAT2STR."),
    Case("semantic_length_type", "semantic_length_type.ifj26", "", "", None,
         "strLength requires String, so an Int argument must fail at compile time with code 5.",
         "A different compiler code points to built-in parameter type checking.", 5),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-k", "--filter", default="", help="run cases whose names contain this text")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--interpreter", help="official interpreter command, e.g. '/path/ic26int {source}'")
    args = parser.parse_args()
    if args.interpreter and "{source}" not in args.interpreter:
        parser.error("--interpreter must include {source}")
    cases = [case for case in CASES if args.filter in case.name]
    if not cases:
        parser.error("no matching cases")

    with tempfile.TemporaryDirectory(prefix="ifj-builtins-") as temp:
        tempdir = Path(temp)
        binary = tempdir / "compiler"
        build = subprocess.run(
            shlex.split(args.cc) + ["-std=c11", "-O2", "-o", str(binary)]
            + [str(ROOT / "src" / name) for name in SOURCES],
            text=True, capture_output=True,
        )
        if build.returncode:
            print(f"BUILD FAILED (exit {build.returncode})\n{build.stderr}")
            return 2

        baseline_source = tempdir / "baseline.ifj26"
        baseline_source.write_text("import ifj26.*\n@main def main(): Unit =\n  ()\n")
        with baseline_source.open("rb") as stream:
            baseline = subprocess.run([str(binary)], stdin=stream,
                                      capture_output=True, text=True, timeout=10)
        if baseline.returncode:
            print(f"BASELINE FAILURE: a minimal program exits {baseline.returncode}; "
                  "the compiler is failing before built-in behavior can be evaluated.")

        failed = 0
        for case in cases:
            source = HERE / case.file
            with source.open("rb") as stream:
                compiled = subprocess.run([str(binary)], stdin=stream, text=True,
                                          capture_output=True, timeout=10)
            actual_output = ""
            actual_exit = None
            execution_error = ""
            if compiled.returncode == 0 and case.compiler_exit == 0:
                if args.interpreter:
                    program = tempdir / "program.ifjcode"
                    program.write_text(compiled.stdout)
                    command = [part.replace("{source}", str(program)) for part in shlex.split(args.interpreter)]
                    executed = subprocess.run(command, input=case.input, text=True,
                                              capture_output=True, timeout=10)
                    actual_output, actual_exit = executed.stdout, executed.returncode
                    execution_error = executed.stderr.strip()
                else:
                    executor = None
                    try:
                        executor = Executor(compiled.stdout, case.input)
                        actual_exit = executor.run()
                        actual_output = executor.output
                    except TargetError as exc:
                        actual_exit = exc.code
                        actual_output = executor.output if executor is not None else ""
                        execution_error = str(exc)
                    except UnsupportedInstruction as exc:
                        print(f"LIMITATION {case.name}: test executor cannot run {exc}")
                        return 2

            ok = compiled.returncode == case.compiler_exit and actual_exit == case.interpreter_exit and actual_output == case.output
            print(f"{'PASS' if ok else 'FAIL'} {case.name}: {case.description}")
            if not ok:
                failed += 1
                print(f"  Expected: compiler {case.compiler_exit}, interpreter {case.interpreter_exit}, stdout {case.output!r}")
                print(f"  Actual:   compiler {compiled.returncode}, interpreter {actual_exit}, stdout {actual_output!r}")
                if compiled.returncode and case.compiler_exit == 0:
                    print("  Where:    Compiler pipeline; no IFJcode26 or interpreter result was produced.")
                else:
                    print(f"  Where:    {case.failure_location}")
                if compiled.stderr.strip():
                    print(f"  Compiler stderr: {compiled.stderr.strip()}")
                if execution_error:
                    print(f"  Interpreter: {execution_error}")
        print(f"{len(cases) - failed}/{len(cases)} passed")
        return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
