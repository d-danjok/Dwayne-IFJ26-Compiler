#!/usr/bin/env python3
"""Advanced TAC and optimizer tests. Only the test directory is writable by this suite."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
# Search upwards instead of assuming a particular folder depth or working directory.
ROOT = next((p for p in HERE.parents if (p/'src/tac.c').is_file() and (p/'docs/LL.md').is_file()), None)
if ROOT is None:
    raise SystemExit('Cannot find repository root (expected src/tac.c and docs/LL.md).')


class Fault(Exception):
    pass


def normalized(value):
    kind, data = value
    if kind == 'float': data = data.hex() if math.isfinite(data) else 'nan' if math.isnan(data) else str(data)
    return kind, data


class TacVM:
    """Reference TAC evaluator, not a replacement for the official target interpreter.

    CALL_PARAM pushes a call argument (TAC emits them last arg first), CALL consumes its
    own last N of them and PARAM receives the callee's formal parameters in order.
    """
    def __init__(self, code, inputs=(7,)):
        self.code = code
        self.inputs = list(inputs)
        self.labels = {}
        self.functions = {}
        self.output = ''
        self.events = []
        self.pc = 0
        self.env = {}
        self.pending = []
        self.receiving = []
        self.calls = []
        for i, (op, a, b, r) in enumerate(code):
            if op == 'LABEL':
                if r[2] in self.labels: raise Fault('duplicate label')
                self.labels[r[2]] = i
            if op == 'FUNC_START': self.functions[r[2]] = i
        for op, a, b, r in code:
            for target in ([a,b] if op == 'IF_JUMP' else [r] if op == 'GOTO' else []):
                if target[0] != 'label' or target[2] not in self.labels: raise Fault(f'missing label {target}')
        main = next((r[2] for op,a,b,r in code if op == 'MAIN'), None)
        if main is not None:
            if main not in self.functions: raise Fault(f'missing main function {main}')
            self.pc = self.functions[main] + 1

    def get(self, a):
        kind, typ, value = a
        if kind == 'empty': return 'unit', None
        if kind == 'literal': return typ, float.fromhex(value) if typ == 'float' else value
        key = kind, value
        if kind not in ('id','temp') or key not in self.env: raise Fault(f'uninitialized/invalid operand {a}')
        return self.env[key]

    def put(self, a, value):
        if a[0] == 'empty': return
        if a[0] not in ('id','temp'): raise Fault(f'invalid destination {a}')
        self.env[a[0], a[2]] = value

    def typed(self, a, typ):
        kind, value = self.get(a)
        if kind != typ: raise Fault(f'expected {typ}, got {kind}')
        return value

    def builtin(self, name, args):
        self.events.append((name, tuple(normalized(v) for v in args)))
        if name == 'print':
            typ, value = args[0]
            self.output += '()' if typ == 'unit' else str(value).lower() if typ == 'bool' else str(value)
            return 'unit', None
        if name == 'readInt': return 'int', self.inputs.pop(0) if self.inputs else 0
        if name == 'identity': return args[0]
        if name == 'sum': return ('float' if any(t=='float' for t,v in args) else 'int'), sum(v for t,v in args)
        if name == 'INT_TO_FLOAT':
            if len(args)!=1 or args[0][0]!='int': raise Fault('invalid INT_TO_FLOAT arguments')
            return 'float', float(args[0][1])
        if name == 'toStr':
            typ,value=args[0]
            return 'string', '()' if typ=='unit' else f'{value:.2f}' if typ=='float' else str(value).lower() if typ=='bool' else str(value)
        raise Fault(f'unsupported reference builtin {name}')

    def execute(self, limit=100000):
        for _ in range(limit):
            if self.pc >= len(self.code): return 'unit', None
            op,a,b,r = self.code[self.pc]
            following = self.pc+1
            if op in ('LABEL','MAIN'): pass
            elif op == 'FUNC_START': raise Fault('fell through into a function')
            elif op in ('ASSIGNMENT','VAR_DEF','VAL_DEF'): self.put(r,self.get(a))
            elif op == 'CALL_PARAM': self.pending.append(self.get(a))
            elif op == 'PARAM':
                if not self.receiving: raise Fault('PARAM without an incoming argument')
                self.put(a,self.receiving.pop(0))
            elif op == 'CALL':
                count=self.typed(b,'int')
                if count<0 or len(self.pending)<count: raise Fault('not enough call arguments')
                args=self.pending[-count:][::-1] if count else []  # CALL_PARAMs come last arg first
                if count: del self.pending[-count:]
                name=a[2]
                if name in self.functions:
                    arity={'main':0,'fact':1,'f':0,'probe':0}.get(name)
                    if arity != count: raise Fault(f'fixture arity mismatch for {name}')
                    self.calls.append((following,self.env,self.receiving,r))
                    self.env={};self.receiving=list(args);self.pc=self.functions[name]+1;continue
                self.put(r,self.builtin(name,args))
            elif op in ('RETURN','FUNC_END'):
                value=self.get(r) if op=='RETURN' else ('unit',None)
                if not self.calls: return value
                following,self.env,self.receiving,destination=self.calls.pop()
                self.put(destination,value)
            elif op == 'GOTO': following=self.labels[r[2]]
            elif op == 'IF_JUMP': following=self.labels[a[2] if self.typed(r,'bool') else b[2]]
            elif op in ('PLUS','MINUS','MUL','DIV','UNARY_MINUS'):
                typ,left=self.get(a)
                if op=='UNARY_MINUS': value=-left
                else:
                    rt,right=self.get(b)
                    if typ!=rt: raise Fault('mixed arithmetic types without conversion')
                    if typ=='string' and op=='PLUS': value=left+right
                    elif typ not in ('int','float'): raise Fault('invalid arithmetic type')
                    elif op=='PLUS': value=left+right
                    elif op=='MINUS': value=left-right
                    elif op=='MUL': value=left*right
                    elif right==0: raise Fault('division by zero')
                    elif typ=='int': value=(abs(left)//abs(right))*(-1 if (left<0)!=(right<0) else 1)
                    else: value=left/right
                # Internal TAC uses long long. These boundary tests only verify
                # conservative folding, not language-level overflow semantics.
                if typ=='int' and not -(2**63)<=value<2**63: raise Fault('internal integer overflow')
                self.put(r,(typ,value))
            elif op in ('EQUALS','GREATER','LESSER'):
                left,right=self.get(a),self.get(b)
                if left[0]!=right[0] and 'unit' not in (left[0],right[0]): raise Fault('comparison type mismatch')
                value=left==right if op=='EQUALS' else left[1]>right[1] if op=='GREATER' else left[1]<right[1]
                # Tuple equality can treat the same NaN object as equal; use numeric equality explicitly.
                if op=='EQUALS' and left[0]==right[0]=='float': value=left[1]==right[1]
                self.put(r,('bool',value))
            elif op in ('AND','OR','NOT'):
                left=self.typed(a,'bool')
                if op=='NOT': value=not left
                else:
                    right=self.typed(b,'bool');value=(left and right) if op=='AND' else (left or right)
                self.put(r,('bool',value))
            else: raise Fault(f'unsupported TAC operator {op}')
            self.pc=following
        raise Fault('step limit exceeded')


def observe(code, inputs=(7,)):
    vm=None
    try:
        vm=TacVM(code,inputs);value=vm.execute();failure=None
    except Fault as exc:
        value=('unit',None);failure=str(exc)
    # Pure intrinsic calls may be removed. Only observable built-ins participate
    # in the effect trace; their argument values and order must remain identical.
    effects=tuple(event for event in vm.events if event[0] in ('print','readInt')) if vm else ()
    return {'result':normalized(value),'output':vm.output if vm else '', 'fault':failure,'effects':effects}, vm.pc if vm else None


def reference_checks():
    e=['empty','none',None]
    lit=lambda n:['literal','int',n]
    t=['temp','int',0]
    data,_=observe([['DIV',lit(-7),lit(2),t],['RETURN',e,e,t]])
    assert data['result']==('int',-3) and data['fault'] is None
    data,_=observe([['DIV',lit(1),lit(0),t]])
    assert data['fault']=='division by zero'
    data,_=observe([['RETURN',e,e,t]])
    assert 'uninitialized' in data['fault']
    data,_=observe([['EQUALS',['literal','float','nan'],['literal','float','nan'],['temp','bool',0]],['RETURN',e,e,['temp','bool',0]]])
    assert data['result']==('bool',False)


RAW = {
 'constant_chain':('int',6), 'long_chain':('int',-96),
 'alias_copy':('int',7), 'alias_overwrite':('int',7), 'alias_cycle':('int',7),
 'loop_sum':('int',45), 'branch_join':('int',30),
 'dead_effect':('int',7), 'live_effect':('int',7), 'dead_declaration':('int',7),
 'call_barrier':('int',7), 'param_barrier':('int',7), 'label_barrier':('int',7),
 'convert_constant':('float','0x1.5000000000000p+5'),
 'convert_large':('float',float(9007199254740993).hex()),
 'convert_boundary':('float',float(42).hex()), 'convert_nested':('float',float(47).hex()),
 'ordinary_call':('int',42), 'owned_strings':('string','borrowed # string'),
 'negative_div':('int',-3), 'float_div':('float',float(4.5).hex()),
 'signed_zero':('float',(-0.0).hex()), 'nan_equal':('bool',False),
 'not':('bool',True), 'bool_and':('bool',False), 'bool_or':('bool',True),
}
TRAPS = {'zero_div':'division by zero','unused_trap':'division by zero','float_zero':'division by zero',
 'int_max_add':'internal integer overflow','int_min_sub':'internal integer overflow','int_min_neg':'internal integer overflow',
 'int_min_div':'internal integer overflow','mul_overflow':'internal integer overflow'}
# name: source example, output, source group
AST = {
 'invoice':('price=25; count=4; print(price*count-10)','90','expression'),
 'sum_loop':('sum integers 0..9 in a while loop; print(sum)','45','loop'),
 'nested_loops':('3 outer iterations x 4 inner iterations; print(total)','12','loop'),
 'input_branch':('readInt(); if x>0 then print("positive") else print("other")','positive','branch'),
 'dead_branch':('if false then print("bad") else print("ok")','ok','branch'),
 'scope_shadow':('outer x=7; inner x=9; print(inner x); print(outer x)','97','scope'),
 'factorial':('recursive fact(6), explicit returns in both branches','720','call'),
 'main_after_function':('fact defined before @main; print(fact(6))','720','call'),
 'nested_calls':('print(sum(5, identity(7))) [test reference callees]','12','call'),
 'conversion':('print(toStr(Int-to-Double(2+3)))','5.00','conversion'),
 'implicit_return':('def f(): Int = { 42 }; print(f())','42','return'),
 'unit_return':('def f(): Unit = 42; print(f()==())','true','return'),
 'short_or':('print(true || probe()); probe prints SIDE','true','boolean'),
 'short_and':('print(false && probe()); probe prints SIDE','false','boolean'),
 'concat':('print("Hello " + "world") after semantic CONCAT rewrite','Hello world','expression'),
}
ANCHORS = {
 'optimizer':[('src/tac_optimizer.c','tac_list *tac_optimize('),('src/tac_optimizer.c','static bool tac_fold_constants(')],
 'expression':[('src/tac.c','if (expr->type == AST_NODE_BINARY_OPERATION)')],
 'loop':[('src/tac.c','static tac_argument tac_emit_while(')],
 'branch':[('src/tac.c','tac_argument end_label = create_label(prog);')],
 'scope':[('src/tac.c','static tac_argument create_new_id(')],
 'call':[('src/tac.c','static tac_argument tac_emit_call_param('),('src/tac.c','static tac_argument tac_emit_func(')],
 'conversion':[('src/tac.c','if (expr->type == AST_NODE_INT_TO_DOUBLE_CONVERSION)')],
 'return':[('src/tac.c','if (body->left_child->type != AST_NODE_BLOCK)')],
 'boolean':[('src/tac.c','tac_argument arg2 = tac_expr_emit(err, expr->right_child, prog);')],
}


def locations(group):
    out=[]
    for name,anchor in ANCHORS[group]:
        text=(ROOT/name).read_text();at=text.find(anchor)
        out.append(f'{name}:{text[:at].count(chr(10))+1}' if at>=0 else name+' (source anchor changed)')
    return '; '.join(out)


def build(directory,cc,sanitize,with_codegen=False):
    flags=[cc,'-std=c11','-g','-I',str(ROOT/'src')]
    if sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    obj=directory/'optimizer.o';tac_obj=directory/'tac.o';exe=directory/'driver'
    commands=[flags+['-Dutils_safe_malloc=test_tac_malloc','-c',str(ROOT/'src/tac.c'),'-o',str(tac_obj)],
              flags+['-Dutils_safe_malloc=test_optimizer_malloc','-c',str(ROOT/'src/tac_optimizer.c'),'-o',str(obj)],
              flags+[str(HERE/'tac_optimizer_driver.c'),str(HERE/'ast_print.c'),str(obj),str(tac_obj),*[str(ROOT/'src'/f) for f in ('ast.c','attribute_table.c','utils.c','stack.c')],'-lm','-o',str(exe)]]
    if with_codegen:
        codegen_obj=directory/'codegen.o'
        commands.insert(-1,flags+['-Dtac_list_destruct=test_codegen_destruct','-c',str(ROOT/'src/codegen.c'),'-o',str(codegen_obj)])
        commands[-1].extend(['-DTEST_CODEGEN',str(codegen_obj)])
    for command in commands:
        result=subprocess.run(command,capture_output=True,text=True)
        if result.stderr:print(result.stderr.rstrip())
        if result.returncode:raise RuntimeError('test build failed (compiler diagnostics above)')
    return exe


def invoke(exe,kind,name,seed=1,fail=0,tac_fail=0):
    env=os.environ.copy();env['OPT_TEST_FAIL_ALLOC']=str(fail);env['TAC_TEST_FAIL_ALLOC']=str(tac_fail)
    env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    result=subprocess.run([str(exe),kind,name,str(seed)],capture_output=True,text=True,timeout=15,env=env)
    if result.returncode:raise RuntimeError(f'fixture exit {result.returncode}: {result.stderr.strip()}')
    try:return json.loads(result.stdout)
    except ValueError:raise RuntimeError(f'fixture emitted invalid JSON: {result.stdout[:300]} {result.stderr[:300]}')


def structural(name,data):
    after=data['after'];ops=[x[0] for x in after]
    if name in ('constant_chain','long_chain','random','negative_div','float_div','not','bool_and','bool_or'):
        assert ops==['RETURN'],f'constant expression not reduced to RETURN literal: {ops[:20]}'
    if name in ('dead_effect','dead_declaration'):assert 'CALL' not in ops,'unreachable side effect retained'
    if name=='live_effect':assert 'CALL' in ops,'reachable print removed'
    if name=='dead_declaration':assert 'VAR_DEF' in ops,'declaration lost during unreachable-code elimination'
    if name in ('call_barrier','param_barrier','label_barrier'):assert after[-1][3][0]=='id','propagation crossed a boundary'
    if name in ('ordinary_call','convert_large','convert_boundary'):assert 'CALL' in ops,'non-foldable call was removed'
    if name in ('convert_constant','convert_nested'):assert all(x[1][2]!='INT_TO_FLOAT' for x in after if x[0]=='CALL'),'constant conversion was not folded'
    if name in TRAPS or name in ('nan_equal','float_overflow'):assert any(o in ops for o in ('DIV','PLUS','MINUS','UNARY_MINUS','MUL','EQUALS')),'unsafe fold/trapping operation removed'


def check_data(kind,name,data,seed):
    assert 'generation_error' not in data,f'TAC generation rejected example, error={data.get("generation_error")}'
    assert data.get('ast_consumed'), 'tac_generate did not consume its AST'
    assert not data['failed'] and data['optimizer_error']==0,'optimizer unexpectedly reported failure'
    assert data['metadata'] and data['again_metadata'],'size/IDs/tail/cursor metadata inconsistent'
    assert data['again_error']==0,'second optimization failed'
    assert data['after']==data['again'],'optimization is not idempotent'
    if name in ('empty','main_first','main_middle','main_last','main_absent'):
        if name.startswith('main_') and name!='main_absent':assert data['after'][0][0]=='MAIN','MAIN was not moved to the front'
        expected=([x for x in data['before'] if x[0]=='MAIN']+[x for x in data['before'] if x[0]!='MAIN'])
        assert data['after']==expected,'MAIN relocation lost/reordered another instruction'
        return
    before,bpc=observe(data['before']);after,apc=observe(data['after'])
    assert before==after,f'optimizer changed behaviour:\n    before={before}\n    after ={after}\n    TAC PCs: before={bpc}, after={apc}'
    if kind=='ast':
        assert after['fault'] is None,f'TAC execution failed: {after["fault"]}; instruction={apc}'
        assert after['output']==AST[name][1],f'expected stdout={AST[name][1]!r}; actual={after["output"]!r} (same before/after optimization)'
    else:
        if name in TRAPS:assert after['fault']==TRAPS[name],f'expected preserved trap {TRAPS[name]!r}, got {after}'
        else:
            expected=RAW.get(name)
            if name=='random':
                value=seed%31-15
                for k in range(1,9):value+=((seed+k*7)%13-6)*(1 if k%2 else -1)
                expected=('int',value)
            assert after['fault'] is None,f'unexpected execution failure: {after}'
            if expected is not None:assert after['result']==expected,f'expected result={expected!r}, got {after["result"]!r}'
        if name=='live_effect':assert after['output']=='99','print output was not preserved'
        structural(name,data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--filter',default='',help='substring in raw/NAME or ast/NAME')
    parser.add_argument('--random',type=int,default=64,help='number of reproducible arithmetic seeds (default 64)')
    parser.add_argument('--no-fault-injection',action='store_true')
    args=parser.parse_args()
    if args.random<0:parser.error('--random must be nonnegative')
    reference_checks()
    print('Advanced TAC / optimizer tests - LL-shaped ASTs and independent TAC execution')
    print('Production files are compiled unchanged. Parser, semantic analyser and codegen are not invoked.')
    print('Each example checks the assignment oracle AND raw/optimized equivalence.\n')
    total=failed=0
    failures=[]
    with tempfile.TemporaryDirectory(prefix='ifj-advanced-') as directory:
        try:exe=build(Path(directory),args.cc,args.sanitize)
        except (RuntimeError,OSError) as exc:print(f'[BUILD ERROR] {exc}');return 2
        cases=[('raw',n,1) for n in [*RAW,*TRAPS,'float_overflow','empty','null','main_first','main_middle','main_last','main_absent']]
        cases += [('ast',n,1) for n in AST]
        cases += [('raw','random',seed) for seed in range(args.random)]
        for kind,name,seed in cases:
            title=f'{kind}/{name}'+(f' seed={seed}' if name=='random' else '')
            if args.filter and args.filter not in title:continue
            total+=1;data=None
            try:
                data=invoke(exe,kind,name,seed)
                if name=='null':assert data['null_ok'],'NULL input was not rejected'
                else:check_data(kind,name,data,seed)
                print(f'[PASS {total:03}] {title}')
            except (AssertionError,RuntimeError,subprocess.TimeoutExpired,Fault,KeyError,ValueError) as exc:
                failed+=1;failures.append(title)
                example=AST[name][0] if kind=='ast' else name.replace('_',' ')
                group=AST[name][2] if kind=='ast' else 'optimizer'
                print(f'[FAIL {total:03}] {title}\n  Example: {example}\n  Error: {exc}\n  Inspect: {locations(group)}')
            if data and name!='null' and 'after' in data and not args.no_fault_injection:
                for allocation in range(1,data['allocations']+1):
                    total+=1;fault_title=f'{title} allocation #{allocation}'
                    try:
                        fault=invoke(exe,kind,name,seed,allocation)
                        assert fault['failed'] and fault['optimizer_error']!=0,'allocation error lost or reported as success'
                        assert fault['metadata'],'partially optimized list has broken metadata'
                        print(f'[PASS {total:03}] {fault_title}: error preserved, partial list destructible')
                    except (AssertionError,RuntimeError,subprocess.TimeoutExpired,KeyError,ValueError) as exc:
                        failed+=1;failures.append(fault_title)
                        print(f'[FAIL {total:03}] {fault_title}\n  Error: {exc}\n  Inspect: {locations("optimizer")}')
            if data and kind=='ast' and not args.no_fault_injection:
                for allocation in range(1,data.get('tac_allocations',0)+1):
                    total+=1;fault_title=f'{title} TAC allocation #{allocation}'
                    try:
                        fault=invoke(exe,kind,name,seed,tac_fail=allocation)
                        assert fault.get('tac_failure_injected'), 'TAC allocation fault was not reached'
                        assert fault.get('generation_error',0)!=0,'TAC allocation error lost or reported as success'
                        assert fault.get('ast_consumed'),'AST not consumed on TAC failure'
                        print(f'[PASS {total:03}] {fault_title}: error preserved, AST consumed')
                    except (AssertionError,RuntimeError,subprocess.TimeoutExpired,KeyError,ValueError) as exc:
                        failed+=1;failures.append(fault_title)
                        print(f'[FAIL {total:03}] {fault_title}\n  Error: {exc}\n  Inspect: {locations(AST[name][2])}')
    print(f'\nRESULT: {total-failed}/{total} passed; {failed} failed.')
    if failures:print('Failing examples: '+', '.join(failures))
    print('Executor is test-only TAC semantics, not the official IFJcode26 interpreter.')
    if not total:print('No matching tests.');return 2
    return 1 if failed else 0


if __name__=='__main__':sys.exit(main())
