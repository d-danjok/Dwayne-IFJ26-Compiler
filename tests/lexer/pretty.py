#!/usr/bin/env python3
"""Pretty-print an IFJ26 lexer test report (test.output) in the terminal, with colors."""

import argparse
from dataclasses import dataclass, field
import difflib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import textwrap


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tests" / "lexer" / "run.py"

CASE_HEADER = re.compile(r"^\[(\d+)\] (PASS|FAIL) (\S+)$")
MODE_LINE = re.compile(r"^Mode: (.+); selected cases: (\d+)$")
TOTAL_LINE = re.compile(r"^TOTAL: (\d+) \| PASS: (\d+) \| FAIL: (\d+)$")
SEQUENCE_ROW = re.compile(r"^\d+: (.*)$")
TOKEN_LINE = re.compile(r"^(.*?)(?: line=(\d+))?$")
DIAGNOSTIC = re.compile(r"^(\S+?(?::\d+)*): (warning|error|fatal error): (.*)$")
MISMATCH_LIMIT = 6
SECTIONS = {"  Required sequence": "expected", "  Lexer returned": "actual", "  Probe trace": "trace"}
PROTOCOL = ("TOKEN\t", "ERROR\t", "COUNTER\t")
GLYPHS = {
    True: dict(ok="✔", fail="✘", warn="⚠", bullet="•", full="█", empty="░", rule="─", more="…", bar="│"),
    False: dict(ok="+", fail="x", warn="!", bullet="*", full="#", empty=".", rule="-", more="~", bar="|"),
}


@dataclass
class Case:
    number: int
    passed: bool
    name: str
    fields: dict = field(default_factory=dict)
    mismatches: list = field(default_factory=list)
    expected: list = field(default_factory=list)
    actual: list = field(default_factory=list)
    trace: list = field(default_factory=list)


@dataclass
class Run:
    mode: str = "?"
    selected: int = 0
    fails_only: bool = False
    build: str = ""
    build_output: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    cases: list = field(default_factory=list)
    total: tuple | None = None
    failed_names: list = field(default_factory=list)


def parse(text):
    """Split a (possibly combined --all-modes) report into one Run per mode."""
    runs = []
    run = case = section = state = None
    for line in text.splitlines():
        if line == "IFJ26 STRICT LEXER TEST REPORT":
            run = Run()
            runs.append(run)
            case = section = state = None
            continue
        if run is None:
            continue
        if case is not None and section and line.startswith("    "):
            getattr(case, section).append(line[4:])
            continue
        header = CASE_HEADER.match(line)
        if header:
            case = Case(int(header[1]), header[2] == "PASS", header[3])
            run.cases.append(case)
            section, state = None, "case"
            continue
        if not line.strip():
            section = None
            continue
        if state == "case" and line.startswith("  "):
            if line.startswith("  MISMATCH: "):
                case.mismatches.append(line[len("  MISMATCH: "):])
                continue
            section = next((name for prefix, name in SECTIONS.items() if line.startswith(prefix)), None)
            if section is None:
                key, _, value = line[2:].partition(": ")
                case.fields[key] = value
            continue
        if state == "failed" and line.startswith("  "):
            run.failed_names.append(line.strip())
            continue
        mode = MODE_LINE.match(line)
        total = TOTAL_LINE.match(line)
        if mode:
            run.mode, run.selected = mode[1], int(mode[2])
        elif line.startswith("Report details: failed cases only"):
            run.fails_only = True
        elif line in ("BUILD PASS", "BUILD FAIL"):
            run.build, state = line[6:], "build"
        elif line.startswith("BUILD ERROR"):
            run.build, state = "ERROR", None
            run.build_output.append(line.partition(": ")[2])
        elif total:
            run.total, state, case = tuple(map(int, total.groups())), None, None
        elif line.startswith("Failed cases"):
            state = "failed"
        elif line.startswith("LeakSanitizer: "):
            run.notes.append(line)
            state = None
        elif state == "build" and not line.startswith("====="):
            run.build_output.append(line)
    return runs


def split_token(description):
    """'TOKEN_INT value=1 line=2' -> ('TOKEN_INT value=1', '2')."""
    match = TOKEN_LINE.match(description)
    return match[1], match[2]


def same_token(expected, actual):
    """Mirror run.py: kind and value must match; the line only when the test asserts one."""
    (expected_body, expected_line), (actual_body, actual_line) = split_token(expected), split_token(actual)
    return expected_body == actual_body and (expected_line is None or expected_line == actual_line)


def short_token(description):
    body, line = split_token(description)
    body = body.removeprefix("TOKEN_").replace(" value=", " ")
    return body + (f" @{line}" if line else "")


