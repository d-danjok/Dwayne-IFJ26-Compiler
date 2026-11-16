# TAC, codegen output, and optimizer tests

## Program output: TAC + codegen

```sh
python3 tests/codegen/run_output_tests.py --sanitize
```

This is the program-logic suite: it builds LL-shaped, typed ASTs, calls the real
`tac_generate()` and `codegen_run()`, executes the generated **IFJcode26**, and
compares exact stdout with independent expected output. It does not just check
whether generation succeeds or an error is raised. Every passing and failing
program prints its example, input, expected output, actual output and exit code.
Failures also include source locations and the generated instruction or WRITE
trace. Production files remain unchanged.

Examples include arithmetic, invoice totals, summing 0..9, nested loops,
input-dependent branches with positive/negative/zero inputs, scope shadowing,
calls, factorial, early/implicit/Unit returns, Boolean side effects, string
escaping, conversions and nested built-ins. There are 30 output cases without the
optimizer. No allocation-failure tests are included in this output suite.

```sh
# Run only loop output examples:
python3 tests/codegen/run_output_tests.py --filter loop

# Add the real optimizer between TAC and codegen (31 output cases):
python3 tests/codegen/run_output_tests.py --optimize --sanitize

# Keep generated target programs for debugging:
python3 tests/codegen/run_output_tests.py --save-generated /tmp/ifj-generated

# Use an official interpreter installed separately (adapt its CLI as needed):
python3 tests/codegen/run_output_tests.py --interpreter '/path/to/interpreter {source}'
```

`{source}` is replaced with each generated target file. The command is executed
without a shell, and each example's input is passed on stdin. An interpreter
requiring a flag can use a template such as `'/path/to/interpreter --source={source}'`.

By default, `target_executor.py` executes the instructions these examples use.
It checks frames, initialization, calls, branches, arithmetic, string operations,
I/O and exit status, but is **not the official interpreter**. Unsupported
instructions are reported as executor limitations, never silently accepted.
Numeric overflow, every input format and the full instruction set are outside its
scope. Parser/semantic analysis are bypassed because these tests isolate the
backend. Thus this is real generated-program execution, but not a full
source-file-to-output compiler test. Main is first in the default fixtures;
`--optimize` also tests relocating a main marker from after another function.

## Advanced TAC + optimizer regression suite

Run from the repository root:

```sh
python3 tests/codegen/run_tests.py --sanitize
```

Focused runs:

```sh
python3 tests/codegen/run_tests.py --filter raw/ --random 0
python3 tests/codegen/run_tests.py --filter ast/factorial --sanitize
python3 tests/codegen/run_tests.py --filter ast/short --sanitize
python3 tests/codegen/run_tests.py --random 200 --no-fault-injection
```

Python 3.9+ and a C compiler are required. No Python packages are needed. Use
`--cc clang` or `--cc gcc` to select the compiler. Sanitizer mode requires compiler
support for AddressSanitizer and UndefinedBehaviorSanitizer. UBSan halts on the
first error inside each fixture process, while the runner continues with other
examples. Exit codes: **0** all selected checks passed; **1** failing checks;
**2** build failure or no matching examples.

## Paths and isolation

The runner locates the repository by searching its parent directories for
`src/tac.c` and `docs/LL.md`. The fixture includes headers through the compiler's
`-I <repository>/src` path. There are no references to the former `test/` folder,
no hardcoded user paths, and no dependency on the shell's current directory.
For example, invoking the runner by absolute path from `/tmp` works.

Build products live in a temporary directory and are removed automatically.
Production sources are **not edited**. Test-only allocator substitutions are
applied separately to the compiled TAC and optimizer objects; they do not change
public APIs or allocators in the AST/attribute-table modules.

## Layers of verification

1. **Raw TAC / optimizer:** hand-built programs exercise constant folding, copy
   propagation, overwritten aliases, cyclic copies, labels, calls, parameters,
   branch joins, loops, dead side effects, declaration preservation, borrowed
   strings, and constant conversion calls. They verify known expected results as
   well as before/after equivalence.
