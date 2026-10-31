#!/usr/bin/env python3
"""Build the real lexer and write isolated, strict test results to test.output."""

import argparse
from dataclasses import dataclass
import hashlib
import math
import os
from pathlib import Path
import resource
import re
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "tests" / "lexer" / "exmaples_to_test"
LEXICAL = "IFJ26 S §1.1 (pp. 2–3); assignment-context/02-lexical-and-layout.md"
LAYOUT = "IFJ26 S §1.2 (pp. 3–4); assignment-context/02-lexical-and-layout.md"
API = "Module interface/test conventions (src/lexer.h, src/token.h), not language-specification requirements"
BASE_KEYWORDS = tuple("def do Double else if import Int return String then Unit val var while".split())
KEYWORDS_REFERENCE = "IFJ26 S §1.1 (p. 2); assignment-context/source-text/specification.txt:65–70"
EXTENSION_KEYWORDS = tuple("Boolean case class extension for List match type".split())
FORUM_NUMBERS = ("IFJ26 forum (Ondryas, 2026-10-11): repeated separators are allowed; "
                 "2e001 and 2e-001 are valid Double literals")
TEAM_DECISION = "Team decision 2026-10-19 (SPEC_AUDIT.md §8), within IFJ26 S §1.1/§1.2 and §6 code 1"
EXTENSIONS = "IFJ26 S §5 (pp. 10–17): the project implements all extensions"
INTERP_CONVENTION = ("IFJ26 S §5.6 INTERPOLATEDS (pp. 13–14); team convention (token.h): INTERP_BEGIN, then STRING "
                     "chunks (possibly empty) alternating with $id / ${...} splices, then INTERP_END")
CR_REFERENCE = ("IFJ26 S §1.2 (p. 3): LF ends a physical line; Scala 3.4 lexical syntax: CR is whitespace "
                "(https://www.scala-lang.org/files/archive/spec/3.4/01-lexical-syntax.html)")
STACK_LIMIT = 8 * 1024 * 1024


@dataclass(frozen=True)
class Token:
    kind: str
    value: object = None
    line: int | None = None


@dataclass(frozen=True)
class Case:
    name: str
    source: bytes
    expected: tuple[Token, ...]
    reason: str
    reference: str = LEXICAL
    error: int = 0
    pipe: bool = False
    api: str = ""


def t(kind, value=None, line=None):
    """Keep expected sequences readable without copying token enum numbers."""
    return Token("TOKEN_" + kind, value, line)


def kw(word):
    """Keyword token with its keyword_list name, e.g. kw("Double") -> KW_DOUBLE."""
    return t("KEYWORD", "KW_" + word.upper())


def category(case):
    """Report source of an expectation: assignment rule, module/API contract, or team decision."""
    if case.api or case.reference.startswith(API):
        return "API contract"
    if "team " in case.reference.lower():
        return "team decision"
    return "assignment"


def check_layout_balance(name, expected, complete):
    """Catch impossible INDENT/DEDENT expectations before any C code runs."""
    depth = 0
    for token in expected:
        depth += {"TOKEN_INDENT": 1, "TOKEN_DEDENT": -1}.get(token.kind, 0)
        if depth < 0:
            raise ValueError(f"{name}: expected DEDENT below indentation level 0")
    if complete and depth:
        raise ValueError(f"{name}: expected sequence leaves {depth} INDENT level(s) open at EOF")


def logical_lines(expected, at_eof):
    """Apply the lexer's logical-line EOL policy to a physical-EOL expectation.

    EOL ends only a line that produced a content token (blank/comment-only lines emit nothing),
    is suppressed inside parentheses, and is synthesized at EOF after an unterminated last line
    (only when the scan reaches EOF, i.e. not for a prefix that ends in a lexical error).
    INDENT/DEDENT are layout tokens and never count as line content.
    """
    result = []
    depth = 0
    has_content = False
    for token in expected:
        if token.kind == "TOKEN_EOL":
            if depth == 0 and has_content:
                result.append(token)
                has_content = False
            continue
        if token.kind == "TOKEN_OPEN_PAREN":
            depth += 1
        elif token.kind == "TOKEN_CLOSE_PAREN":
            depth = max(0, depth - 1)
        result.append(token)
        if token.kind not in ("TOKEN_INDENT", "TOKEN_DEDENT"):
            has_content = True
    if at_eof and has_content:
        result.append(t("EOL"))
    return tuple(result)


