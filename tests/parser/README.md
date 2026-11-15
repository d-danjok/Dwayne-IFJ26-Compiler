# Lexer + parser tests

These tests run the real lexer and the real parser together on small IFJ26 programs. Each test checks
the compiler error code. A test can also check the shape of the AST.

From the repository root:

```sh
python3 tests/parser/run.py                  # run all cases
python3 tests/parser/run.py -k expression    # only cases with "expression" in group/name
python3 tests/parser/run.py -v               # also print the AST of every case that parsed
python3 tests/parser/run.py --sanitize       # build with AddressSanitizer + UBSan (finds leaks)
python3 tests/parser/run.py --ast prog.ifj   # print the error code and the AST of any file
```

Exit status: `0` all cases pass, `1` at least one case fails, `2` the build failed.
Requires Python 3.8+ and a C compiler (`CC` selects it, default `cc`).

## Cases

A case is one `.ifj` file. Its directory gives the expected error code:

| Directory              | Expected code | Meaning                         |
|------------------------|---------------|---------------------------------|
| `cases/valid/`         | 0             | the program must parse          |
| `cases/lexical_error/` | 1             | the lexer must reject it        |
| `cases/syntax_error/`  | 2             | the parser must reject it       |

To add a case, put a new `.ifj` file into the correct directory. Nothing else is needed.

Most cases use a plain `def f(): Unit =` function and no `@main`. A missing `@main` is a semantic
error (3), so the parser must accept such a program. This keeps each case about one feature.
Cases with the prefix `ext_` need the BOOLTHEN or FUNEXP extension.

A failed case prints the expected and actual code, the log that the parser wrote to stderr, and the
token trace (see below).
Code `99` means an internal error. The log then contains the raw internal code, for example
`internal error code: 207` (`INTERNAL_STACK_IS_EMPTY`).

Limit: an error case passes when the compiler returns the correct code for any reason. It does not
prove that the parser found the intended error.

## AST snapshots

A valid case can have a snapshot `NAME.ast` next to `NAME.ifj`. When the snapshot exists, the printed
AST must be identical to it. Cases with a checked snapshot show `[ast]` in the output.

```sh
python3 tests/parser/run.py --update -k valid/while_loop   # write or replace the snapshot
```

`--update` writes what the parser produces now, correct or not. Read every new snapshot and compare
it with `docs/LL.md` before you commit it. A case that does not parse gets no snapshot.

## AST printer

`ast_print.h` declares one function. Any test or debugging code can use it:

```c
#include "ast_print.h"
ast_print(stderr, tree, attr_tab);
```

Compile with `-Itests/parser` and add `tests/parser/ast_print.c` to the sources. Example output for
`val a = 1 + 2 * 3` (expression part):

```
BINARY_OPERATION + @3
├─L LITERAL Int 1 @3
└─R BINARY_OPERATION * @3
   ├─L LITERAL Int 2 @3
   └─R LITERAL Int 3 @3
```

One line is one node: node type, operator, type and value, `block=N`, `@source line`. A field that
still has its "unset" value is not printed. `L` is `left_child`, `R` is `right_child`.

## Token trace

When the result is an error, the probe also prints the tokens that the parser read from the lexer, one
row per logical line. The number at the start of a row is the source line of its first token:

```
tokens read before the error (7, the last one is where reading stopped):
   1 | KEYWORD(import) IDENTIFIER(ifj26) DOT OPERATOR(*) EOL
   3 | ANNOTATION KEYWORD(def)
```

The last token is the last one that the parser asked for. The parser reads one token ahead, so the
token that it rejected is the last one or the one before it. The trace is part of the log, so
`run.py` shows it for every failed case and `--ast` shows it for every error.

To record the tokens, `run.py` compiles `parser.c` with
`-Dlexer_get_next_token=parser_test_next_token`. The wrapper in `probe.c` calls the real lexer and
stores each token. `parser.c` itself is not changed.

## Temporary shim

`probe.c` has a weak `log_printf`. `utils.h` declares this function, but the merge removed its
definition from `utils.c`. The linker ignores the weak copy when the real function is back.
