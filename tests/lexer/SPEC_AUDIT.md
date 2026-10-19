# Lexer test suite vs. zadání, specifikace and forum answers

Audit date: 2026-10-19. Subject: `tests/lexer/run.py` (462 cases) at the state where 424 pass.

Sources used:

- **S** = `Specifikace_IFJ26_a_IFJcode26.pdf`: §1.1 lexical units, §1.2 layout, §2 prolog and `@main`,
  §3 operators, §5 extensions, §6 error codes.
- **Z** = `Zadání_projektu_IFJ_a_IAL_2026_2027.pdf`: §2 (input from stdin, exit codes), §2.2 (FSM).
- **F** = forum answers by Ing. Ondryáš (dates below).

"Lexer today" columns come from the last ordinary run of the current `src/lexer.c`.

## Summary

| Verdict | Cases | What to do |
|---|---|---|
| Correct and spec-backed | ~390 | keep |
| **Contradicts an official forum answer** | 11 | flip the expectation (section 1) |
| Questionable reading of the spec text | 1 (+1 error-code choice) | decide or ask on the forum (section 2) |
| Pins a team convention the spec leaves open | 3 | agree in the team, then keep or change (section 3) |
| Becomes wrong once a planned extension is implemented | 3 | make it conditional (section 4) |
| Implementation contract, not spec (`api/`, `buffer/`, `attributes/`, `line/`) | 51 | fine, but don't read them as spec |

The base lexemes are covered well: keywords, identifiers, all base operators, escapes,
unterminated strings and comments, nested comments, token boundaries, buffer growth, and stdin.
The serious problems are the number rules in section 1 and the almost untested indentation
layer (section 5.1).

## 1. Wrong: the expectation contradicts an official answer

**F, 11. 10. 2026 (number literals):**

1. "ano, v souladu se Scalou může být použito uvnitř číselných literálů i více podtržítek za sebou"
2. "2a) 2e001 i 2e-001 jsou platné desetinné literály"