def cases():
    """Requirements-based fixtures; no expected-failure exemptions."""
    result = []

    def add(name, source, expected=(), reason="", reference=LEXICAL, error=0, pipe=False, api=""):
        if isinstance(source, str):
            source = source.encode("ascii")
        expected = logical_lines(expected, at_eof=not error)
        check_layout_balance(name, expected, complete=not error)
        if not error and not api:
            expected += (t("EOF"), t("EOF"))
        result.append(Case(name, source, expected, reason, reference, error, pipe, api))

    for name in ("constructor", "null_input", "null_table", "null_error", "sticky_error", "initial_line",
                 "independent_instances", "attribute_lifetime"):
        add("api/" + name, "", reason={
            "constructor": "Constructor preserves borrowed input/table and initializes all scanner fields.",
            "null_input": "NULL input is rejected without dereferencing it (current constructor contract).",
            "null_table": "NULL table is rejected without dereferencing it (current constructor contract).",
            "null_error": "NULL error pointer is rejected without dereferencing it.",
            "sticky_error": "An existing error returns UNDEFINED without consuming input or overwriting the error.",
            "initial_line": "Lexer-owned line counter starts at 1; EOF of empty input reports line 1.",
            "independent_instances": "Interleaved lexers keep input, attributes and line counters independent.",
            "attribute_lifetime": "Stored strings survive buffer reallocation and freeing the lexer that created them.",
        }[name], reference=API, api=name)

    for name, source, expected in [
        ("empty", "", []),
        ("spaces", "     ", []),
        ("no_final_lf", "name", [t("IDENTIFIER", "name")]),
        ("final_lf", "name\n", [t("IDENTIFIER", "name"), t("EOL")]),
        ("physical_lfs", "\n\n", [t("EOL"), t("EOL")]),
    ]:
        add("stream/" + name, source, expected, "Raw scanner termination and stable repeated EOF.", API)

    for word in ("a", "Z", "abc123", "_a", "_1", "__", "___", "a_b_C9", "foo_", "main", "ifj26",
                 "If", "DEF", "int", "double", "unit", "defx", "if_", "while2"):
        add("identifier/" + word, word, [t("IDENTIFIER", word)],
            "Identifiers preserve spelling and case; a keyword prefix is not a keyword.")
    for word in ("to", "until", "by", "s"):
        add("identifier/" + word, word, [t("IDENTIFIER", word)],
            "Not a keyword: to/until/by are Scala methods used by CONTROL, and 's' is an identifier unless glued "
            "to a string quote (INTERPOLATEDS).",
            "IFJ26 forum (Ondryas, 2026-09-29 and 2026-10-11); IFJ26 S §1.1 keyword list")
    add("identifier/s_space_string", 's "x"', [t("IDENTIFIER", "s"), t("STRING", "x")],
        "'s' separated from the quote by whitespace is an identifier followed by an ordinary string.",
        "IFJ26 forum (Ondryas, 2026-10-11): 's' must be immediately before the opening quote")
    add("identifier/standalone_underscore", "_", [t("UNDERSCORE")],
        "A lone underscore must not become an identifier; token.h exposes TOKEN_UNDERSCORE.")
    add("identifier/distinct_values", "same Same same other", [t("IDENTIFIER", x) for x in
        ("same", "Same", "same", "other")], "Attribute values remain valid across buffer reuse and interning.")

    for word in BASE_KEYWORDS:
        add("keyword/" + word, word, [kw(word)],
            f"The assignment reserves '{word}' as a keyword: the lexer must return TOKEN_KEYWORD "
            f"with attribute KW_{word.upper()} from token.h keyword_list.", KEYWORDS_REFERENCE)
        for label, identifier in (("case", word.swapcase()), ("suffix", word + "0"), ("prefix", "_" + word)):
            add(f"keyword_boundary/{word}_{label}", identifier, [t("IDENTIFIER", identifier)],
                f"Only the exact spelling '{word}' is reserved; '{identifier}' is an identifier. "
                "Identifiers and keywords are case-sensitive.", KEYWORDS_REFERENCE)
    for word in EXTENSION_KEYWORDS:
        add("keyword/extension_" + word, word, [kw(word)],
            f"'{word}' is in the S §1.1 keyword list (extension group: 'v základním zadání je vhodné je na místě "
            "identifikátoru nepřijímat'); all extensions are implemented, so it is a real keyword.",
            KEYWORDS_REFERENCE + "; " + EXTENSIONS)
    add("keyword/all_base", " ".join(BASE_KEYWORDS), [kw(word) for word in BASE_KEYWORDS],
        "Every base keyword from the assignment's first group must remain a keyword in a token sequence.",
        KEYWORDS_REFERENCE)
    add("keyword/def_comment_boundary", "def/**/name", [kw("def"), t("IDENTIFIER", "name")],
        "A block comment separates tokens: 'def' stays a keyword and 'name' stays an identifier.", KEYWORDS_REFERENCE)
    add("keyword/def_parenthesis_boundary", "def(", [kw("def"), t("OPEN_PAREN")],
        "A punctuation boundary ends 'def'; lexical classification must not depend on parser validity.", KEYWORDS_REFERENCE)

    operators = {"=": "ASSIGN", "+": "PLUS", "-": "MINUS", "*": "MULTIPLY", "/": "DIVISION",
                 "==": "EQUAL", "!=": "NOT_EQUAL", ">": "GREATER", ">=": "EQUAL_GREATER",
                 "<": "LESS", "<=": "EQUAL_LESS"}
    for spelling, name in operators.items():
        add("operator/" + name.lower(), spelling, [t("OPERATOR", "OP_" + name)],
            "Recognize the complete operator at EOF without losing lookahead.")
        add("operator/adjacent_" + name.lower(), "x" + spelling + "2",
            [t("IDENTIFIER", "x"), t("OPERATOR", "OP_" + name), t("INT", 2)],
            "Operator lookahead must leave the next token untouched.")
    add("operator/maximal_munch", "===!==<<=>>=",
        [t("OPERATOR", "OP_" + x) for x in
         ("EQUAL", "ASSIGN", "NOT_EQUAL", "ASSIGN", "LESS", "EQUAL_LESS", "GREATER", "EQUAL_GREATER")],
        "Multi-character operators use longest matches; following operator characters remain available.")
    add("operator/unary_minus", "-12 -.5", [t("OPERATOR", "OP_MINUS"), t("INT", 12),
        t("OPERATOR", "OP_MINUS"), t("DOUBLE", 0.5)], "The minus sign is a separate operator, not part of a literal.")
    for spelling, kind in ((".", "DOT"), (",", "COMMA"), (":", "COLON"),
                           ("(", "OPEN_PAREN"), (")", "CLOSE_PAREN")):
        add("punctuation/" + kind.lower(), spelling, [t(kind)], "Recognize punctuation at EOF.")
    add("punctuation/unit", "()", [t("OPEN_PAREN"), t("CLOSE_PAREN")],
        "Raw scanner convention: Unit syntax is a pair of parentheses; semantic Unit belongs to the parser.", API)
    add("punctuation/unit_nested", "(((())))", [t("OPEN_PAREN")] * 4 + [t("CLOSE_PAREN")] * 4,
        "Unit wrapped in parentheses may appear in tests; same paren-pair convention as punctuation/unit.",
        "IFJ26 forum (Ondryas, 2026-10-13); team convention: Unit is OPEN_PAREN CLOSE_PAREN")
    add("punctuation/unit_expression", "()+42", [t("OPEN_PAREN"), t("CLOSE_PAREN"),
        t("OPERATOR", "OP_PLUS"), t("INT", 42)],
        "Unit may stand wherever a term is expected, e.g. 'val a = () + 42' (syntactically valid).",
        "IFJ26 forum (Ondryas, 2026-10-13); team convention: Unit is OPEN_PAREN CLOSE_PAREN")
    add("punctuation/semicolon", "1;2", [t("INT", 1), t("SEMICOLON"), t("INT", 2)],
        "S §1.2 requires semicolon statement separation; ';' is TOKEN_SEMICOLON ('|' keeps TOKEN_SEPARATOR).",
        LAYOUT)
    add("annotation/main", "@main", [t("ANNOTATION")], "Recognize the exact @main annotation.",
        "IFJ26 S §2 (p. 4); assignment-context/04-program-scopes-functions-control.md")
    for source in ("@", "@m", "@ma", "@mai", "@Main", "@ main", "@mainx"):
        add("annotation/invalid_" + source.replace(" ", "_"), source, error=1,
            reason="Incomplete or differently spelled @main cannot form the required annotation.")
    add("annotation/main_paren", "@main(", [t("ANNOTATION"), t("OPEN_PAREN")],
        "The annotation ends at the first character that cannot continue an identifier.",
        "IFJ26 S §2 (p. 4)")
    add("annotation/main_def_same_line", "@main def", [t("ANNOTATION"), kw("def")],
        "The annotation may be on the same line as the def keyword.", "IFJ26 S §2 (p. 4)")
    for name, source in (("spaced", "import  ifj26 . *"), ("comment", "import/* komentar */ifj26.*")):
        add("prolog/" + name, source, [kw("import"), t("IDENTIFIER", "ifj26"), t("DOT"),
            t("OPERATOR", "OP_MULTIPLY")],
            "The prolog allows arbitrary whitespace between its tokens; a block comment without LF separates tokens.",
            "IFJ26 S §2 (p. 4); IFJ26 forum (2026-09-23): 'import/* komentar */ifj26.*' is valid")

    integers = {"0": 0, "00": 0, "0_0": 0, "7": 7, "42": 42, "2147483647": 2147483647,
                "1_000_000": 1000000, "2_1_4": 214}
    for source, value in integers.items():
        add("integer/" + source, source, [t("INT", value)],
            "Decimal value must be exact; digit separators do not change the value. Only nonzero integers ban leading zero.")
    for source, value in {"1__2": 12, "1___2": 12, "0__0": 0}.items():
        add("integer/" + source, source, [t("INT", value)],
            "Several consecutive separators between two digits are allowed, as in Scala.", FORUM_NUMBERS)
    for source in ("01", "0123", "00_1", "0_1", "1_", "0_", "1__"):
        add("integer/invalid_" + source, source, error=1,
            reason="A malformed integer is a lexical error (1), not several valid tokens or an internal error.")

    doubles = {
        "0.0": 0.0, ".5": .5, "1.25": 1.25, "1e2": 100.0, "1E+2": 100.0,
        "1e-2": .01, "0e0": 0.0, "1.0e0": 1.0, "1.2e3": 1200.0,
        "1.2E-3": .0012, ".5e+2": 50.0, "1.25d": 1.25, "1.25D": 1.25,
        ".5D": .5, "1e2d": 100.0, "1E-2D": .01, "1_2.3_4": 12.34,
        "1e1_0": 1e10, "1.2_5e+0": 1.25, "1e000": 1.0,
        "1e0": 1.0, "1e00": 1.0, "1e+00": 1.0, "1e-00": 1.0,
        "1e0_0": 1.0, "1.5E00_0D": 1.5,
        "1e-100": 1e-100, "1e100": 1e100, "1.0000000000000002": 1.0000000000000002,
        # Finite binary64 boundaries: subnormal results set ERANGE in strtod but are valid literals.
        "1e-308": 1e-308, "5e-324": 5e-324, "2.2250738585072014e-308": 2.2250738585072014e-308,
        "1.7976931348623157e308": 1.7976931348623157e308,
    }
    for source, value in doubles.items():
        add("double/" + source, source, [t("DOUBLE", value)],
            "Check binary64 value exactly, including signs, all-zero exponents, separators and optional d/D.")
    forum_doubles = {
        "1e01": 10.0, "1e+01": 10.0, "1e-01": 0.1, "1e09": 1e9, "2e001": 20.0, "2e-001": 0.2,
        "1e0_1": 10.0, "1.5E00_2D": 150.0, "1e2__3": 1e23, "1e0__0": 1.0, "1.2__3": 1.23,
        "1_2__3.4__5e6__7": 123.45e67,
    }
    for source, value in forum_doubles.items():
        add("double/" + source, source, [t("DOUBLE", value)],
            "Exponent digits may start with zero and separators may repeat; the value is exact binary64.",
            FORUM_NUMBERS)
    for source, value in {"01.5": 1.5, "00.5": 0.5}.items():
        add("double/" + source, source, [t("DOUBLE", value)],
            "The leading-zero ban applies to Int literals only; the integer part of a Double is any digit sequence "
            "(Scala 3 accepts it too).")
    for source in ("1e", "1E", "1e+", "1e-", "1e_2", "1e+_2", "1e--2", "1e2_",
                   "1e0_", "1.2_", ".5_", "1.2e", "1.2e+", "1_e2", "1_.5", "1.5_D", "1.2__", "1e2__"):
        add("double/invalid_" + source, source, error=1,
            reason="Missing exponent digits or a separator not between two digits must return lexical error 1 "
                   "(Scala: 'trailing separator is not allowed').")
    add("number/dot_lookahead", "1.foo 1. .5", [t("INT", 1), t("DOT"), t("IDENTIFIER", "foo"),
        t("INT", 1), t("DOT"), t("DOUBLE", .5)],
        "A dot without a following fractional digit is punctuation: the Double form requires a fractional part.")
    add("number/dot_identifier", "1.e5 1._5", [t("INT", 1), t("DOT"), t("IDENTIFIER", "e5"),
        t("INT", 1), t("DOT"), t("IDENTIFIER", "_5")],
        "Without a fractional digit after the dot, 'e5' and '_5' are identifiers (Scala: member selection).")
    add("number/minus_zero", "-0", [t("OPERATOR", "OP_MINUS"), t("INT", 0)],
        "Unary minus before zero is valid; the sign is a separate operator.",
        "IFJ26 forum (Ondryas, 2026-09-22): '-0' is valid")
    for source in ("12abc", "0abc", "1.5x", "1e5e", "1.5Dx", "2e0.01", "1.5.5"):
        add("number/invalid_glued_" + source, source, error=1,
            reason="A number immediately followed by a letter or by another fraction is one malformed lexeme "
                   "(lexical error 1), not several tokens.", reference=TEAM_DECISION)
    for source in ("0x1F", "0X1f", "0b101", "1L", "1l", "1.5f", "1.5F", "1f", "1e2f"):
        add("number/invalid_unsupported_" + source, source, error=1,
            reason="Other number bases, the L suffix and the f/F suffixes are not supported in IFJ26 "
                   "(S §1.1); the unsupported literal is lexical error 1.", reference=TEAM_DECISION)
    for source in ("1d", "1D", "0d", "1_000D"):
        add("number/invalid_int_suffix_" + source, source, error=1,
            reason="The d/D suffix only ends the two Double forms (with a fraction or an exponent); digits + suffix "
                   "alone is lexical error 1.", reference=TEAM_DECISION)
    add("number/adjacency", "12+34*(.5-2e1)", [t("INT", 12), t("OPERATOR", "OP_PLUS"), t("INT", 34),
        t("OPERATOR", "OP_MULTIPLY"), t("OPEN_PAREN"), t("DOUBLE", .5), t("OPERATOR", "OP_MINUS"),
        t("DOUBLE", 20.0), t("CLOSE_PAREN")], "Number scanning must not swallow punctuation or operators.")

    strings = [("empty", '""', ""), ("ascii", '"Hello, world!"', "Hello, world!"),
               ("comments_are_text", '"// /* */"', "// /* */"),
               ("leading_escape", '"\\nrest"', "\nrest"),
               ("all_escapes", '"\\\\\\\"\\n\\r\\t"', '\\"\n\r\t'),
               ("multiline_empty", '""""""', ""),
               ("multiline_spaces", '"""A\n  hoj s\\nvete\n   """', "A\n  hoj s\\nvete\n   "),
               ("multiline_raw", '"""\\q\\n\\t // /* */"""', "\\q\\n\\t // /* */"),
               ("multiline_one_quote", '"""a"b"""', 'a"b'),
               ("multiline_two_quotes", '"""a""b"""', 'a""b'),
               ("multiline_physical_tab", '"""a\tb\n c"""', "a\tb\n c"),
               ("physical_tab", '"a\tb"', "a\tb"),
               ("multiline_four_quotes", '"""a""""', 'a"'),
               ("multiline_crlf", '"""x\r\ny"""', "x\r\ny")]
    for name, source, value in strings:
        add("string/" + name, source, [t("STRING", value)],
            "String value must match byte for byte; ordinary strings decode escapes, triple-quoted strings preserve content.")
    for escape in ('\\', '"', 'n', 'r', 't'):
        value = {'\\': '\\', '"': '"', 'n': '\n', 'r': '\r', 't': '\t'}[escape]
        add("string/escape_" + str(ord(escape)), '"a\\' + escape + 'b"', [t("STRING", "a" + value + "b")],
            "Each of the five allowed escapes must decode correctly inside a string.")
    for name, source in [
        ("single_quote_eof", '"'), ("text_eof", '"abc'), ("escape_eof", '"abc\\'),
        ("physical_lf", '"a\nb"'), ("initial_lf", '"\n"'),
        ("triple_open_eof", '"""'), ("triple_text_eof", '"""abc'),
        ("triple_one_quote_eof", '"""abc"'), ("triple_two_quotes_eof", '"""abc""'),
    ]:
        add("string/invalid_" + name, source, error=1,
            reason="Unterminated strings and physical LF in ordinary strings must fail with lexical error 1.")
    for char in "abfv0xusq/":
        add("string/invalid_escape_" + char, '"\\' + char + '"', error=1,
            reason="Only backslash, quote, n, r and t are allowed escape characters.")
    for code in range(32, 127):
        char = chr(code)
        if char not in '\\"nrtabfv0xusq/':
            add(f"string/invalid_escape_ascii_{code}", '"\\' + char + '"', error=1,
                reason="Exhaust every other printable ASCII escape; the allowed set is exactly five characters.")
    printable = "".join(chr(code) for code in range(32, 127) if chr(code) not in '\\"')
    add("string/all_safe_printable_ascii", '"' + printable + '"', [t("STRING", printable)],
        "Every printable ASCII character except delimiter/escape characters is preserved literally.")
    add("string/buffer_reuse", '"first" "" "last" first', [t("STRING", "first"), t("STRING", ""),
        t("STRING", "last"), t("IDENTIFIER", "first")], "Later tokens must not mutate earlier attribute values.")
    add("string/multiline_then_tokens", '"""a\nb""" + y', [t("STRING", "a\nb"), t("OPERATOR", "OP_PLUS"),
        t("IDENTIFIER", "y")], "A multiline string is part of its logical line; tokens may follow on its last line.")

    for name, source, expected in [
        ("line_eof", "// no LF", []), ("line_lf", "x//ignored\ny", [t("IDENTIFIER", "x"), t("EOL"), t("IDENTIFIER", "y")]),
        ("line_empty", "//\n", [t("EOL")]), ("block_empty", "/**/", []),
        ("block_single", "/* text */", []), ("block_nested", "/* outer /* inner */ tail */x", [t("IDENTIFIER", "x")]),
        ("block_separates", "ab/**/cd", [t("IDENTIFIER", "ab"), t("IDENTIFIER", "cd")]),
        ("block_number_boundary", "1/**/2", [t("INT", 1), t("INT", 2)]),
        ("block_multiline", "x/* a\nb */  \ny", [t("IDENTIFIER", "x"), t("EOL"), t("IDENTIFIER", "y")]),
        ("block_multiline_eof", "/* a\nb */", []),
        ("markers_in_line", "// /* \" ignored\nx", [t("EOL"), t("IDENTIFIER", "x")]),
        ("quotes_in_block", '/* " */x', [t("IDENTIFIER", "x")]),
        ("slashes_in_block", "/* // text */x", [t("IDENTIFIER", "x")]),
        ("asterisks", "/* **/x", [t("IDENTIFIER", "x")]),
        ("reset_after_multiline", "/*a\nb*/\n/*c*/x", [t("EOL"), t("IDENTIFIER", "x")]),
        ("line_after_multiline", "x/*a\nb*///text\ny", [t("IDENTIFIER", "x"), t("EOL"), t("IDENTIFIER", "y")]),
        ("block_after_multiline", "x/*a\nb*/ /*c*/\ny", [t("IDENTIFIER", "x"), t("EOL"), t("IDENTIFIER", "y")]),
        ("close_marker_outside", "*/", [t("OPERATOR", "OP_MULTIPLY"), t("OPERATOR", "OP_DIVISION")]),
        ("line_in_parens", "f(1, // c\n  2)", [t("IDENTIFIER", "f"), t("OPEN_PAREN"), t("INT", 1), t("COMMA"),
                                               t("EOL"), t("INT", 2), t("CLOSE_PAREN")]),
    ]:
        add("comment/" + name, source, expected,
            "Comments preserve token boundaries and nesting; raw scanner emits physical EOL at comment termination.")
    for name, source in [("open", "/*"), ("text", "/* text"), ("star_eof", "/*x*"),
                         ("nested", "/* outer /* inner */"), ("overlap", "/*/"),
                         ("nested_overlap", "/* /*/ */"), ("code_after_multiline", "/*a\nb*/x")]:
        add("comment/invalid_" + name, source, error=1,
            reason="All nesting must close. After a block comment containing LF, only whitespace is allowed until line end.")
    add("comment/deep_nesting", "/*" * 64 + "body" + "*/" * 64 + "x", [t("IDENTIFIER", "x")],
        "Nested comments must close at the matching depth, without leaking comment state.")

    for char in "#$%&?^`'\\{}~":
        add("invalid/ascii_" + str(ord(char)), char, error=1,
            reason="This character cannot begin a lexeme, even with all extensions ('$', '{', '}' are valid only "
                   "inside an interpolated string; a lone '&' is not '&&').")
    add("invalid/nul", b"\0", error=1, reason="An input NUL byte must not be mistaken for EOF.")
    for code in (0x01, 0x7F):
        add(f"invalid/control_{code:02x}", bytes([code]), error=1,
            reason="A control character outside strings and comments is an unknown character (lexical error 1).")
    for name, source in (("first_column", "\tx"), ("after_space", " \tx"), ("after_lf", "\n\tx")):
        prefix = [t("EOL")] if source.startswith("\n") else []
        add("layout/leading_tab_" + name, source, prefix, error=1, reference=LAYOUT,
            reason="A tab in indentation of a nonempty line is lexical error 1.")
    add("layout/interior_tab", "x\ty", [t("IDENTIFIER", "x"), t("IDENTIFIER", "y")],
        "Tab is whitespace between tokens; the lexical tab prohibition applies to indentation of nonempty lines.", LAYOUT)
    add("layout/tab_after_multiline_comment", "/*a\nb*/\t\nx", [t("EOL"), t("IDENTIFIER", "x")],
        "Whitespace after a multiline block comment is allowed until the physical line ends.", LAYOUT)
    add("layout/cr_between_tokens", "a\rb", [t("IDENTIFIER", "a"), t("IDENTIFIER", "b")],
        "CR is whitespace between tokens and does not end a physical line.", CR_REFERENCE)
    add("layout/crlf", "a\r\nb", [t("IDENTIFIER", "a", 1), t("EOL"), t("IDENTIFIER", "b", 2)],
        "CRLF ends a line through its LF; only LF increments the line counter.", CR_REFERENCE)

    for name, source, expected in [
        ("identifier", "abc", t("IDENTIFIER", "abc")), ("integer", "12", t("INT", 12)),
        ("zero", "0", t("INT", 0)), ("double", "1.5", t("DOUBLE", 1.5)),
        ("suffix", "1.5D", t("DOUBLE", 1.5)), ("exponent", "1e2", t("DOUBLE", 100.0)),
        ("string", '"x"', t("STRING", "x")), ("multiline", '"""x"""', t("STRING", "x")),
    ]:
        for label, suffix, following in [
            ("space", " z", [t("IDENTIFIER", "z")]), ("newline", "\nz", [t("EOL"), t("IDENTIFIER", "z")]),
            ("comma", ",z", [t("COMMA"), t("IDENTIFIER", "z")]), ("close_paren", ")", [t("CLOSE_PAREN")]),
            ("comment", "/**/z", [t("IDENTIFIER", "z")]), ("plus", "+2", [t("OPERATOR", "OP_PLUS"), t("INT", 2)]),
        ]:
            add(f"boundary/{name}_{label}", source + suffix, [expected] + following,
                "Finishing a token must not consume the next separator, operator, comment or lexeme.")

    # Only metadata-specific cases constrain token.line, keeping unrelated failures independent.
    add("line/ordinary", "a\nb", [t("IDENTIFIER", "a", 1), t("EOL", line=1), t("IDENTIFIER", "b", 2)],
        "Token positions use the 1-based starting physical line; caller supplies a counter initialized to 1.", API)
    add("line/multiline_string", '"""a\nb""" x', [t("STRING", "a\nb", 1), t("IDENTIFIER", "x", 2)],
        "A multiline token must retain its starting line, while the next token uses the updated line.", API)
    add("line/comment_then_identifier", "y/*a\nb*/\nx",
        [t("IDENTIFIER", "y", 1), t("EOL", line=2), t("IDENTIFIER", "x", 3)],
        "Comment LF updates the shared counter; the EOL ending y's logical line is on line 2.", API)
    add("line/escaped_lf", '"\\n" x', [t("STRING", "\n", 1), t("IDENTIFIER", "x", 1)],
        "Decoded newline escapes must not increment the physical source line.", API)
    add("line/multiline_crlf_string", '"""x\r\ny""" z', [t("STRING", "x\r\ny", 1), t("IDENTIFIER", "z", 2)],
        "A multiline string keeps CR and LF; only the LF advances the line of the next token.", API)
    # The parser needs comma/close-paren lines to tell f(1,) (error) from f(1,<LF>) (S §1.2 trailing comma).
    f_call = [t("IDENTIFIER", "f", 1), t("OPEN_PAREN", line=1), t("INT", 1, 1), t("COMMA", line=1)]
    for name, source, close_line in [("trailing_comma_same_line", "f(1,)", 1),
                                     ("trailing_comma_lf", "f(1,\n  )", 2),
                                     ("trailing_comma_comment_lf", "f(1, // c\n  )", 2)]:
        add("line/" + name, source, f_call + [t("EOL")] * (close_line - 1) + [t("CLOSE_PAREN", line=close_line)],
            "Comma and close-paren lines must survive EOL suppression inside parentheses; "
            "trailing-comma legality is checked by the parser.", API)
    add("line/trailing_comma_def_params", "def g(a: Int,\n      b: Int,\n  ): Unit = ()", [
        kw("def"), t("IDENTIFIER", "g", 1), t("OPEN_PAREN"), t("IDENTIFIER", "a", 1), t("COLON"), kw("Int"),
        t("COMMA", line=1), t("EOL"), t("IDENTIFIER", "b", 2), t("COLON"), kw("Int"), t("COMMA", line=2), t("EOL"),
        t("CLOSE_PAREN", line=3), t("COLON"), kw("Unit"), t("OPERATOR", "OP_ASSIGN"), t("OPEN_PAREN"),
        t("CLOSE_PAREN")],
        "A multiline parameter list keeps every comma and the closing parenthesis on its own physical line.", API)

    for size in (126, 127, 128, 129, 254, 255, 256, 257, 1024, 4096):
        word = "a" * size
        add(f"buffer/identifier_{size}", word + " z", [t("IDENTIFIER", word), t("IDENTIFIER", "z")],
            "Cross initial/doubled buffer boundaries; preserve NUL termination and reset before the next token.", API)
        add(f"buffer/string_{size}", '"' + word + '" "z"', [t("STRING", word), t("STRING", "z")],
            "String storage grows safely and remains valid after buffer reuse.", API)
        add(f"buffer/triple_quote_pair_{size}", '"""' + word + '""b"""', [t("STRING", word + '""b')],
            "Two-quote fallback can append two bytes at a buffer boundary; sanitizer must report no overflow.", API)
    add("buffer/long_zero_integer", "0" * 1024, [t("INT", 0)],
        "Long all-zero literals test buffer growth without the assignment's excluded numeric-overflow cases.")
    add("buffer/long_fraction", "0." + "0" * 1024 + " " + "1", [t("DOUBLE", 0.0), t("INT", 1)],
        "Long fractions grow the numeric buffer and preserve following tokens.")
    add("buffer/long_comment", "/*" + "x" * 4096 + "*/a", [t("IDENTIFIER", "a")],
        "Long comments must terminate without crashing or dropping the following token.")
    add("buffer/long_line_comment", "//" + "x" * 4096 + "\na", [t("EOL"), t("IDENTIFIER", "a")],
        "Long line comments must terminate and leave the next line available.")
    add("buffer/long_whitespace", "a" + " " * 4096 + "b", [t("IDENTIFIER", "a"), t("IDENTIFIER", "b")],
        "Long runs of whitespace between tokens must not lose the next token (first-line indent is not tested here).")
    for kind in ("identifier", "string", "integer", "double"):
        words = [f"v{i}" if kind == "identifier" else f'"v{i}"' if kind == "string" else
                 str(i) if kind == "integer" else f"{i}.5" for i in range(150)]
        expected = [t({"identifier": "IDENTIFIER", "string": "STRING", "integer": "INT", "double": "DOUBLE"}[kind],
                      f"v{i}" if kind in ("identifier", "string") else i if kind == "integer" else i + .5)
                    for i in range(150)]
        add("attributes/growth_" + kind, " ".join(words + [words[0]]), expected + [expected[0]],
            "More than 100 distinct attributes trigger table growth; a repeated value must still be readable.", API)

    def lid(*names):
        return [t("IDENTIFIER", n) for n in names]

    ind, ded, eol = t("INDENT"), t("DEDENT"), t("EOL")
    a, b, c, d = lid("a", "b", "c", "d")
    block_cases = [
        ("indent_same_depth", "a\n  b\n  c\n", [a, eol, ind, b, eol, c, eol, ded],
         "Same width as the stack top emits no further INDENT; one DEDENT at EOF."),
        ("indent_two_levels_dedent_all", "a\n  b\n    c\nd\n", [a, eol, ind, b, eol, ind, c, eol, ded, ded, d, eol],
         "One DEDENT per popped level, emitted before the first token of the dedented line."),
        ("dedent_partial", "a\n  b\n    c\n  d\n", [a, eol, ind, b, eol, ind, c, eol, ded, d, eol, ded],
         "Dedent to a width still on the stack pops exactly one level; the rest close at EOF."),
        ("dedent_at_eof_no_lf", "a\n  b\n    c", [a, eol, ind, b, eol, ind, c, eol, ded, ded],
         "EOF without LF ends the logical line, then one DEDENT per remaining level."),
        ("reindent_after_dedent", "a\n  b\nc\n  d\n", [a, eol, ind, b, eol, ded, c, eol, ind, d, eol, ded],
         "A new deeper line after a dedent pushes a fresh level and emits INDENT again."),
        ("blank_line_in_block", "a\n  b\n\n  c\n", [a, eol, ind, b, eol, c, eol, ded],
         "Empty physical lines are ignored and do not change the indentation level."),
        ("whitespace_line_wider", "a\n  b\n      \n  c\n", [a, eol, ind, b, eol, c, eol, ded],
         "Whitespace-only lines do not change the indentation level, whatever their width."),
        ("whitespace_line_narrower", "a\n  b\n \nc\n", [a, eol, ind, b, eol, ded, c, eol],
         "A whitespace-only line of smaller width does not dedent; the next content line does."),
        ("tab_on_blank_line", "a\n  b\n\t\n  c\n", [a, eol, ind, b, eol, c, eol, ded],
         "The tab rule applies only to non-empty lines; a whitespace-only tab line is ignored."),
        ("comment_line_deeper", "a\n  b\n      // c\n  d\n", [a, eol, ind, b, eol, d, eol, ded],
         "Comment-only lines are ignored regardless of their indentation."),
        ("comment_line_column0", "a\n  b\n// c\n  d\n", [a, eol, ind, b, eol, d, eol, ded],
         "A comment-only line at column 0 does not dedent."),
        ("block_comment_line", "a\n  b\n    /* c */\n  d\n", [a, eol, ind, b, eol, d, eol, ded],
         "A line containing only a block comment is ignored for indentation."),
        ("multiline_block_comment_line", "a\n  b\n      /* x\ny */\n  c\n", [a, eol, ind, b, eol, c, eol, ded],
         "A multi-line block comment line is ignored; its inner lines never produce INDENT/DEDENT."),
        ("paren_continuation", "f(a,\n    b)\nc\n",
         lid("f") + [t("OPEN_PAREN"), a, t("COMMA"), b, t("CLOSE_PAREN"), eol, c, eol],
         "LF inside parentheses is whitespace: no EOL and no INDENT for the continuation line."),
        ("paren_nested_continuation", "f((a,\n  b),\n c)\n",
         lid("f") + [t("OPEN_PAREN"), t("OPEN_PAREN"), a, t("COMMA"), b, t("CLOSE_PAREN"), t("COMMA"), c,
                     t("CLOSE_PAREN"), eol],
         "Nested parentheses keep LF as whitespace across several physical lines."),
        ("paren_continuation_in_block", "a\n  f(b,\n      c)\n  d\n",
         [a, eol, ind] + lid("f") + [t("OPEN_PAREN"), b, t("COMMA"), c, t("CLOSE_PAREN"), eol, d, eol, ded],
         "A continuation line inside parentheses neither indents nor dedents the block."),
        ("multiline_string_inner_indent", "a\n  x = \"\"\"p\n        q\"\"\"\n  y\n",
         [a, eol, ind] + lid("x") + [t("OPERATOR", "OP_ASSIGN"), t("STRING", "p\n        q"), eol] +
         lid("y") + [eol, ded],
         "A multi-line string belongs to the logical line it starts on; inner lines give no INDENT."),
        ("if_else_shape", "if a then\n  b\nelse\n  c\n",
         [kw("if"), a, kw("then"), eol, ind, b, eol, ded, kw("else"), eol, ind, c, eol, ded],
         "Block bodies after if/then and else each get an INDENT and a DEDENT."),
        ("blank_line_at_eof", "a\n  b\n\n", [a, eol, ind, b, eol, ded],
         "Trailing blank lines are ignored; remaining levels close with DEDENT before EOF."),
        ("while_block", "while a do\n  b\nc\n", [kw("while"), a, kw("do"), eol, ind, b, eol, ded, c, eol],
         "A while body is an indented block closed by a DEDENT on return to column 0."),
        ("deep_nesting_dedent_to_zero", "a\n b\n  c\n   d\n    a\nb\n",
         [a, eol, ind, b, eol, ind, c, eol, ind, d, eol, ind, a, eol, ded, ded, ded, ded, b, eol],
         "Four nested levels all close with four DEDENTs when returning to column 0."),
        ("paren_continuation_logical_start", "a\n  f(\n      g(\n   x))\n",
         [a, eol, ind] + lid("f") + [t("OPEN_PAREN")] + lid("g") + [t("OPEN_PAREN")] + lid("x") +
         [t("CLOSE_PAREN"), t("CLOSE_PAREN"), eol, ded],
         "Continuation indent is measured from the logical line start (2), not the previous physical line (6)."),
        ("crlf_block", "a\r\n  b\r\n", [a, eol, ind, b, eol, ded],
         "CR before LF is trailing whitespace and does not disturb INDENT/DEDENT."),
        ("tab_line_comment_line", "a\n  b\n\t// c\n  d\n", [a, eol, ind, b, eol, d, eol, ded],
         "A tab before a comment-only line is allowed: the tab rule applies to indentation of nonempty lines."),
        ("tab_block_comment_line", "a\n  b\n\t/* c */\n  d\n", [a, eol, ind, b, eol, d, eol, ded],
         "A tab before a line holding only a block comment is allowed; the line is ignored for indentation."),
    ]
    for lay_name, lay_source, lay_expected, lay_reason in block_cases:
        add("layout/" + lay_name, lay_source, lay_expected, lay_reason, reference=LAYOUT)
    for lay_name, lay_source, lay_prefix in [
        ("invalid_dedent_unknown_width", "a\n    b\n  c\n", [a, eol, ind, b, eol]),
        ("invalid_dedent_between_levels", "a\n  b\n    c\n   d\n", [a, eol, ind, b, eol, ind, c, eol]),
        ("invalid_dedent_below_inner_block", "a\n  b\n      c\n    d\n", [a, eol, ind, b, eol, ind, c, eol]),
    ]:
        add("layout/" + lay_name, lay_source, lay_prefix, error=1, reference=TEAM_DECISION + "; S §1.2",
            reason="Dedenting to a width that was never opened is lexical error 1 (§6: 'chybná struktura … "
                   "odsazení'); tokens of earlier lines are returned first.")
    for lay_name, lay_source in [("invalid_continuation_same_width", "a\n  f(\n  x)\n"),
                                 ("invalid_continuation_narrower", "a\n  f(\n x)\n")]:
        add("layout/" + lay_name, lay_source, [a, eol, ind] + lid("f") + [t("OPEN_PAREN")], error=1,
            reference=TEAM_DECISION + "; S §1.2 (p. 3); error code still to be agreed",
            reason="A continuation line inside parentheses must be indented deeper than its logical line start.")

    def op(name):
        return t("OPERATOR", "OP_" + name)

    lp, rp, colon, comma, arrow, larrow = (t("OPEN_PAREN"), t("CLOSE_PAREN"), t("COLON"), t("COMMA"),
                                           t("ARROW"), t("LEFT_ARROW"))
    ob, cb, pipe, dot = t("OPEN_BRACKET"), t("CLOSE_BRACKET"), t("SEPARATOR"), t("DOT")
    f, i, n, v, x, y = lid("f", "i", "n", "v", "x", "y")
    print_i = lid("print") + [lp, i, rp]
    for name, source, expected, reason, section in [
        ("boolthen/true", "true", [t("BOOLEAN", 1)], "'true' is a Boolean literal with attribute 1.", "5.2 BOOLTHEN"),
        ("boolthen/false", "false", [t("BOOLEAN", 0)], "'false' is a Boolean literal with attribute 0.", "5.2 BOOLTHEN"),
        ("boolthen/literal_boundaries", "True FALSE true_ falsey", lid("True", "FALSE", "true_", "falsey"),
         "Only the exact lowercase spellings are Boolean literals.", "5.2 BOOLTHEN"),
        ("boolthen/not", "!x", [op("NOT"), x], "Unary negation '!' is an operator.", "5.2 BOOLTHEN"),
        ("boolthen/not_not", "! !x", [op("NOT"), op("NOT"), x], "Each '!' is a separate negation.", "5.2 BOOLTHEN"),
        ("boolthen/not_vs_not_equal", "a != !b", [a, op("NOT_EQUAL"), op("NOT"), b],
         "'!=' still takes precedence over '!' followed by '='.", "5.2 BOOLTHEN"),
        ("boolthen/and_or", "!a&&b||c", [op("NOT"), a, op("AND"), b, op("OR"), c],
         "'&&' and '||' are two-character operators recognized without surrounding spaces.", "5.2 BOOLTHEN"),
        ("boolthen/else_if", "if a then\n  b\nelse if c then\n  d\n",
         [kw("if"), a, kw("then"), eol, ind, b, eol, ded, kw("else"), kw("if"), c, kw("then"), eol, ind, d, eol, ded],
         "'else if' aligned with 'if' closes the first block and opens another.", "5.2 BOOLTHEN"),
        ("boolthen/if_without_else", "if a then\n  b\nc\n", [kw("if"), a, kw("then"), eol, ind, b, eol, ded, c, eol],
         "A conditional without else is closed by the dedent alone.", "5.2 BOOLTHEN"),
        ("control/generator", "i <- 1 to 10 by 2", [i, larrow, t("INT", 1)] + lid("to") + [t("INT", 10)] + lid("by") +
         [t("INT", 2)], "'<-' is a token; to/by stay identifiers.", "5.3 CONTROL"),
        ("control/left_arrow_glued", "i<-0", [i, larrow, t("INT", 0)],
         "'<-' is one reserved token even without spaces, as in Scala.", "5.3 CONTROL"),
        ("control/less_than_negative", "i < -1", [i, op("LESS"), op("MINUS"), t("INT", 1)],
         "Separated '<' and '-' stay a comparison with a negated literal.", "5.3 CONTROL"),
        ("control/for_one_line", "for i <- 0 until n do print(i)",
         [kw("for"), i, larrow, t("INT", 0)] + lid("until") + [n, kw("do")] + print_i,
         "Single-line for with a one-line body.", "5.3 CONTROL"),
        ("control/for_multiline", "for\n  i <- 0 until n\n  if i != 3\ndo\n  print(i)\n",
         [kw("for"), eol, ind, i, larrow, t("INT", 0)] + lid("until") + [n, eol, kw("if"), i, op("NOT_EQUAL"),
         t("INT", 3), eol, ded, kw("do"), eol, ind] + print_i + [eol, ded],
         "Generator and guards form an indented block; 'do' aligned with 'for' opens the body.", "5.3 CONTROL"),
        ("control/if_expression", "val y = if x < 0 then 0 - x else x",
         [kw("val"), y, op("ASSIGN"), kw("if"), x, op("LESS"), t("INT", 0), kw("then"), t("INT", 0), op("MINUS"), x,
          kw("else"), x], "if used as a one-line expression.", "5.3 CONTROL"),
        ("valmem/chain", 'number.floor.toInt "abc".length',
         lid("number") + [dot] + lid("floor") + [dot] + lid("toInt") + [t("STRING", "abc"), dot] + lid("length"),
         "Member access on identifiers and literals uses DOT and identifiers.", "5.4 VALMEM"),
        ("valmem/double_member", "(1.2345).toString", [lp, t("DOUBLE", 1.2345), rp, dot] + lid("toString"),
         "A parenthesized Double followed by a member.", "5.4 VALMEM"),
        ("algebdt/union_type", "type Scalar = Int | Double | String",
         [kw("type")] + lid("Scalar") + [op("ASSIGN"), kw("Int"), pipe, kw("Double"), pipe, kw("String")],
         "'|' in a union type is TOKEN_SEPARATOR.", "5.5 ALGEBDT"),
        ("algebdt/pipe_glued", "Int|String", [kw("Int"), pipe, kw("String")],
         "A single '|' is not '||'.", "5.5 ALGEBDT"),
        ("algebdt/case_class", "case class Point(x: Int, y: Int)",
         [kw("case"), kw("class")] + lid("Point") + [lp, x, colon, kw("Int"), comma, y, colon, kw("Int"), rp],
         "Product type definition.", "5.5 ALGEBDT"),
        ("algebdt/arrow", "x=>y", [x, arrow, y], "'=>' is one token, not '=' followed by '>'.", "5.5 ALGEBDT"),
        ("algebdt/arrow_vs_equal", "=> = == >=", [arrow, op("ASSIGN"), op("EQUAL"), op("EQUAL_GREATER")],
         "'=>' does not disturb '=', '==' and '>='.", "5.5 ALGEBDT"),
        ("algebdt/match", 'v match\n  case 0 => "zero"\n  case n: Int if n > 0 => "pos"\n'
         '  case Point(a, b) => a\n  case _ => "neg"\n',
         [v, kw("match"), eol, ind, kw("case"), t("INT", 0), arrow, t("STRING", "zero"), eol,
          kw("case"), n, colon, kw("Int"), kw("if"), n, op("GREATER"), t("INT", 0), arrow, t("STRING", "pos"), eol,
          kw("case")] + lid("Point") + [lp, a, comma, b, rp, arrow, a, eol,
          kw("case"), t("UNDERSCORE"), arrow, t("STRING", "neg"), eol, ded],
         "match with literal, typed, constructor and wildcard patterns plus a guard.", "5.5 ALGEBDT"),
        ("lambdas/function_type", "(Int, Int) => Int", [lp, kw("Int"), comma, kw("Int"), rp, arrow, kw("Int")],
         "Function type.", "5.7 LAMBDAS"),
        ("lambdas/unit_function_type", "() => Int", [lp, rp, arrow, kw("Int")],
         "Parameterless function type.", "5.7 LAMBDAS"),
        ("lambdas/lambda_block", "val f = (v: Int) =>\n  v + 1\n",
         [kw("val"), f, op("ASSIGN"), lp, v, colon, kw("Int"), rp, arrow, eol, ind, v, op("PLUS"), t("INT", 1), eol,
          ded], "A lambda body may be an indented block after '=>'.", "5.7 LAMBDAS"),
        ("lists/type", "List[List[Int]]", [kw("List"), ob, kw("List"), ob, kw("Int"), cb, cb],
         "Type arguments use brackets; ']]' is two tokens.", "5.8 LISTS"),
        ("lists/literal", "List(1, 2)", [kw("List"), lp, t("INT", 1), comma, t("INT", 2), rp],
         "List construction.", "5.8 LISTS"),
        ("lists/empty_typed", "val e: List[Int] = List()",
         [kw("val")] + lid("e") + [colon, kw("List"), ob, kw("Int"), cb, op("ASSIGN"), kw("List"), lp, rp],
         "Empty list with an expected type.", "5.8 LISTS"),
        ("lists/concat", "a ++ b++c", [a, op("CONCAT"), b, op("CONCAT"), c],
         "'++' is one operator, with or without spaces.", "5.8 LISTS"),
        ("lists/range_members", "List.range(1, 10).drop(1).head",
         [kw("List"), dot] + lid("range") + [lp, t("INT", 1), comma, t("INT", 10), rp, dot] + lid("drop") +
         [lp, t("INT", 1), rp, dot] + lid("head"), "Static range and chained members.", "5.8 LISTS"),
        ("lists/to_list", "(1 to 5).toList", [lp, t("INT", 1)] + lid("to") + [t("INT", 5), rp, dot] + lid("toList"),
         "Range converted to a list.", "5.8 LISTS"),
        ("extkw/block", "extension (v: Int)\n  def twice: Int = v * 2\n",
         [kw("extension"), lp, v, colon, kw("Int"), rp, eol, ind, kw("def")] + lid("twice") +
         [colon, kw("Int"), op("ASSIGN"), v, op("MULTIPLY"), t("INT", 2), eol, ded],
         "Extension header followed by an indented method block.", "5.9 EXTKW"),
        ("funexp/nested_calls", "2 * f(g() + 1)",
         [t("INT", 2), op("MULTIPLY"), f, lp] + lid("g") + [lp, rp, op("PLUS"), t("INT", 1), rp],
         "Calls nested inside expressions.", "5.1 FUNEXP"),
        ("paramlists/no_list", "def foo: Int = 42",
         [kw("def")] + lid("foo") + [colon, kw("Int"), op("ASSIGN"), t("INT", 42)],
         "Function without a parameter list.", "5.10 PARAMLISTS"),
        ("paramlists/two_lists", "def add(a: Int)(b: Int) = a + b",
         [kw("def")] + lid("add") + [lp, a, colon, kw("Int"), rp, lp, b, colon, kw("Int"), rp, op("ASSIGN"), a,
          op("PLUS"), b], "Function with two parameter lists.", "5.10 PARAMLISTS"),
        ("interpolateds/ordinary_string_dollar", '"$x ${y}"', [t("STRING", "$x ${y}")],
         "Without the s prefix, '$' and braces are ordinary string content.", "5.6 INTERPOLATEDS"),
    ]:
        add(name, source, expected, reason, f"IFJ26 S §{section}; {EXTENSIONS}")

    begin, end, obrace, cbrace = t("INTERP_BEGIN"), t("INTERP_END"), t("OPEN_BRACE"), t("CLOSE_BRACE")
    s, name_id = (lambda value: t("STRING", value)), t("IDENTIFIER", "name")
    for name, source, expected in [
        ("plain", 's"abc"', [begin, s("abc"), end]),
        ("empty", 's""', [begin, s(""), end]),
        ("identifier", 's"a$x b"', [begin, s("a"), x, s(" b"), end]),
        ("identifier_boundary", 's"$name."', [begin, s(""), name_id, s("."), end]),
        ("expression", 's"${n + 1}"', [begin, s(""), obrace, n, op("PLUS"), t("INT", 1), cbrace, s(""), end]),
        ("spec_example", 's"$name${n + 1} costs $$0"',
         [begin, s(""), name_id, s(""), obrace, n, op("PLUS"), t("INT", 1), cbrace, s(" costs $0"), end]),
        ("dollar_dollar", 's"$$"', [begin, s("$"), end]),
        ("escapes", 's"\\"q\\"\\t"', [begin, s('"q"\t'), end]),
        ("nested_string", 's"${f("}")}"', [begin, s(""), obrace, f, lp, s("}"), rp, cbrace, s(""), end]),
        ("multiline", 's"""He said "hello", $name.\nSecond line."""',
         [begin, s('He said "hello", '), name_id, s(".\nSecond line."), end]),
        ("then_tokens", 's"a" + b', [begin, s("a"), end, op("PLUS"), b]),
    ]:
        add("interpolateds/" + name, source, expected,
            "Interpolated string split into chunks and splices; '$$' is a literal dollar.", INTERP_CONVENTION)
    for name, source, prefix in [
        ("unterminated", 's"abc', [begin]), ("lone_dollar", 's"$"', [begin]), ("dollar_digit", 's"$1"', [begin]),
        ("dollar_space", 's"$ x"', [begin]), ("physical_lf", 's"a\nb"', [begin]), ("escape", 's"\\q"', [begin]),
        ("unclosed_splice", 's"${x', [begin, s(""), obrace, x]),
    ]:
        add("interpolateds/invalid_" + name, source, prefix, error=1, reference=INTERP_CONVENTION,
            reason="Unterminated interpolation, '$' without id/{/$, LF in a one-line string and bad escapes are "
                   "lexical error 1; a chunk is returned only after its splice or closing quote is reached.")

    prolog_tokens = [kw("import"), t("IDENTIFIER", "ifj26"), t("DOT"), t("OPERATOR", "OP_MULTIPLY"), t("EOL")]
    add("integration/minimal_program", "import ifj26.*\n@main def main(): Unit =\n  ()\n", prolog_tokens + [
        t("ANNOTATION"), kw("def"), t("IDENTIFIER", "main"), t("OPEN_PAREN"), t("CLOSE_PAREN"),
        t("COLON"), kw("Unit"), t("OPERATOR", "OP_ASSIGN"), t("EOL"),
        t("INDENT"), t("OPEN_PAREN"), t("CLOSE_PAREN"), t("EOL"), t("DEDENT")],
        "Scan a minimal complete base program, including the indented body's INDENT/DEDENT pair.")
    add("integration/annotation_own_line_program", "import ifj26.*\n@main\ndef run(): Unit =\n  ()\n", prolog_tokens + [
        t("ANNOTATION"), t("EOL"), kw("def"), t("IDENTIFIER", "run"), t("OPEN_PAREN"), t("CLOSE_PAREN"),
        t("COLON"), kw("Unit"), t("OPERATOR", "OP_ASSIGN"), t("EOL"),
        t("INDENT"), t("OPEN_PAREN"), t("CLOSE_PAREN"), t("EOL"), t("DEDENT")],
        "S §2 allows @main on its own line before def; the entry function need not be named main.",
        "IFJ26 S §2 (p. 4)")
    program_header = "import ifj26.*\n@main def main(): Unit =\n"
    header_tokens = prolog_tokens + [
        t("ANNOTATION"), kw("def"), t("IDENTIFIER", "main"), t("OPEN_PAREN"), t("CLOSE_PAREN"),
        t("COLON"), kw("Unit"), t("OPERATOR", "OP_ASSIGN"), t("EOL"), t("INDENT")]
    add("integration/numbers_program", program_header +
        "  val whole: Int = 1_000\n  val fraction: Double = .5D\n  val exponent: Double = 1e00\n"
        "  print(whole)\n  print(fraction)\n  print(exponent)\n", header_tokens + [
            kw("val"), t("IDENTIFIER", "whole"), t("COLON"), kw("Int"), t("OPERATOR", "OP_ASSIGN"), t("INT", 1000), t("EOL"),
            kw("val"), t("IDENTIFIER", "fraction"), t("COLON"), kw("Double"), t("OPERATOR", "OP_ASSIGN"), t("DOUBLE", .5), t("EOL"),
            kw("val"), t("IDENTIFIER", "exponent"), t("COLON"), kw("Double"), t("OPERATOR", "OP_ASSIGN"), t("DOUBLE", 1.0), t("EOL"),
            t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "whole"), t("CLOSE_PAREN"), t("EOL"),
            t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "fraction"), t("CLOSE_PAREN"), t("EOL"),
            t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "exponent"), t("CLOSE_PAREN"), t("EOL"),
            t("DEDENT")],
        "Complete base program: typed declarations, digit separators, Double suffix, all-zero exponent and calls.")
    add("integration/strings_comments_program", program_header +
        '  val text = "Hello\\nworld\\t!" // escaped text\n'
        '  val raw = """first\n  second\\n"""\n'
        '  /* outer /* nested */ comment */ print(text)\n  print(raw)\n', header_tokens + [
            kw("val"), t("IDENTIFIER", "text"), t("OPERATOR", "OP_ASSIGN"), t("STRING", "Hello\nworld\t!"), t("EOL"),
            kw("val"), t("IDENTIFIER", "raw"), t("OPERATOR", "OP_ASSIGN"), t("STRING", "first\n  second\\n"), t("EOL"),
            t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "text"), t("CLOSE_PAREN"), t("EOL"),
            t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "raw"), t("CLOSE_PAREN"), t("EOL"),
            t("DEDENT")],
        "Complete base program: ordinary escapes, literal multiline content, line comments and nested block comments.")
    add("integration/control_flow_program", program_header +
        "  var count: Int = 2\n  while count > 0 do\n    if count == 1 then\n"
        '      print("last")\n    else\n      print(count)\n    count = count - 1\n', header_tokens + [
            kw("var"), t("IDENTIFIER", "count"), t("COLON"), kw("Int"), t("OPERATOR", "OP_ASSIGN"), t("INT", 2), t("EOL"),
            kw("while"), t("IDENTIFIER", "count"), t("OPERATOR", "OP_GREATER"), t("INT", 0), kw("do"), t("EOL"),
            t("INDENT"), kw("if"), t("IDENTIFIER", "count"), t("OPERATOR", "OP_EQUAL"), t("INT", 1), kw("then"),
            t("EOL"),
            t("INDENT"), t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("STRING", "last"), t("CLOSE_PAREN"), t("EOL"),
            t("DEDENT"), kw("else"), t("EOL"),
            t("INDENT"), t("IDENTIFIER", "print"), t("OPEN_PAREN"), t("IDENTIFIER", "count"), t("CLOSE_PAREN"), t("EOL"),
            t("DEDENT"), t("IDENTIFIER", "count"), t("OPERATOR", "OP_ASSIGN"), t("IDENTIFIER", "count"),
            t("OPERATOR", "OP_MINUS"), t("INT", 1), t("EOL"),
            t("DEDENT"), t("DEDENT")],
        "Complete base program: mutable variable, nested while/if/else, comparisons, subtraction and calls.")
    for name, source, expected in [("empty", "", []), ("identifier", "alpha", [t("IDENTIFIER", "alpha")]),
                                    ("number", "12", [t("INT", 12)]), ("operator", "<=", [t("OPERATOR", "OP_EQUAL_LESS")]),
                                    ("string", '"x"', [t("STRING", "x")]),
                                    ("annotation", "@main", [t("ANNOTATION")]),
                                    ("comment", "/*x*/y", [t("IDENTIFIER", "y")])]:
        add("stdin/pipe_" + name, source, expected,
            "Compiler input is stdin: a non-seekable pipe must produce the same tokens as a regular file.",
            "Assignment Z: stdin input requirement; src/lexer.h FILE *input", pipe=True)
    return result


