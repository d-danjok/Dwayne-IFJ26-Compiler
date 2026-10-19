# Lexer tests

From the repository root:

```sh
make -C src test-lexer
make -C src test-lexer-sanitize
make -C src test-lexer-all
```

To read the results in the terminal with colors, render the report with `pretty.py`:

```sh
make -C src test-lexer-pretty                    # run the tests, then render the report
make -C src test-lexer-pretty TEST_ARGS=--all-modes
python3 tests/lexer/pretty.py                    # render an existing test.output
python3 tests/lexer/pretty.py -q                 # build, failed names and totals only
python3 tests/lexer/pretty.py -v -k integration/ # full details for matching cases
python3 tests/lexer/pretty.py --run --case double/ | less -R
```

It shows the build result with compiler diagnostics, a pass bar per case group, and for
each failure the mismatches plus an aligned expected/actual token table (`~` changed,
`-` missing, `+` extra token; matching runs are collapsed unless `-v`). In an
`--all-modes` report, sanitizer failures identical to the ordinary run are listed by
name only. Colors are used on a terminal unless `NO_COLOR` is set; force them with
`--color always`. Exit status matches `run.py`: `0` all pass, `1` failures, `2` build
or report error.

To include only failed cases in `test.output`:

```sh
make -C src test-lexer-all TEST_ARGS=--fails-only
# Or directly, for the ordinary run:
python3 tests/lexer/run.py --fails-only
```

`--fails-only` changes report detail only: all selected tests still run, totals
still include passing cases, and failures retain their full diagnostics. It works
with `--case`, `--sanitize`, and `--all-modes`. Build diagnostics and exit status
are unchanged. Without this option, the report includes both passes and failures.
`TEST_ARGS` can pass runner options to any of the three Make test targets.

Each command replaces `test.output` in the repository root. `test-lexer-all` combines
the ordinary and sanitizer runs in one report with both summaries at the top.
The report contains
every case's input bytes, requirement and source reference. Failures include token
positions within the sequence, expected and actual values, error codes, and the
probe trace or sanitizer diagnostic. A list of failing case names follows the totals.
`Required`/`expected` means the behavior demanded by the test; `lexer returned`/
`actual` means what the unmodified lexer produced. For example, a required
`TOKEN_KEYWORD` but returned `TOKEN_IDENTIFIER value='def'` is a lexer failure:
the assignment explicitly reserves `def` as a keyword.
Exit status is `0` for success, `1` for failed cases, and `2` for a build/setup error
(GNU Make itself returns `2` when a recipe fails).

Requires Linux/macOS, Python 3.10+ and a C compiler supporting GNU C11. Sanitizer mode also
requires AddressSanitizer and UndefinedBehaviorSanitizer. No third-party Python
packages or test libraries are needed. `CC` chooses the compiler. The temporary
binary and input files are removed after the run.

Run a subset or keep a separate report:

```sh
python3 tests/lexer/run.py --case double/
python3 tests/lexer/run.py --case stdin/ --output /tmp/lexer-pipe.output
python3 tests/lexer/run.py --sanitize --timeout 10
```

`probe.c` includes the real `lexer.h` and links the unmodified lexer, attribute
table, utilities and error module. Normal fixtures use regular files; dedicated
stdin cases use actual non-seekable subprocess pipes. Every case runs in a fresh
process with a timeout and token limit, so a crash or loop cannot stop subsequent
cases. Each probe has an explicit 8 MiB stack budget (or a lower OS hard limit),
so Make's adjustment of inherited stack limits cannot hide recursive-scanner failures.
Cases exercising two interleaved lexers and retained attributes separately
check state isolation and ownership.

Expected token sequences, literal values and lexical error codes are strict.
Double values have no tolerance. String bytes are transported in hex so control
characters cannot corrupt the test protocol. Successful cases read EOF twice and
verify the physical line counter. Known failures are not marked XFAIL or skipped.
Sanitizer mode fails on any sanitizer output; leak detection is disabled on macOS,
where LeakSanitizer is unavailable. Sanitizers change stack usage, so a recursion
limit observed there need not match an ordinary build.

