"""Small IFJcode26 executor for output regression tests, not the official interpreter."""
import math
import re

UNSET = object()


class TargetError(Exception):
    def __init__(self, message, code=53):
        super().__init__(message)
        self.code = code


class UnsupportedInstruction(Exception):
    pass


class Executor:
    def __init__(self, source, stdin=''):
        self.instructions, self.labels = [], {}
        self.gf, self.tf, self.frames = {}, None, []
        self.calls, self.values = [], []
        self.output, self.pc, self.writes = '', 0, []
        self.input = stdin.splitlines(keepends=True)
        header = False
        for line_number, raw in enumerate(source.splitlines(), 1):
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue
            if not header:
                if line != '.IFJcode26':
                    raise TargetError('missing .IFJcode26 header', 21)
                header = True
                continue
            tokens = line.split()
            if tokens[0] == 'LABEL':
                if len(tokens) != 2 or tokens[1] in self.labels:
                    raise TargetError(f'invalid/duplicate label on line {line_number}', 52)
                self.labels[tokens[1]] = len(self.instructions)
            self.instructions.append((line_number, tokens, line))
        if not header:
            raise TargetError('no generated code', 21)

    def frame(self, name):
        prefix, key = name.split('@', 1)
        frame = self.gf if prefix == 'GF' else self.tf if prefix == 'TF' else self.frames[-1] if prefix == 'LF' and self.frames else None
        if frame is None:
            raise TargetError(f'missing frame: {name}', 55)
        return frame, key

    def get(self, symbol):
        kind, data = symbol.split('@', 1)
        if kind in ('GF', 'LF', 'TF'):
            frame, key = self.frame(symbol)
            if key not in frame:
                raise TargetError(f'undeclared variable: {symbol}', 54)
            if frame[key] is UNSET:
                raise TargetError(f'uninitialized variable: {symbol}', 56)
            return frame[key]
        if kind == 'int': return kind, int(data)
        if kind == 'float': return kind, float.fromhex(data)
        if kind == 'nil' and data == 'nil': return kind, None
        if kind == 'bool' and data in ('true', 'false'): return kind, data == 'true'
        if kind == 'string': return kind, re.sub(r'\\([0-9]{3})', lambda m: chr(int(m[1])), data)
        raise TargetError(f'invalid symbol: {symbol}', 23)

    def put(self, name, value):
        frame, key = self.frame(name)
        if key not in frame:
            raise TargetError(f'write to undeclared variable: {name}', 54)
        frame[key] = value

    def typed(self, symbol, kind):
        actual, value = self.get(symbol)
        if actual != kind:
            raise TargetError(f'{symbol}: expected {kind}, got {actual}')
        return value

    def equal(self, a, b):
        left, right = self.get(a), self.get(b)
        if left[0] != right[0] and 'nil' not in (left[0], right[0]):
            raise TargetError('comparison of incompatible types')
        return left[0] == right[0] and left[1] == right[1]

    def jump(self, label):
        if label not in self.labels:
            raise TargetError(f'undefined label: {label}', 52)
        self.pc = self.labels[label]

    def run(self, limit=100000):
        for _ in range(limit):
            code = self.step()
            if code is not None: return code
        raise TargetError('execution step limit exceeded (possible infinite loop)', 99)

    def step(self):
        """Execute one instruction; the exit code once the program ends, otherwise None."""
        if self.pc >= len(self.instructions): return 0
        _, words, _ = self.instructions[self.pc]
        op, *a = words
        next_pc = self.pc + 1
        if op == 'LABEL': pass
        elif op == 'CREATEFRAME': self.tf = {}
        elif op == 'PUSHFRAME':
            if self.tf is None: raise TargetError('PUSHFRAME without TF', 55)
            self.frames.append(self.tf); self.tf = None
        elif op == 'POPFRAME':
            if not self.frames: raise TargetError('POPFRAME without LF', 55)
            self.tf = self.frames.pop()
        elif op == 'DEFVAR':
            frame, key = self.frame(a[0])
            if key in frame: raise TargetError(f'redeclared variable: {a[0]}', 52)
            frame[key] = UNSET
        elif op == 'MOVE': self.put(a[0], self.get(a[1]))
        elif op == 'CALL':
            self.calls.append(next_pc); self.jump(a[0]); return None
        elif op == 'RETURN':
            if not self.calls: raise TargetError('RETURN without CALL', 56)
            self.pc = self.calls.pop(); return None
        elif op == 'JUMP': self.jump(a[0]); return None
        elif op in ('JUMPIFEQ', 'JUMPIFNEQ'):
            if a[0] not in self.labels: raise TargetError(f'undefined label: {a[0]}', 52)
            if self.equal(a[1], a[2]) == (op == 'JUMPIFEQ'):
                self.jump(a[0]); return None
        elif op == 'EXIT': return self.typed(a[0], 'int')
        elif op == 'WRITE':
            kind, value = self.get(a[0])
            rendered = 'nil' if kind == 'nil' else str(value).lower() if kind == 'bool' else value.hex() if kind == 'float' else str(value)
            self.output += rendered
            self.writes.append((self.instructions[self.pc][0], rendered))
        elif op == 'READ':
            raw = self.input.pop(0) if self.input else None
            result = ('nil', None)
            if raw is not None:
                line = raw.removesuffix('\n')
                if a[1] == 'string': result = ('string', raw)
                elif a[1] == 'int' and re.fullmatch(r'[+-]?[0-9]+', line):
                    value = int(line)
                    if -(2**63) <= value < 2**63: result = ('int', value)
                elif a[1] == 'float' and re.fullmatch(r'[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?', line):
                    value = float(line)
                    if math.isfinite(value): result = ('float', value)
                elif a[1] == 'bool' and line in ('true', 'false'): result = ('bool', line == 'true')
            self.put(a[0], result)
        elif op == 'TYPE':
            try: kind = self.get(a[1])[0]
            except TargetError as exc:
                if exc.code != 56: raise
                kind = ''
            self.put(a[0], ('string', kind))
        elif op in ('ADD', 'SUB', 'MUL', 'DIV', 'IDIV'):
            kind, left = self.get(a[1]); other, right = self.get(a[2])
            if kind != other or kind not in ('int', 'float'): raise TargetError(f'{op}: incompatible numeric types')
            if op == 'DIV' and kind != 'float' or op == 'IDIV' and kind != 'int': raise TargetError(f'{op}: wrong numeric type')
            if op in ('DIV', 'IDIV') and right == 0: raise TargetError('division by zero', 57)
            if op == 'ADD': value = left + right
            elif op == 'SUB': value = left - right
            elif op == 'MUL': value = left * right
            elif op == 'DIV': value = left / right
            else: value = (abs(left) // abs(right)) * (-1 if (left < 0) != (right < 0) else 1)
            self.put(a[0], (kind, value))
        elif op in ('EQ', 'LT', 'GT'):
            if op == 'EQ': value = self.equal(a[1], a[2])
            else:
                kind, left = self.get(a[1]); other, right = self.get(a[2])
                if kind != other or kind == 'nil': raise TargetError('invalid ordered comparison')
                value = left < right if op == 'LT' else left > right
            self.put(a[0], ('bool', value))
        elif op in ('AND', 'OR', 'NOT'):
            left = self.typed(a[1], 'bool')
            if op == 'NOT': value = not left
            else:
                right = self.typed(a[2], 'bool'); value = left and right if op == 'AND' else left or right
            self.put(a[0], ('bool', value))
        elif op in ('INT2FLOAT', 'FLOAT2INT', 'INT2STR', 'FLOAT2STR', 'INT2CHAR'):
            value = self.typed(a[1], 'float' if op in ('FLOAT2INT', 'FLOAT2STR') else 'int')
            if op == 'INT2FLOAT': result = ('float', float(value))
            elif op == 'FLOAT2INT': result = ('int', math.trunc(value))
            elif op == 'INT2STR': result = ('string', str(value))
            elif op == 'FLOAT2STR': result = ('string', f'{value:.2f}')
            else:
                if not 0 <= value <= 127: raise TargetError('invalid character code', 58)
                result = ('string', chr(value))
            self.put(a[0], result)
        elif op == 'CONCAT': self.put(a[0], ('string', self.typed(a[1], 'string') + self.typed(a[2], 'string')))
        elif op == 'STRLEN': self.put(a[0], ('int', len(self.typed(a[1], 'string'))))
        elif op in ('STRI2INT', 'GETCHAR'):
            value = self.typed(a[1], 'string'); index = self.typed(a[2], 'int')
            if not 0 <= index < len(value): raise TargetError('string index out of bounds', 58)
            self.put(a[0], ('int', ord(value[index])) if op == 'STRI2INT' else ('string', value[index]))
        elif op == 'PUSHS': self.values.append(self.get(a[0]))
        elif op == 'POPS':
            if not self.values: raise TargetError('empty value stack', 56)
            self.put(a[0], self.values.pop())
        elif op == 'CLEARS': self.values.clear()
        else: raise UnsupportedInstruction(op)
        self.pc = next_pc
        return None


def self_check():
    prefix = '.IFJcode26\nCREATEFRAME\nPUSHFRAME\nDEFVAR LF@x\n'
    vm = Executor(prefix + 'IDIV LF@x int@-7 int@2\nWRITE LF@x\nEXIT int@0')
    assert vm.run() == 0 and vm.output == '-3'
    vm = Executor(prefix + 'MOVE LF@x string@a\\032b\\035c\\092d\nWRITE LF@x')
    assert vm.run() == 0 and vm.output == 'a b#c\\d'
    for instruction, code in [('WRITE LF@x', 56), ('MOVE LF@y int@1', 54), ('DEFVAR LF@x', 52), ('FLOAT2INT LF@x int@1', 53)]:
        try: Executor(prefix + instruction).run()
        except TargetError as exc: assert exc.code == code
        else: raise AssertionError(f'executor accepted invalid operation: {instruction}')
    vm = Executor('.IFJcode26\nCREATEFRAME\nCALL f\nWRITE TF@r\nEXIT int@0\nLABEL f\nPUSHFRAME\nDEFVAR LF@r\nMOVE LF@r int@42\nPOPFRAME\nRETURN')
    assert vm.run() == 0 and vm.output == '42'