def example_path(case):
    """Use stable, portable names, including for punctuation and case-sensitive fixtures."""
    category, name = case.name.split("/", 1)
    if category == "integration":
        return Path("programs") / (name + ".ifj")
    slug = re.sub(r"[^a-z0-9_]+", "_", name.lower()).strip("_") or "input"
    digest = hashlib.sha256(case.name.encode("utf-8")).hexdigest()[:8]
    return Path("fragments") / category / f"{slug}_{digest}.ifj"


def export_examples(fixtures):
    """Export exact bytes without adding comments/newlines that could change a lexer test."""
    directory = EXAMPLES
    directory.mkdir(parents=True, exist_ok=True)
    entries = ["| File | Test case | Expected lexer error | Purpose |",
               "| --- | --- | --- | --- |"]
    count = 0
    for case in fixtures:
        if case.api:
            continue
        relative = example_path(case)
        destination = directory / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() and destination.read_bytes() != case.source:
            raise ValueError(f"Refusing to overwrite an edited example: {destination}")
        destination.write_bytes(case.source)
        reason = case.reason.replace("|", "&#124;")
        entries.append(f"| [{relative.as_posix()}]({relative.as_posix()}) | `{case.name}` | {case.error} | {reason} |")
        count += 1
    introduction = """# IFJ26 inputs used by the lexer tests

`programs/` contains complete base-language programs with `import ifj26.*` and
`@main`: a minimal program, `@main` on its own line, numeric declarations,
strings/comments, and control flow. All five are included in the lexer suite. Their parser, semantic and runtime behavior
has not been tested: this repository's test harness exercises the lexer only.

`fragments/` contains every other source-input fixture, organized by test category.
These are **lexer fragments, not complete programs**. Some are deliberately invalid.
Expected error `0` means successful lexing; `1` means a required lexical error.
These expectations are not current pass/fail results; see `../../../test.output` for those.

The files preserve the exact bytes fed to the lexer. In particular, missing final
newlines, tabs, empty files, NUL bytes, escapes and long tokens are intentional.
No explanatory comments were inserted into the inputs. C API-only cases such as
NULL constructor arguments have no source program and are not exported.

Find the case name in the table below, then run it from the repository root:

```sh
python3 tests/lexer/run.py --case integration/numbers_program
python3 tests/lexer/run.py --case integration/
make -C src test-lexer-all
```

The runner uses the fixtures in `tests/lexer/run.py`; these files are exact snapshots
for inspection and manual use. Editing a snapshot does not change the automated
test. To export missing examples again, run `python3 tests/lexer/run.py --export-examples`.
Export refuses to replace edited source files; move an edited copy aside first if
you want to restore the original fixture. Each report case links its snapshot when
the file is present and still matches the tested bytes.

"""
    (directory / "README.md").write_text(introduction + "\n".join(entries) + "\n", encoding="utf-8")
    print(f"Exported {count} exact source inputs to {directory}")