The suite encodes the opposite as a "Project clarification: after a leading zero, only another
zero digit is allowed" (`run.py`, `double/invalid_*` reference; README paragraph about the
leading-zero rule). S restricts leading zeros only for the Int literal ("Nenulový literál nesmí
začínat nulou"). For the exponent it only requires "alespoň jedna číslice" with separators "stejně
jako u celočíselného literálu".

| Case | Input | Suite expects | Correct per F | Lexer today |
|---|---|---|---|---|
| `integer/invalid_1__2` | `1__2` | error 1 | INT 12 | rejects (suite PASS, hides a lexer bug) |
| `integer/invalid_0__0` | `0__0` | error 1 | INT 0 (the suite accepts `0_0` itself) | rejects (hidden bug) |
| `double/invalid_1.2__3` | `1.2__3` | error 1 | DOUBLE 1.23 | rejects (hidden bug) |
| `double/invalid_1e2__3` | `1e2__3` | error 1 | DOUBLE 1e23 | rejects (hidden bug) |
| `double/invalid_1e0__0` | `1e0__0` | error 1 | DOUBLE 1.0 | rejects (hidden bug) |
| `double/invalid_1e0_1` | `1e0_1` | error 1 | DOUBLE 10.0 | rejects (hidden bug) |
| `double/invalid_1.5E00_2D` | `1.5E00_2D` | error 1 | DOUBLE 150.0 | rejects (hidden bug) |
| `double/invalid_1e01` | `1e01` | error 1 | DOUBLE 10.0 | accepts (suite FAIL, but the lexer is right) |
| `double/invalid_1e+01` | `1e+01` | error 1 | DOUBLE 10.0 | accepts (lexer right) |
| `double/invalid_1e-01` | `1e-01` | error 1 | DOUBLE 0.1 | accepts (lexer right) |
| `double/invalid_1e09` | `1e09` | error 1 | DOUBLE 1e9 | accepts (lexer right) |

Fix: move all 11 into the valid lists with the values above. Delete the "project clarification"
reference and the README paragraph. Keep the cases that really are invalid: trailing `_`, and `_`
next to `.`, `e` or a sign (`1_`, `1e2_`, `1e_2`, `1e+_2`, `1_e2`, `1.2_`, `.5_`). The real
leading-zero errors also stay (`01`, `0123`, `0_1`, `00_1`).

After the fix, the lexer fails 7 more cases (it rejects `__`, and it rejects exponents with
leading zeros when they contain `_`) and passes 4 more.

## 2. Questionable reading of the spec text

| Case | Input | Suite expects | Issue |
|---|---|---|---|
| `comment/invalid_comment_after_multiline` | `/*a\nb*///text` | error 1 | S §1.1: after a block comment containing LF "nesmí být další tokeny (očekává se zde konec řádku, kterému mohou předcházet jen bílé znaky)". A `//` comment is not a token, and S says "komentář [je] posuzován jako jeden konec řádku". So the line comment *is* the expected line end, and the input is most likely **valid** (no tokens). Recommend flipping it, or asking on the forum. |
| `comment/invalid_code_after_multiline` | `/*a\nb*/x` | error 1 | The input is clearly invalid. S doesn't say which error class: §6 lists code 1 for "chybná struktura … komentáře" and code 2 for "chybná syntaxe". Error 1 is defensible; record it as a team decision. |

## 3. Team conventions pinned by tests (allowed by S/F, but must be agreed)

| Case | Pins | Alternative allowed |
|---|---|---|
| `punctuation/unit` `()` | `OPEN_PAREN CLOSE_PAREN` | F 13. 10.: Unit may be a literal at lexer level *or* handled in the parser. `token.h` already defines `TOKEN_UNIT`. If the lexer emits UNIT, `(())` must give `OPEN UNIT CLOSE` and `(((())))` must still work. Whitespace between the parentheses "testovat nebudeme". |
| `punctuation/semicolon` `1;2` | `;` as `TOKEN_SEPARATOR` | S §1.2 requires `;` as a statement separator, so `;` must be tokenized; the lexer fails this today. But `token.h` documents `TOKEN_SEPARATOR` as `\|` (the ALGEBDT union). If ALGEBDT is chosen, `;` and `\|` need different tokens. |
| integration programs, EOL/INDENT/DEDENT | the logical-line EOL policy in `logical_lines()` | Matches S §1.2: blank and comment-only lines are ignored, and LF inside parentheses is whitespace. The token representation itself is the team's choice. |

`number/dot_lookahead` (`1.` → `INT DOT`) is labelled as a lexer.c convention, but S backs it:
the Double form `[celá část] . {desetinná část} [exponent]` makes the fractional part mandatory.

## 4. Correct for the base language only (depends on the chosen extensions)

`token.h` already has `KW_BOOLEAN`, `KW_CASE`, `KW_MATCH`, …, `TOKEN_BOOLEAN` and `TOKEN_SEPARATOR (|)`,
so the team apparently plans BOOLTHEN (required by CONTROL) and ALGEBDT.

| Case | Input | Becomes valid with |
|---|---|---|
| `invalid/ascii_33` | `!` | BOOLTHEN (unary negation) |
| `invalid/ascii_91`, `invalid/ascii_93` | `[`, `]` | LISTS (`List[Int]`) |

The other `invalid/*` cases stay correct under every extension. A lone `&` is still invalid
(BOOLTHEN only adds `&&`), and `$ { }` are only meaningful inside `s"…"`.

## 5. Completely missed

Priority: **H** = base language, will be graded; **M** = base, edge cases; **E** = only if the
extension is chosen.

### 5.1 Layout and indentation, S §1.2 (H): almost no tests

INDENT/DEDENT appear only in the 4 integration programs, and that is the only reason the
"INDENT on every same-depth line" lexer bug showed up. Missing:

- **INDENT/DEDENT levels:**
  - same-depth lines emit nothing;
  - a deeper line emits exactly one INDENT;
  - a multi-level dedent emits several DEDENTs;
  - DEDENTs come before EOF.
- **Dedent to a width that isn't on the stack** (indent 4 → 2 when the stack is 0, 4): must be an
  error. The code is undecided: §6 has 1 for "chybná struktura … odsazení" and 2 for "porušení
  struktury odsazených bloků". Decide, or ask on the forum.
- **Blank and comment-only lines** ("nemění aktuální úroveň odsazení"):
  - `x\n    \ny` (whitespace-only line in between);
  - `x\n      // c\ny` (comment-only line at a deeper indentation);
  - an indented multi-line `/* */` between statements.
- **LF inside parentheses:** `f(a,\n    b)` gives no EOL and no INDENT.
- **Continuation inside parentheses** must be indented more than the logical line ("musí být
  následující řádek odsazen více"): `f(a,\nb)` should be an error, class undecided.
- **Logical line containing a multi-line string** (`val x = """a\n  b"""\ny`): its indentation is
  measured on its first physical line, and the content lines don't produce INDENT.
- **Tabs:**
  - a tab on a blank or whitespace-only line (`\t\n`) is allowed, because the rule is only for
    "neprázdný řádek";
  - a tab at the start of a comment-only line or of a continuation line inside parentheses is
    ambiguous. Decide.
- **Indented first line before the prolog** (`  import ifj26.*`): S §2 says "Před prologem mohou
  být pouze bílé znaky a komentáře", so this must not be an error or a stray INDENT.
- **`else` on its own logical line** (S §2.4): the lexer must emit DEDENT before `else`
  (`if … then\n  x\nelse\n  y`).

### 5.2 Number literals, S §1.1 (H/M)

- **Valid multiple separators** (section 1), plus `1___2` and `1_2__3.4__5e6__7`.
- **Unsupported forms:** `0x1F` ("Jiné číselné soustavy"), `1L` ("sufix L"), `1.5f`, `1.5F`, `1f`
  ("Sufixy f a F podporovány nejsou"). The lexer must not silently produce `INT 0` + `IDENTIFIER x1F`.
  Error 1 is the natural choice.
- **`2e0.01`:** F 11. 10. says "není platným literálem". There is no test that it doesn't become one
  DOUBLE. Scala splits it into `2e0` and `.01` (section 6), so `DOUBLE DOUBLE` is the
  Scala-compatible lexer output, and the parser then reports error 2.
- **`1d` / `1D`** (digits plus suffix, no `.` and no exponent): neither of the S Double forms
  matches, but Scala accepts it as `1.0`. S says the suffix may end the literal "ve všech
  případech". Recommend DOUBLE 1.0, and ask on the forum if the team wants certainty.
- **Separators next to `.` or a suffix:** `1_.5`, `1._5`, `1.5_D`. Also `1.e5`, which should give
  `INT DOT IDENTIFIER(e5)`.
- **Number directly followed by letters:** `12abc`, `1.5x`, `1e5e`, `1Dx`. The Scala scanner
  *splits* `12abc` into `12` + `abc`, and only the parser fails ("end of statement expected").
  Choose between the Scala-compatible split (the program then fails with error 2) and error 1.
  The split is simpler and matches Scala; either way, pin it with a test.
- **Integer part of a Double with a leading zero:** `01.5`, `00.5`. S restricts only the Int
  literal, and Scala accepts both (1.5, 0.5), so they should be valid DOUBLEs.
- **Negatives** (F 22. 9.: `-0` is valid): `-0` and `-2147483648` give `OP_MINUS INT`.
  `operator/unary_minus` covers only `-12` and `-.5`.
- **Unit in expressions** (F 13. 10.): `(((())))` and `()+42`.

### 5.3 Strings (M)

- A literal (physical) tab or another control character inside a single-line string. S forbids
  only the real end of line.
- Multi-line string closed by more than three quotes (`"""a""""`). In Scala the last three
  quotes close the string, so the value is `a"`. See section 6.
- A multi-line string in the middle of a logical line followed by more tokens:
  `"""a\nb""" + y`. Line counting is covered, the token sequence after it is not.
- CR / CRLF input: S defines line ends only by LF, and `\r` isn't mentioned as whitespace. The
  graders' tests are ASCII; decide what `\r` is.

### 5.4 Comments (M)

- `*/` outside a comment gives `OP_MULTIPLY OP_DIVISION`.
- A one-line block comment after a multi-line one on the same line (`/*a\nb*/ /*c*/`): valid,
  because a block comment without LF counts as whitespace.
- `import/* komentar */ifj26.*` (forum example). `def/**/name` and `abc/**/z` cover the mechanism;
  the exact prolog form isn't tested.

### 5.5 Keywords and identifiers (M)

- **Extension keywords** `Boolean case class extension for List match type`: S says "vhodné je na
  místě identifikátoru nepřijímat", and `token.h` has `KW_*` for all of them. They're untested.
- **Must stay identifiers:**
  - `to`, `until`, `by` (F 29. 9.: in Scala they are methods, not keywords);
  - `s` (INTERPOLATEDS uses only `s` glued to a quote);
  - `true`/`false` when BOOLTHEN is not chosen.
- `___`: F 29. 9. says `__` and longer are identifiers. `__` is covered, longer isn't (trivial).

### 5.6 Characters and annotations (M)

- **Unknown characters not tested:**
  - `%` (37), `^` (94);
  - `|` (124) in the base language;
  - control bytes 0x01–0x08, 0x0B, 0x0C, 0x7F.
- **`@mainx`:** must not become `ANNOTATION` + `IDENTIFIER x`. Also `@main(` and `@main def` on one
  line (S §2 allows both placements).

### 5.7 Extensions (E): no tests at all

- **BOOLTHEN:** `!`, `&&`, `||`, `true`, `false`. Also `!` versus `!=` (maximal munch).
- **CONTROL:** `<-`. Note `x<-1` (Scala reads it as `<-`).
- **LAMBDAS / ALGEBDT:** `=>` versus `=`, `==`, `>=`; `|`; `_` as a pattern.
- **LISTS:** `[`, `]`, `++` versus `+`.
- **INTERPOLATEDS** (F 11. 10.: "znak 's' musí být bezprostředně před otvírací uvozovkou"):
  - `s"…"` and `s"""…"""`;
  - `$id`, `${expr}`, `$$`, `\"`;
  - and `s "x"` (with a space) must lex as `IDENTIFIER s` + `STRING`.

### 5.8 Whole program (Z §2): not testable yet

- **Exit status 1 for any lexical error, and the message on stderr.** There is no `main.c` yet. The
  suite checks the `error` value, not the process exit code.
- **Reading from stdin:** covered by the `stdin/pipe_*` cases. These are the correct
  Z-requirement tests, and the lexer fails them today (`ftell`/`fseek` on a pipe).

## 6. Scala 3 cross-check for points S leaves open

S §1: "Není-li výslovně uvedeno jinak, je každá konstrukce IFJ26 současně platnou konstrukcí jazyka
Scala 3.8.4 … a má obdobnou sémantiku." Scala's own scanner is therefore the tie-breaker wherever
S is silent. It is not authoritative where S overrides Scala, e.g. the `f`/`L` suffixes and other
number bases.

Probe: one `object P: val v = <lexeme>` per input, compiled with `scalac` from Scala **3.3.6**
(nixpkgs `scala_3`; S targets 3.8.4, and the literal scanner rules in question are the same
across 3.x). For accepted inputs, the value printed by the compiled program is shown.

| Input | Scala 3 | Bearing on the suite |
|---|---|---|
| `00`, `0_0`, `0__0` | accepted, `0` | Confirms the suite's `00` / `0_0` valid, and that `0__0` is valid (section 1) |
| `1__2` | accepted, `12` | Confirms section 1 |
| `1e01`, `2e001`, `2e-001`, `1e0_1`, `1e2__3`, `1.2__3` | accepted: `10.0`, `20.0`, `0.2`, `10.0`, `1.0E23`, `1.23` | Confirms section 1 and the forum answer |
| `01` | accepted, `1` | **S overrides Scala here** ("Nenulový literál nesmí začínat nulou"); keep `integer/invalid_01` |
| `01.5`, `00.5` | accepted, `1.5`, `0.5` | Valid Double; add tests (5.2) |
| `1d`, `1D` | accepted, `1.0` | Supports DOUBLE 1.0 (5.2) |
| `12abc` | scanner splits it; parser error "end of statement expected but identifier found" | Split is the Scala-compatible lexer behaviour (5.2) |
| `2e0.01` | scanner splits it into `2e0` + `.01`; parser error "end of statement expected but number literal found" | Not one literal, as F says; `DOUBLE DOUBLE` + error 2 (5.2) |
| `1.e5`, `1._5` | `INT . IDENTIFIER`, then "value e5 / _5 is not a member of Int" | `INT DOT IDENTIFIER` (5.2), consistent with `number/dot_lookahead` |
| `1_.5`, `1.5_D` | lexical error "trailing separator is not allowed" | Error 1; add tests (5.2) |
| `"""a""""` | accepted, value `a"` | The last three quotes close the string (5.3) |
| `-0` | accepted, `0` | `OP_MINUS INT 0`, as F 22. 9. says (5.2) |

## 7. Recommended changes, in order

1. **Fix the 11 cases in section 1.** Remove the "project clarification" from `run.py` and the
   README.
2. **Decide `/*a\nb*///text`** (section 2). Most likely flip it to valid.
3. **Add a `layout/` fragment group** for every item in 5.1. Agree on the error code for an
   inconsistent dedent first, or ask on the forum.
4. **Add the numeric gaps from 5.2.** At minimum: unsupported forms (`0x`, `L`, `f`), `2e0.01`,
   `12abc`, `-0`.
5. **Agree the Unit and `;` token conventions** (section 3) and update `token.h`'s comment for
   `TOKEN_SEPARATOR`.
6. **Add extension keyword tests and identifier checks** (`to`, `until`, `by`, `s`).
7. **When extensions are chosen,** make the section 4 cases conditional and add the 5.7 groups.

## 8. Status of the fixes (2026-10-19)

Applied to `run.py`: 462 cases became 523.

- **Section 1, done.** The 11 cases are now valid literals, referencing `FORUM_NUMBERS`. Also
  added: `1___2`, `2e001`, `2e-001`, `1_2__3.4__5e6__7`. The "project clarification" is removed
  from `run.py` and the README.
- **Section 2, done.** `/*a\nb*///text` is replaced by the valid case
  `comment/line_after_multiline`. `comment/invalid_code_after_multiline` keeps error 1.
- **Section 5.1, partly done.** The new `layout/` group has 20 cases: same depth, multi-level
  and partial dedent, EOF dedents, blank, whitespace-only and comment-only lines, LF in
  parentheses, a multi-line string, if/else, while, and deep nesting.
  Inconsistent dedent is now 3 cases with error 1 (team decision). Still open: continuation-line
  indentation inside parentheses, a tab at the start of a comment-only line, and an indented
  first line.
- **Section 5.2, partly done:**
  - added: `01.5`, `00.5`, `1_.5`, `1.5_D`, `1.e5`, `1._5`, `-0`, `(((())))`, `()+42`;
  - team decisions applied: every case below is lexical error 1, referencing `TEAM_DECISION`:
    - numbers glued to a letter or a second fraction: `12abc`, `0abc`, `1.5x`, `1e5e`,
      `1.5Dx`, `2e0.01`, `1.5.5`;
    - unsupported forms: `0x1F`, `0X1f`, `0b101`, `1L`, `1l`, `1.5f`, `1.5F`, `1f`, `1e2f`;
    - digits with only a suffix: `1d`, `1D`, `0d`, `1_000D`.

    For the glued numbers and `1d` this is deliberately stricter than Scala (section 6), which
    splits them or accepts `1d`.
- **Sections 5.3–5.6, done:**
  - strings: physical tab in a string, `"""a""""`, tokens after a multi-line string;
  - comments: `*/` outside a comment, one-line block comment after a multi-line one, line
    comment after a multi-line one, the prolog with a comment and with spacing;
  - keywords and identifiers: the 8 extension keywords, `to`, `until`, `by`, `s`, `s "x"`,
    `___`;
  - characters and annotations: `%`, `^`, 0x01, 0x7F, `@mainx`, `@main(`, `@main def`.
- **Sections 3, 4 and 5.7, not changed.** These are team decisions (Unit and `;` tokens) and
  depend on which extensions are chosen.

Result with the lexer as of 2026-10-19, after the decision cases: 546 cases, 477 pass in both
modes. All 23 decision cases fail because the lexer splits the glued and unsupported numbers
into tokens and accepts inconsistent dedents. Every other new failure was checked; each one is a
lexer behaviour, not an expectation error:

- **`layout/` (12 new failures):**
  - INDENT is emitted again on same-depth lines;
  - DEDENT is emitted after the dedented line's first token instead of before it;
  - LF inside parentheses produces EOL and INDENT;
  - a tab on a whitespace-only line is rejected.
- **Numbers:** the lexer rejects repeated `__` and `01.5`.
- **Strings:** `"""a""""` ends the string too early.
- **Comments:** any comment after a multi-line block comment is rejected.
- **Annotations:** `@mainx` becomes `ANNOTATION` + `IDENTIFIER`.