2. **AST -> TAC -> optimizer:** fixtures follow the current LL/parser layouts,
   including the `MAIN_FUNC_DEF -> FUNC_DEF` wrapper. Types and declaration scope
   IDs model semantic-analysis output. Real examples include an invoice formula,
   a loop sum, nested loops, input-dependent branching, scope shadowing, recursive
   factorial, a function declared before main, return semantics, Boolean side
   effects, numeric conversion and concatenation.
3. **Reference execution:** an independent test-only TAC evaluator compares result
   values, output, observable built-in calls (including reads), and traps before
   and after optimization. It uses an explicit argument stack and fixture arities
   for user functions. MAIN is resolved as an entry marker even before relocation.
4. **Structural invariants:** size, instruction IDs, tail/cursor pointers, cycle
   detection, label destinations, MAIN relocation, idempotence, required folding,
   retained trapping operations, and barriers are checked. These represent the
   current optimizer's documented contract, not assignment requirements about
   how aggressively code must be optimized.
5. **Allocation failures:** each optimizer allocation observed on a baseline run
   is failed in a separate process. Errors must survive and partially modified
   lists must remain destructible with valid metadata. Every observed TAC
   allocation for an AST fixture is also failed separately: generation must
   report failure and consume its AST. Sanitizers check the cleanup paths.
6. **Reproducible arithmetic properties:** by default 64 deterministic seeds
   exercise signed constant chains and compare against a separately computed
   Python result. Seed numbers are shown for reproduction. These are seeded
   examples, not a claim of exhaustive fuzzing.

## Assignment expectations

The source examples use compact braces for readability; AST blocks follow
`docs/LL.md`, not brace syntax. Expectations come from the local
`Specifikace_IFJ26_a_IFJcode26.pdf`:

- Section 2.3: explicit and implicit function returns, and discarding results of
  explicitly Unit-returning functions.
- Section 4: basic print/conversion behaviour used by fixtures.
- Section 5.1: nested calls and left-to-right argument evaluation.
- Section 5.2: Boolean values and short-circuit `&&` / `||`.

`sum` and `identity` in the nested-call fixture are **test reference callees**, not
IFJ26 built-ins. Their implementation is deliberately trivial to isolate argument
stack behaviour without introducing another AST function. The factorial fixture
exercises real emitted user functions and recursion.

Boundary tests around `LLONG_MIN/MAX`, NaN and infinity check conservative
optimizer behaviour. They do **not** establish the language's overflow semantics:
IFJ26 Int is described as 32-bit, the target int is 64-bit, and the assignment says
overflow is not tested. Signed zero is compared with its sign intact. Division by
zero must remain observable when reachable, even if the result is unused.

## Reading failures

Each failure names the layer and example/seed, gives the expected versus actual
result (or violated invariant), and points to relevant source locations. Line
numbers are resolved from source anchors on each run. If raw and optimized TAC
have the same wrong output, the report explicitly says so: this distinguishes an
existing TAC bug from an optimizer regression. Existing defects are genuine
failures, not marked as expected passes.

The current compiler emits warnings from `tac_optimizer.h` and enum conversions;
the runner displays these rather than modifying or suppressing compiler code.

## Scope limits

The `run_tests.py` suite isolates **TAC and its optimizer**. It does not invoke the
unfinished parser, semantic analyser, `main.c`, codegen, or the official IFJcode26
interpreter. Use `run_output_tests.py` above to execute codegen output.
The reference evaluator validates the emitted TAC, not final target-code frames
or instruction formatting. In particular, a passing nested-call test here does
not prove codegen's temporary-frame handling is correct. Full compiler and
codegen/interpreter testing is still required separately.

AST generation failures are reported directly; they are never passed into the
optimizer. Structural constraints and side-effect traces make equivalence checks
stronger, but this remains a finite regression suite, not proof of correctness.