def decode_output(output):
    """Reject malformed adapter output instead of silently discarding it."""
    tokens = []
    error = 0
    error_kind = None
    counter = None
    for row in output.splitlines():
        fields = row.split("\t")
        if fields[0] == "TOKEN" and len(fields) == 5:
            _, kind, line, index, raw = fields
            int(index)
            value = None
            if kind in ("TOKEN_STRING", "TOKEN_IDENTIFIER"):
                value = bytes.fromhex(raw).decode("ascii")
            elif kind in ("TOKEN_INT", "TOKEN_BOOLEAN"):
                value = int(raw)
            elif kind == "TOKEN_DOUBLE":
                value = float.fromhex(raw)
            elif kind in ("TOKEN_OPERATOR", "TOKEN_KEYWORD"):
                value = raw
            tokens.append(Token(kind, value, int(line)))
        elif fields[0] == "ERROR" and len(fields) == 4:
            if error:
                raise ValueError("duplicate error record")
            error, error_kind = int(fields[1]), fields[2]
        elif fields[0] == "COUNTER" and len(fields) == 2 and counter is None:
            counter = int(fields[1])
        else:
            raise ValueError("unexpected probe record: " + row)
    if counter is None:
        raise ValueError("missing final line counter")
    return tokens, error, error_kind, counter


