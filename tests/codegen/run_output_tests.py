#!/usr/bin/env python3
"""Compare stdout of actual TAC -> codegen programs with independent expected output."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from run_tests import ROOT, build, locations
from target_executor import Executor, TargetError, UnsupportedInstruction, self_check

# These are ordinary successful-program expectations, not tests that expect bugs.
# source descriptions are compact; C fixtures construct matching LL-shaped ASTs.
CASES = [
 ('arithmetic_output', 'print((2 + 3) * 4)', '', '20', 'expression'),
 ('negative_division', 'print(-7 / 2)', '', '-3', 'expression'),
 ('invoice', 'price=25; count=4; print(price*count-10)', '', '90', 'expression'),
 ('sum_loop', 'sum 0..9 using a while loop', '', '45', 'loop'),
 ('nested_loops', '3 x 4 nested iterations; print(total)', '', '12', 'loop'),
 ('dead_branch', 'if false then print("bad") else print("ok")', '', 'ok', 'branch'),
 ('no_else', 'if false then print("BAD"); print("ok")', '', 'ok', 'branch'),
 ('input_branch', 'readInt(); if x>0 then positive else other', '7\n', 'positive', 'branch'),
 ('input_branch', 'readInt(); if x>0 then positive else other', '-2\n', 'other', 'branch'),
 ('input_branch', 'readInt(); if x>0 then positive else other', '0\n', 'other', 'branch'),
 ('scope_shadow', 'outer x=7; inner x=9; print(inner); print(outer)', '', '97', 'scope'),
 ('plain_call', 'def f(): Int = 42; print(f())', '', '42', 'call'),
 ('parameter_output', 'def f(x: Int): Int = x; print(f(7))', '', '7', 'call'),
 ('factorial', 'recursive fact(6), explicit returns', '', '720', 'call'),
 ('early_return', 'return 42 before print("BAD"); print(f())', '', '42', 'return'),
 ('implicit_return', 'def f(): Int = { 42 }; print(f())', '', '42', 'return'),
 ('unit_return', 'def f(): Unit = 42; print(f()==())', '', 'true', 'return'),
 ('short_or', 'print(true || probe()); probe prints SIDE', '', 'true', 'boolean'),
 ('short_and', 'print(false && probe()); probe prints SIDE', '', 'false', 'boolean'),
 ('conversion', 'print(toStr(Int-to-Double(2+3)))', '', '5.00', 'conversion'),
 ('nested_builtin', 'print(ord("AZ", toInt(1.0)))', '', '90', 'call'),
 ('string_output', 'print string containing newline, #, space and backslash', '', 'line 1\n# \\ end', 'expression'),
 ('concat', 'print("Hello " + "world")', '', 'Hello world', 'expression'),
 ('boolean_text', 'print(strLength(toStr(true)))', '', '4', 'conversion'),
 ('print_value', 'print(print(1))', '', '1()', 'call'),
 ('read_output', 'print(readInt())', '12\n', '12', 'call'),
 ('read_output', 'print(readInt()) on invalid input', 'bad\n', '0', 'call'),
 ('read_output', 'print(readInt()) at EOF', '', '0', 'call'),
 ('ord_output', 'print(ord("AZ", 1))', '', '90', 'call'),
 ('substring_output', 'print(subStr("abcd", 1, 3))', '', 'bc', 'call'),
]


def codegen_location(name):
    anchor = {'parameter_output':'case TAC_PARAM:', 'factorial':'case TAC_PARAM:',
        'plain_call':'case TAC_CALL:', 'nested_builtin':'case TAC_PARAM:',
        'boolean_text':'strcmp(t->arg1.identifier_name, "toStr")',
        'print_value':'strcmp(t->arg1.identifier_name, "print")',
        'read_output':'strcmp(t->arg1.identifier_name, "readInt")',
        'substring_output':'strcmp(t->arg1.identifier_name, "subStr")',
        'unit_return':'case TAC_RETURN:', 'implicit_return':'case TAC_FUNC_END:'}.get(name,'switch (t->op)')
    text=(ROOT/'src/codegen.c').read_text();at=text.find(anchor)
    return 'src/codegen.c'+(':'+str(text[:at].count('\n')+1) if at>=0 else ' (anchor changed)')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--optimize',action='store_true',help='insert the real optimizer between TAC and codegen')
    parser.add_argument('--filter',default='')
    parser.add_argument('--save-generated',type=Path,help='save generated IFJcode26 for inspecting failures')
    parser.add_argument('--interpreter',help='official interpreter command template, e.g. "/path/to/interpreter {source}"; executed without a shell')
    args=parser.parse_args()
    if args.interpreter and '{source}' not in args.interpreter:
        parser.error('--interpreter must contain {source} for the generated program path')
    self_check()
    print('PROGRAM OUTPUT TESTS: real tac_generate -> '+('tac_optimize -> ' if args.optimize else '')+'real codegen_run')
    print('Execute generated IFJcode26, compare exact stdout and require exit 0.')
    print('Backend: '+('external interpreter' if args.interpreter else 'test-only IFJcode26 subset executor (not official)'))
    print('LL-shaped AST inputs bypass the unfinished parser/semantic analyser.\n')
    if args.save_generated: args.save_generated.mkdir(parents=True,exist_ok=True)
    failed=total=limitations=0
    cases=list(CASES)
    if args.optimize: cases.append(('main_after_function','fact defined before main; print(fact(6))','','720','call'))
    with tempfile.TemporaryDirectory(prefix='ifj-output-') as tmp:
        directory=Path(tmp)
        try: binary=build(directory,args.cc,args.sanitize,with_codegen=True)
        except (OSError,RuntimeError) as exc: print(f'[BUILD ERROR] {exc}');return 2
        for name,example,stdin,expected,group in cases:
            if args.filter and args.filter not in name:continue
            total+=1;vm=None;actual='';exit_code=None;detail='';runtime_fault=False
            env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
            env['OPT_TEST_FAIL_ALLOC']='0';env['TAC_TEST_FAIL_ALLOC']='0'
            try:
                generated=subprocess.run([str(binary),'codegen-opt' if args.optimize else 'codegen',name],
                    capture_output=True,text=True,timeout=15,env=env)
                target=directory/f'{total:02}_{name}.ifjcode26'
                target.write_text(generated.stdout)
                if args.save_generated: (args.save_generated/target.name).write_text(generated.stdout)
                if generated.returncode:
                    detail=f'compiler fixture exited {generated.returncode}: {generated.stderr.strip()}'
                elif args.interpreter:
                    command=[part.replace('{source}',str(target)) for part in shlex.split(args.interpreter)]
                    execution=subprocess.run(command,input=stdin,capture_output=True,text=True,timeout=15)
                    actual,exit_code=execution.stdout,execution.returncode
                    detail=execution.stderr.strip()
                else:
                    vm=Executor(generated.stdout,stdin)
                    try:exit_code=vm.run()
                    except TargetError as exc:
                        exit_code=exc.code;detail=str(exc);runtime_fault=True
                    actual=vm.output
            except UnsupportedInstruction as exc:
                limitations+=1;detail=f'Test executor does not implement {exc}; rerun with --interpreter';runtime_fault=True
            except (OSError,subprocess.TimeoutExpired,TargetError,ValueError,IndexError) as exc:
                detail=str(exc)
            passed=actual==expected and exit_code==0
            print(f'[{"PASS" if passed else "FAIL"} {total:02}] {name}')
            if not passed:
                print(f'  Program:  {example}\n  Input:    {stdin!r}\n  Expected: {expected!r}\n  Actual:   {actual!r} (exit={exit_code})')
                failed+=1
                if detail: print('  Error:    '+detail)
                print(f'  Inspect:  {locations(group)}; {codegen_location(name)}')
                if vm and runtime_fault and vm.pc<len(vm.instructions):
                    line,_,instruction=vm.instructions[vm.pc]
                    print(f'  Generated line {line}: {instruction}')
                elif vm and vm.writes:
                    print('  Writes:   '+'; '.join(f'line {line}: {value!r}' for line,value in vm.writes))
            print()
    print(f'OUTPUT RESULTS: {total-failed}/{total} passed; {failed} failed; {limitations} executor limitations.')
    if not total: return 2
    return 1 if failed else 0


if __name__=='__main__':sys.exit(main())