The cases cover base keywords, identifiers, operators, punctuation, annotations,
integer and double forms, malformed numeric separators and exponents, all printable
ASCII escape candidates, ordinary and multiline strings, nested/truncated comments,
lookahead at EOF and token boundaries, buffer growth, attribute table growth,
constructor contracts, line metadata, and piped input.

The base keyword list is taken from specification §1.1, page 2:
`def do Double else if import Int return String then Unit val var while`.
Every keyword is checked on its own and in a sequence. Changed case, a digit
suffix, or an underscore prefix must produce identifiers instead. Tests also
check that comments and punctuation terminate keywords correctly. The extension
group (`Boolean case class extension for List match type`) is also expected as
`TOKEN_KEYWORD`: §1.1 says it is advisable not to accept them as identifiers and
`token.h` defines a `KW_` value for each. `to`, `until`, `by` and `s` must stay
identifiers (forum answers of 2026-09-29 and 2026-10-11).

The exact source inputs are available in [`exmaples_to_test`](exmaples_to_test/README.md).
Its `programs/` folder contains four complete IFJ26 programs exercised by integration
lexer cases. Its `fragments/` folder contains the remaining valid and invalid lexer
inputs. The index describes each file and its expected lexical error code. Regenerate
missing snapshots with `python3 tests/lexer/run.py --export-examples`; editing a
snapshot does not change the embedded test fixture.

Number rules follow §1.1 and the forum answer of 2026-10-11: a nonzero Int must
not start with zero (`01`, `0_1` are lexical error 1; `00`, `0_0` are zero), but the
exponent and the integer part of a Double may (`1e01`, `2e-001`, `01.5` are valid).
Several consecutive separators between two digits are valid (`1__2`, `1.2__3`); a
separator not between two digits is lexical error 1 (`1_`, `1_.5`, `1e_2`, `1.5_D`).
Ambiguous points were cross-checked with Scala 3; see `SPEC_AUDIT.md`.
Team decisions of 2026-10-19 (stricter than Scala, which splits these into tokens):
a number glued to a letter or to a second fraction (`12abc`, `1.5x`, `2e0.01`),
unsupported forms (`0x1F`, `0b101`, `1L`, `1.5f`) and digits with only a suffix
(`1d`, `1D`) are lexical error 1.

These tests target the current **raw scanner interface**, not a complete compiler.
Module/API conventions are identified explicitly in each case's reference;
the assignment does not prescribe C enum names, constructor internals, or the
representation of source positions. These checks must not be read as quotations
of the language specification.
They explicitly use paired parenthesis tokens for `()` and logical-line `TOKEN_EOL`
tokens: `logical_lines()` in `run.py` keeps an EOL only after a line that produced a token
(blank and comment-only lines emit none), drops EOLs inside parentheses, and expects a
synthesized EOL before EOF when the last line has no final LF. The integration programs
also expect INDENT/DEDENT. Line metadata expectations use 1-based token start lines, with the caller
installing `current_line`. The semicolon case expects `TOKEN_SEPARATOR`; the header
currently documents that token only for `|`, so its meaning must be coordinated
with the parser. Keyword attribute IDs cannot yet be checked because no keyword
enum is exposed in the headers.

The `layout/` group checks INDENT/DEDENT levels, blank and comment-only line
suppression and LF inside parentheses (S §1.2). A dedent to a width never opened
is lexical error 1 (team decision). Not covered yet: continuation-line indentation
inside parentheses, tabs at the start of comment-only lines, and an indented first
line. Trailing-comma restrictions and other syntax/semantic errors
belong to parser tests. Unselected extensions,
unspecified annotation boundaries and numeric-suffix tokenization, range overflow
(excluded by the assignment), allocation failure injection, and every possible
unbounded input are not claimed as covered. Requirements and unresolved boundaries
were read from the sibling `assignment-context` directory; it is not needed at runtime.