def describe(token):
    """Readable token expectations, with escaped literal data and optional source line."""
    value = "" if token.value is None else " value=" + repr(token.value)
    line = "" if token.line is None else f" line={token.line}"
    return token.kind + value + line


def compare(case, output):
    """Exact sequence, value, error and selected source-position comparisons."""
    actual, error, error_kind, counter = decode_output(output)
    issues = []
    if error != case.error:
        issues.append(f"error code: expected {case.error}, actual {error}")
    if error and error_kind != "TOKEN_UNDEFINED":
        issues.append(f"error token: expected TOKEN_UNDEFINED, actual {error_kind}")
    if len(actual) != len(case.expected):
        issues.append(f"token count: expected {len(case.expected)}, actual {len(actual)}")
    for position, (expected, observed) in enumerate(zip(case.expected, actual), 1):
        if expected.kind != observed.kind or expected.value != observed.value or (
            expected.line is not None and expected.line != observed.line
        ) or (isinstance(expected.value, float) and (
            not math.isfinite(observed.value) or expected.value.hex() != observed.value.hex()
        )):
            issues.append(f"token #{position}: required {describe(expected)}; lexer returned {describe(observed)}")
            if (expected.kind == "TOKEN_KEYWORD" and observed.kind == "TOKEN_IDENTIFIER"
                    and observed.value in BASE_KEYWORDS):
                issues.append(f"'{observed.value}' IS a reserved keyword under IFJ26 S §1.1. "
                              "The failure is the lexer's IDENTIFIER classification; the test requires KEYWORD.")
    expected_counter = 1 + case.source.count(b"\n")
    if not case.error and counter != expected_counter:
        issues.append(f"physical line counter: expected {expected_counter}, actual {counter}")
    return issues, actual