def sequence(rows):
    return [match[1] if (match := SEQUENCE_ROW.match(row)) else row for row in rows]


def group_of(name):
    return name.split("/", 1)[0]


class Palette:
    CODES = dict(bold="1", dim="2", red="31", green="32", yellow="33", blue="34", magenta="35", cyan="36")

    def __init__(self, enabled):
        self.enabled = enabled

    def __call__(self, text, *styles):
        if not self.enabled or not styles or not text:
            return text
        return "\033[" + ";".join(self.CODES[style] for style in styles) + "m" + text + "\033[0m"


class Renderer:
    def __init__(self, options, color, unicode):
        self.p = Palette(color)
        self.g = GLYPHS[unicode]
        self.verbose = options.verbose
        self.quiet = options.quiet
        self.filter = options.filter
        self.width = max(60, min(160, shutil.get_terminal_size((100, 24)).columns))

    def fit(self, text, width):
        return text[:width - 1] + self.g["more"] if len(text) > width else text.ljust(width)

    def rule(self, title, color):
        text = f"{self.g['rule'] * 2} {title} "
        print(self.p(text + self.g["rule"] * max(0, self.width - len(text)), "bold", color))

    def bar(self, passed, total, width):
        filled = round(width * passed / total) if total else 0
        return self.p(self.g["full"] * filled, "green") + self.p(self.g["empty"] * (width - filled), "red")

    def names(self, names, color, indent=4):
        grouped = {}
        for name in names:
            grouped.setdefault(group_of(name), []).append(name.partition("/")[2] or name)
        label_width = max(map(len, grouped), default=0)
        for group, items in grouped.items():
            chunks = textwrap.wrap(" ".join(items), self.width - indent - label_width - 2,
                                   break_on_hyphens=False, break_long_words=False) or [""]
            for index, chunk in enumerate(chunks):
                label = (group if index == 0 else "").ljust(label_width)
                print(" " * indent + self.p(label, "bold") + "  " + self.p(chunk, color))

    def render(self, runs):
        previous = {}
        for index, run in enumerate(runs):
            if index:
                print()
            self.run(run, previous, runs[0].mode if index else None)
            for case in run.cases:
                previous.setdefault(case.name, (case.mismatches, case.actual))
        if len(runs) > 1:
            print()
            self.rule("Overall", "magenta")
            for run in runs:
                self.totals(run, label=run.mode)

    def run(self, run, previous, first_mode):
        tags = [f"{run.selected} cases"] + (["failures-only report"] if run.fails_only else [])
        tags += [f"filter '{self.filter}'"] if self.filter else []
        self.rule(f"IFJ26 lexer {self.g['bullet']} {run.mode} {self.g['bullet']} " + ", ".join(tags), "cyan")
        self.build(run)
        if run.build != "PASS":
            return
        for note in run.notes:
            print("  " + self.p(note, "dim"))
        cases = [case for case in run.cases if self.filter in case.name]
        failed_names = [name for name in run.failed_names if self.filter in name]
        failures = [case for case in cases if not case.passed]
        if not self.quiet:
            self.groups(run, cases, failed_names)
        if failed_names and self.quiet:
            print()
            print("  " + self.p(f"Failed ({len(failed_names)})", "bold", "red"))
            self.names(failed_names, "red")
        elif failures:
            repeated = [case.name for case in failures if not self.verbose and previous.get(case.name) ==
                        (case.mismatches, case.actual)]
            fresh = [case for case in failures if case.name not in repeated]
            print()
            print("  " + self.p(f"Failures ({len(failures)})", "bold", "red"))
            for case in fresh:
                self.failure(case)
            if repeated:
                print()
                print("  " + self.p(f"{len(repeated)} failure(s) identical to the {first_mode} run "
                                    "(use -v to repeat details):", "dim"))
                self.names(repeated, "red")
        print()
        self.totals(run)

    def build(self, run):
        diagnostics = list(dict.fromkeys(match.groups() for line in run.build_output
                                         if (match := DIAGNOSTIC.match(line))))
        warnings = sum(kind == "warning" for _, kind, _ in diagnostics)
        errors = len(diagnostics) - warnings
        status = {"PASS": self.p(f"{self.g['ok']} PASS", "bold", "green"),
                  "FAIL": self.p(f"{self.g['fail']} FAIL", "bold", "red"),
                  "ERROR": self.p(f"{self.g['fail']} ERROR", "bold", "red")}.get(
                      run.build, self.p("? report ends before the build result", "yellow"))
        counts = [self.p(f"{errors} error(s)", "red")] if errors else []
        counts += [self.p(f"{warnings} warning(s)", "yellow")] if warnings else []
        print(f"  {self.p('Build', 'bold')}  {status}  " + ", ".join(counts))
        for location, kind, text in diagnostics:
            glyph, color = (self.g["warn"], "yellow") if kind == "warning" else (self.g["fail"], "red")
            print(f"    {self.p(glyph, color)} {self.p(location.removeprefix(str(ROOT) + '/'), 'bold')} {text}")
        if self.verbose or (run.build != "PASS" and not diagnostics):
            for line in run.build_output:
                print("      " + self.p(line, "dim"))

    def groups(self, run, cases, failed_names):
        stats = {}
        if run.fails_only:
            for name in failed_names:
                stats.setdefault(group_of(name), [None, 0])[1] += 1
        else:
            for case in cases:
                stats.setdefault(group_of(case.name), [0, 0])[0 if case.passed else 1] += 1
        if not stats:
            return
        print()
        print("  " + self.p("Groups", "bold") + ("" if not run.fails_only else
                                                  self.p("  (pass counts unavailable in a failures-only report)", "dim")))
        name_width = max(map(len, stats))
        for name, (passed, failed) in stats.items():
            if passed is None:
                print(f"    {name.ljust(name_width)}  " + self.p(f"{self.g['fail']} {failed} failed", "red"))
                continue
            total = passed + failed
            ratio = f"{passed}/{total}".rjust(7)
            mark = self.p(self.g["ok"], "green") if not failed else self.p(f"{self.g['fail']} {failed} failed", "red")
            label = self.p(name.ljust(name_width), "dim" if not failed else "bold")
            print(f"    {label}  {ratio}  {self.bar(passed, total, 20)}  {mark}")

    def failure(self, case):
        p, g = self.p, self.g
        print()
        print(f"  {p(g['fail'], 'red', 'bold')} {p(case.name, 'bold')}  {p(f'[{case.number:03d}]', 'dim')}")
        source = next((value for key, value in case.fields.items() if key.startswith("Input bytes")), "")
        if self.verbose:
            for key, value in case.fields.items():
                print(f"      {p(key + ':', 'dim')} {value}")
        else:
            requirement = case.fields.get("Requirement", "")
            if requirement:
                print("      " + p(self.fit(requirement, self.width - 6).rstrip(), "dim"))
            location = case.fields.get("Input file", "")
            print(f"      {p('input', 'cyan')}  " + self.fit(source, self.width - 13 - len(location)).rstrip() +
                  ("  " + p(location, "dim") if location else ""))
        limit = len(case.mismatches) if self.verbose else MISMATCH_LIMIT
        for issue in case.mismatches[:limit]:
            print(f"      {p(g['bullet'], 'yellow')} {self.issue(issue)}")
        if len(case.mismatches) > limit:
            print("        " + p(f"{g['more']} {len(case.mismatches) - limit} more mismatch(es); "
                                    "the aligned table below shows where the sequences diverge (-v lists all)", "dim"))
        self.table(sequence(case.expected), sequence(case.actual))
        extra = [line for line in case.trace if not line.startswith(PROTOCOL)]
        shown = case.trace if self.verbose or not (case.expected or case.actual) else extra
        if shown:
            print("      " + p("probe trace / sanitizer:", "magenta"))
            limit = len(shown) if self.verbose else 25
            for line in shown[:limit]:
                print("        " + p(line.replace("\t", "  "), "dim"))
            if len(shown) > limit:
                print("        " + p(f"{g['more']} {len(shown) - limit} more line(s); use -v", "dim"))

    def issue(self, text):
        p = self.p
        match = re.match(r"(token #\d+: required )(.*)(; lexer returned )(.*)$", text) or \
            re.match(r"(.*?: expected )(.*)(, actual )(.*)$", text)
        if match:
            return p(match[1], "yellow") + p(match[2], "green") + p(match[3], "yellow") + p(match[4], "red")
        return p(text, "yellow")

    def table(self, expected, actual):
        """Aligned expected/actual token columns; + extra token, - missing token, ~ changed token."""
        if not expected and not actual:
            return
        p, g = self.p, self.g
        keys = lambda rows: [split_token(row)[0] for row in rows]
        rows = []
        for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, keys(expected), keys(actual),
                                                           autojunk=False).get_opcodes():
            for k in range(max(i2 - i1, j2 - j1)):
                e = i1 + k if i1 + k < i2 else None
                a = j1 + k if j1 + k < j2 else None
                left = expected[e] if e is not None else None
                right = actual[a] if a is not None else None
                mark = "-" if right is None else "+" if left is None else "=" if same_token(left, right) else "~"
                rows.append((mark, e, left, a, right))
        digits = len(str(max(len(expected), len(actual))))
        column = min(48, max(14, (self.width - 12 - 2 * digits) // 2))
        changed = [index for index, row in enumerate(rows) if row[0] != "="]
        keep = set(range(len(rows))) if self.verbose else {j for i in changed for j in (i - 1, i, i + 1)}
        number = lambda n: str(n + 1).rjust(digits) if n is not None else " " * digits
        print(p(f"        {' ' * digits} {self.fit('expected', column)}   {' ' * digits} actual", "dim"))
        skipped = 0
        for index, (mark, e, left, a, right) in enumerate(rows):
            if index not in keep:
                skipped += 1
                continue
            if skipped:
                print(p(f"        {g['more']} {skipped} matching token(s)", "dim"))
                skipped = 0
            left_text = self.fit(short_token(left) if left else "", column)
            right_text = self.fit(short_token(right) if right else "", column).rstrip()
            if mark == "=":
                print(p(f"      {' '} {number(e)} {left_text} {g['bar']} {number(a)} {right_text}", "dim"))
            else:
                print(f"      {p(mark, 'bold', 'yellow')} {p(number(e), 'dim')} {p(left_text, 'green')} "
                      f"{p(g['bar'], 'dim')} {p(number(a), 'dim')} {p(right_text, 'red')}")
        if skipped:
            print(p(f"        {g['more']} {skipped} matching token(s)", "dim"))

    def totals(self, run, label=None):
        prefix = f"  {self.p(label.ljust(14), 'bold')}" if label else "  "
        if run.build != "PASS" or run.total is None:
            print(prefix + self.p(f"{self.g['fail']} no results (build {run.build or 'missing'})", "bold", "red"))
            return
        total, passed, failed = run.total
        percent = 100 * passed / total if total else 0.0
        verdict = self.p(f"{self.g['ok']} ALL PASS", "bold", "green") if not failed else \
            self.p(f"{self.g['fail']} {failed} failed", "bold", "red")
        print(f"{prefix}{verdict}  {self.p(f'{passed} passed', 'green')}  {total} total  "
              f"{self.p(f'{percent:5.1f}%', 'bold')}  {self.bar(passed, total, 30)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", nargs="?", type=Path, default=ROOT / "test.output",
                        help="report written by run.py (default: test.output in the repository root)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="full token tables, all case fields, probe traces and compiler output")
    parser.add_argument("-q", "--quiet", action="store_true", help="build, failed names and totals only")
    parser.add_argument("-k", "--filter", default="", help="show only cases whose name contains this text")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto")
    runner = parser.add_argument_group("run the tests first (options forwarded to run.py)")
    runner.add_argument("--run", action="store_true", help="run tests/lexer/run.py, then render its report")
    runner.add_argument("--case", help="run only names containing this text")
    runner.add_argument("--sanitize", action="store_true", help="ASan + UBSan build")
    runner.add_argument("--all-modes", action="store_true", help="ordinary and sanitizer runs")
    runner.add_argument("--fails-only", action="store_true", help="keep only failed cases in the report")
    runner.add_argument("--timeout", help="per-case timeout in seconds")
    options = parser.parse_args()
    if options.verbose and options.quiet:
        parser.error("choose either --verbose or --quiet")
    forwarded = [f"--{name}" for name in ("sanitize", "all_modes", "fails_only") if getattr(options, name)]
    forwarded = [flag.replace("_", "-") for flag in forwarded]
    forwarded += [item for name in ("case", "timeout") if getattr(options, name) is not None
                  for item in (f"--{name}", getattr(options, name))]
    if forwarded and not options.run:
        parser.error("run.py options require --run")

    color = options.color == "always" or (options.color == "auto" and sys.stdout.isatty() and
                                          "NO_COLOR" not in os.environ and os.environ.get("TERM") != "dumb")
    unicode = (sys.stdout.encoding or "").lower().replace("-", "").startswith("utf8")
    if options.run:
        print(Palette(color)("Running lexer tests...", "dim"), file=sys.stderr, flush=True)
        process = subprocess.run([sys.executable, str(RUNNER), *forwarded, "--output", str(options.report)],
                                 capture_output=True, text=True)
        if process.returncode not in (0, 1) and not options.report.is_file():
            sys.stderr.write(process.stdout + process.stderr)
            return 2
    try:
        text = options.report.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        parser.error(f"cannot read report: {exc}")
    runs = parse(text)
    if not runs:
        parser.error(f"{options.report} is not a lexer test report")
    Renderer(options, color, unicode).render(runs)
    if any(run.build != "PASS" or run.total is None for run in runs):
        return 2
    return 1 if any(run.total[2] for run in runs) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        sys.exit(1)
