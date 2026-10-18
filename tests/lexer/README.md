# Lexer tests

From the repository root:

```sh
make -C src test-lexer
make -C src test-lexer-sanitize
make -C src test-lexer-all
```

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
check that comments and punctuation terminate keywords correctly. Extension-only
keywords are not imposed on the base language: §1.1 says those situations are
not tested without the corresponding extensions.

The exact source inputs are available in [`exmaples_to_test`](exmaples_to_test/README.md).
Its `programs/` folder contains four complete IFJ26 programs exercised by integration
lexer cases. Its `fragments/` folder contains the remaining valid and invalid lexer
inputs. The index describes each file and its expected lexical error code. Regenerate
missing snapshots with `python3 tests/lexer/run.py --export-examples`; editing a
snapshot does not change the embedded test fixture.

The project's clarified leading-zero rule applies to integer literals and exponent
digits: after an initial zero, only more zero digits are allowed (including with
valid digit separators). Thus `1e00`, `1e-00` and `1e0_0` are valid, while `1e01`,
`1e+01` and `1e0_1` must produce lexical error 1. Splitting a malformed number into
multiple otherwise valid tokens does not satisfy this requirement.

These tests target the current **raw scanner interface**, not a complete compiler.
Module/API conventions are identified explicitly in each case's reference;
the assignment does not prescribe C enum names, constructor internals, or the
representation of source positions. These checks must not be read as quotations
of the language specification.
They explicitly use paired parenthesis tokens for `()` and physical `TOKEN_EOL`
tokens. Line metadata expectations use 1-based token start lines, with the caller
installing `current_line`. The semicolon case expects `TOKEN_SEPARATOR`; the header
currently documents that token only for `|`, so its meaning must be coordinated
with the parser. Keyword attribute IDs cannot yet be checked because no keyword
enum is exposed in the headers.

Indent/dedent policy, blank/comment-only line suppression, parenthesized logical
lines, trailing-comma restrictions, and other syntax/semantic errors need a defined
layout/parser interface and separate integration tests. Unselected extensions,
unspecified annotation boundaries and numeric-suffix tokenization, range overflow
(excluded by the assignment), allocation failure injection, and every possible
unbounded input are not claimed as covered. Requirements and unresolved boundaries
were read from the sibling `assignment-context` directory; it is not needed at runtime.