def limit_probe_stack():
    """Make raises RLIMIT_STACK on some hosts; give each fixture the same 8 MiB budget."""
    _, hard = resource.getrlimit(resource.RLIMIT_STACK)
    limit = STACK_LIMIT if hard == resource.RLIM_INFINITY else min(STACK_LIMIT, hard)
    resource.setrlimit(resource.RLIMIT_STACK, (limit, hard))


def run_case(binary, case, directory, timeout, environment):
    """A crash or timeout fails only this fixture; every subsequent case still runs."""
    fixture = directory / "input.ifj"
    fixture.write_bytes(case.source)
    args = [str(binary), "--api", case.api] if case.api else [str(binary), "--pipe" if case.pipe else str(fixture)]
    try:
        process = subprocess.run(args, input=case.source if case.pipe else None, capture_output=True,
                                 timeout=timeout, env=environment, preexec_fn=limit_probe_stack)
    except subprocess.TimeoutExpired:
        return [f"timeout after {timeout:g}s (possible nontermination or excessive recursion)"], "", []
    stdout = process.stdout.decode("utf-8", errors="replace")
    stderr = process.stderr.decode("utf-8", errors="replace")
    if process.returncode:
        return [f"probe exited with status {process.returncode}; expected 0"], stdout + stderr, []
    if case.api:
        expected = f"API\t{case.api}\tPASS\t"
        return ([] if stdout.startswith(expected) and not stderr else ["API assertion failed or unexpected stderr"]), stdout + stderr, []
    try:
        issues, actual = compare(case, stdout)
    except (ValueError, TypeError) as exc:
        issues, actual = ["invalid probe output: " + str(exc)], []
    if stderr:
        issues.append("unexpected stderr (including any sanitizer diagnostics)")
    return issues, stdout + stderr, actual


def main():
    """Compile in a temporary directory and always write build/results diagnostics to the report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", default="", help="run only names containing this text")
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UndefinedBehaviorSanitizer")
    parser.add_argument("--all-modes", action="store_true", help="combine ordinary and sanitizer runs in one report")
    parser.add_argument("--fails-only", action="store_true", help="omit passing case details; still run all selected cases and report totals")
    parser.add_argument("--export-examples", action="store_true", help="export source fixtures to tests/lexer/exmaples_to_test and exit")
    parser.add_argument("--output", type=Path, default=ROOT / "test.output")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-case timeout in seconds (default: 5)")
    options = parser.parse_args()
    if options.timeout <= 0:
        parser.error("--timeout must be positive")
    if options.sanitize and options.all_modes:
        parser.error("choose either --sanitize or --all-modes")
    selected = [case for case in cases() if options.case in case.name]
    if len({case.name for case in selected}) != len(selected):
        parser.error("test case names must be unique")
    if not selected:
        parser.error("no cases match --case")
    if options.export_examples:
        if options.case or options.sanitize or options.all_modes or options.fails_only:
            parser.error("--export-examples exports all cases; do not combine it with filters or run modes")
        try:
            export_examples(selected)
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        return 0
    options.output.parent.mkdir(parents=True, exist_ok=True)
    if options.all_modes:
        with tempfile.TemporaryDirectory(prefix="ifj_lexer_reports_") as temp:
            reports = []
            statuses = []
            for sanitize in (False, True):
                path = Path(temp) / ("sanitized.output" if sanitize else "ordinary.output")
                command = [sys.executable, str(Path(__file__).resolve()), "--case", options.case,
                           "--timeout", str(options.timeout), "--output", str(path)]
                if sanitize:
                    command.append("--sanitize")
                if options.fails_only:
                    command.append("--fails-only")
                process = subprocess.run(command, capture_output=True, text=True)
                statuses.append(process.returncode)
                content = path.read_text(encoding="utf-8") if path.exists() else process.stdout + process.stderr
                reports.append(content)
                mode = "ASan + UBSan" if sanitize else "Ordinary"
                totals = next((row for row in content.splitlines() if row.startswith("TOTAL:")), "BUILD/SETUP FAILED")
                print(f"{mode}: {totals}", flush=True)
            with options.output.open("w", encoding="utf-8") as combined:
                combined.write("IFJ26 LEXER TESTS — ORDINARY AND SANITIZER RUNS\n")
                for mode, content in zip(("Ordinary", "ASan + UBSan"), reports):
                    totals = next((row for row in content.splitlines() if row.startswith("TOTAL:")), "BUILD/SETUP FAILED")
                    combined.write(f"{mode}: {totals}\n")
                for content in reports:
                    combined.write("\n" + "=" * 80 + "\n\n" + content)
            print(f"Report: {options.output}")
            return 2 if any(status not in (0, 1) for status in statuses) else int(any(statuses))
    with options.output.open("w", encoding="utf-8") as report, tempfile.TemporaryDirectory(prefix="ifj_lexer_") as temp:
        directory = Path(temp)
        binary = directory / "lexer_probe"
        command = shlex.split(os.environ.get("CC", "cc")) + ["-std=gnu11", "-Wall", "-Wextra", "-g", "-O0"]
        if options.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
        command += ["-I", str(ROOT / "src"), str(ROOT / "tests/lexer/probe.c")]
        command += [str(ROOT / "src" / name) for name in ("lexer.c", "attribute_table.c", "stack.c", "utils.c", "error.c")]
        command += ["-lm", "-o", str(binary)]
        report.write("IFJ26 STRICT LEXER TEST REPORT\n")
        report.write(f"Mode: {'ASan + UBSan' if options.sanitize else 'ordinary'}; selected cases: {len(selected)}\n")
        if options.fails_only:
            report.write("Report details: failed cases only. All selected cases ran; totals include passing cases.\n")
        report.write("Expected behavior comes from the supplied assignment, project clarifications and stated raw scanner/API conventions.\n")
        report.write("HOW TO READ: required/expected = test requirement; lexer returned/actual = output from lexer.c.\n")
        report.write("Example: required KEYWORD, lexer returned IDENTIFIER 'def' means the lexer misclassified a keyword.\n")
        report.write("Base keywords (S §1.1): " + " ".join(BASE_KEYWORDS) + "\n")
        report.write("API/convention cases are labeled separately from assignment language rules in their Reference fields.\n")
        report.write("No XFAIL, ignored failures, numeric tolerances, or retries. Each fixture uses a fresh process.\n")
        _, hard_stack = resource.getrlimit(resource.RLIMIT_STACK)
        stack = STACK_LIMIT if hard_stack == resource.RLIM_INFINITY else min(STACK_LIMIT, hard_stack)
        report.write(f"Probe stack limit: {stack} bytes (8 MiB unless the OS hard limit is lower).\n")
        report.write("Successful scans must reach EOF twice; all token values and consumed physical LFs are checked.\n")
        report.write("Positions use 1-based token START lines when explicitly asserted.\n")
        report.write("Scope: scanner incl. INDENT/DEDENT layout (S §1.2), not parser. Trailing-comma legality and\n")
        report.write("malformed program syntax need parser integration tests.\n")
        report.write("Unselected extensions, unspecified annotation boundaries/numeric suffix tokenization, overflow,\n")
        report.write("allocation-failure injection and exhaustive unbounded inputs are not claimed as covered.\n")
        report.write("Build: " + shlex.join(command) + "\n\n")
        try:
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired) as exc:
            report.write(f"BUILD ERROR: {exc}\n")
            print(f"Build failed; see {options.output}")
            return 2
        report.write("BUILD " + ("PASS" if build.returncode == 0 else "FAIL") + "\n" + build.stdout + build.stderr + "\n")
        if build.returncode:
            print(f"Build failed; see {options.output}")
            return 2
        environment = os.environ.copy()
        environment["LC_ALL"] = "C"
        if options.sanitize:
            environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            report.write("LeakSanitizer: " + ("unavailable on macOS; disabled" if sys.platform == "darwin" else "enabled") + "\n\n")
        failed_names = []
        for number, case in enumerate(selected, 1):
            issues, raw, actual = run_case(binary, case, directory, options.timeout, environment)
            if issues:
                failed_names.append(case.name)
            elif options.fails_only:
                continue
            report.write(f"[{number:03d}] {'FAIL' if issues else 'PASS'} {case.name}\n")
            report.write(f"  Requirement: {case.reason}\n  Reference: {case.reference}\n")
            if not case.api:
                example = EXAMPLES / example_path(case)
                if example.is_file() and example.read_bytes() == case.source:
                    report.write(f"  Input file: {example.relative_to(ROOT)}\n")
            report.write(f"  Input bytes ({len(case.source)}): {case.source!r}\n")
            report.write(f"  Expected error: {case.error}; expected tokens: {len(case.expected)}\n")
            for issue in issues:
                report.write("  MISMATCH: " + issue + "\n")
            if issues:
                report.write("  Required sequence (test expectations):\n")
                for index, token in enumerate(case.expected, 1):
                    report.write(f"    {index}: {describe(token)}\n")
                report.write("  Lexer returned (observed output, not the language rules):\n")
                for index, token in enumerate(actual, 1):
                    report.write(f"    {index}: {describe(token)}\n")
                report.write("  Probe trace / crash / sanitizer details:\n")
                report.write("\n".join("    " + line for line in raw.splitlines()) + "\n")
            report.write("\n")
            report.flush()
        failed = len(failed_names)
        summary = f"TOTAL: {len(selected)} | PASS: {len(selected) - failed} | FAIL: {failed}"
        report.write(summary + "\n")
        # Keep team decisions and API contracts apart from proven assignment violations.
        sources = {}
        for case in selected:
            total, fails = sources.get(category(case), (0, 0))
            sources[category(case)] = (total + 1, fails + (case.name in failed_names))
        report.write("BY SOURCE: " + " | ".join(f"{name}: {total - fails}/{total} pass"
                                                for name, (total, fails) in sorted(sources.items())) + "\n")
        if failed_names:
            report.write("Failed cases (search their names above for full details):\n")
            report.write("".join("  " + name + "\n" for name in failed_names))
        print(summary)
        print(f"Report: {options.output}")
        return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
