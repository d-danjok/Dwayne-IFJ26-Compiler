#!/usr/bin/env python3
"""Run the TAC/optimizer and program-output suites and pretty-print their results,
optionally with the AST, TAC and generated IFJcode26 of each case (--show)."""

import argparse
import contextlib
import io
import json
from dataclasses import dataclass, field
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run_tests import ROOT, build  # noqa: E402
from run_output_tests import CASES  # noqa: E402
from target_executor import Executor, TargetError, UnsupportedInstruction, UNSET  # noqa: E402

HEADER = re.compile(r"^\[(PASS|FAIL) (\d+)\] (.+)$")
DETAIL = re.compile(r"^  (Program|Example|Input|Expected|Actual|Error|Inspect|Writes|Generated line \d+):\s*(.*)$")
DIAGNOSTIC = re.compile(r"^(\S+?(?::\d+)*): (warning|error|fatal error): (.*)$")
EXPECTED_ACTUAL = re.compile(r"^(.*?expected \S+?=)(.*?)(; actual=)(.*?)( \(.*\))?$")
OUTPUT_GROUPS = {name: group for name, *_, group in CASES} | {"main_after_function": "call"}
GLYPHS = {
    True: dict(ok="✔", fail="✘", warn="⚠", bullet="•", full="█", empty="░", rule="─"),
    False: dict(ok="+", fail="x", warn="!", bullet="*", full="#", empty=".", rule="-"),
}


@dataclass
class Case:
    number: int
    passed: bool
    title: str
    details: list = field(default_factory=list)


@dataclass
class Suite:
    label: str
    runner: str
    flags: list
    group_of: object
    stdin_of: object = lambda case: "7\n"  # run_tests.py feeds readInt() a 7
    build_error: str = ""
    diagnostics: list = field(default_factory=list)
    cases: list = field(default_factory=list)
    returncode: int = 0


def tac_group(title):
    kind, _, rest = title.partition("/")
    base = "random" if rest.startswith("random") else kind
    return base + (" alloc faults" if "allocation #" in title else "")


def output_group(title):
    return OUTPUT_GROUPS.get(title, "other")


def run(suite):
    process = subprocess.run([sys.executable, str(HERE / suite.runner), *suite.flags],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT)
    suite.returncode = process.returncode
    case = None
    for line in process.stdout.splitlines():
        if header := HEADER.match(line):
            case = Case(int(header[2]), header[1] == "PASS", header[3].partition(": ")[0])
            suite.cases.append(case)
        elif diagnostic := DIAGNOSTIC.match(line):
            suite.diagnostics.append(diagnostic.groups())
        elif line.startswith("[BUILD ERROR]"):
            suite.build_error = line.partition("] ")[2]
        elif not line.strip() or line.startswith(("RESULT", "OUTPUT RESULTS")):
            case = None
        elif case and not case.passed:
            detail = DETAIL.match(line)  # anything else is sanitizer / traceback output of this failure
            case.details.append((detail[1], detail[2]) if detail else ("", line.strip()))
    return suite


def tac_argument(argument):
    kind, value_type, value = argument
    if kind == "empty":
        return ""
    if kind == "temp":
        return f"$t{value}"
    if kind == "label":
        return f"L{value}"
    if kind == "literal":
        if value_type == "string":
            return json.dumps(value)
        if value_type == "float":
            return repr(float.fromhex(value)) if value not in ("nan", "inf", "-inf") else value
        if value_type == "bool":
            return "true" if value else "false"
        return "()" if value_type == "unit" else str(value)
    return str(value) if kind in ("id", "func") else f"<{kind}>"


def tac_text(instruction):
    """'LESSER $t2, 1 -> $t3': operands, then the result field of the quadruple."""
    op, *arguments = instruction
    arg1, arg2, result = map(tac_argument, arguments)
    operands = ", ".join(filter(None, (arg1, arg2)))
    return " ".join(filter(None, (op, operands, ("-> " if operands and result else "") + result)))


def tac_listing(instructions):
    return [(f"{index:>3}  {tac_text(instruction)}", None) for index, instruction in enumerate(instructions)]


def value_text(value):
    if value is UNSET:
        return "<uninitialized>"
    kind, data = value
    return "nil" if kind == "nil" else f"{kind} {json.dumps(data) if kind in ('string', 'bool') else data!r}"


class Dumper:
    """Builds the test driver once and asks it for the AST, TAC and IFJcode26 of a fixture."""

    NO_EFFECT = {"LABEL", "CREATEFRAME", "PUSHFRAME", "POPFRAME", "DEFVAR", "CALL", "RETURN", "JUMP", "EXIT",
                 "PUSHS", "CLEARS"}
    TRACE_LIMIT = 2000

    def __init__(self, directory, cc, sanitize, optimize, trace):
        compiler_output = io.StringIO()  # the suites already report build diagnostics
        try:
            with contextlib.redirect_stdout(compiler_output):
                self.binary = build(Path(directory), cc, sanitize, with_codegen=True)
        except RuntimeError as exc:
            raise RuntimeError(f"{exc}\n{compiler_output.getvalue()}") from None
        self.optimize, self.trace = optimize, trace
        self.seen = {}

    def invoke(self, *args, **extra_env):
        env = os.environ | {"OPT_TEST_FAIL_ALLOC": "0", "TAC_TEST_FAIL_ALLOC": "0"} | extra_env
        try:
            return subprocess.run([str(self.binary), *args], capture_output=True, text=True, timeout=15, env=env)
        except subprocess.TimeoutExpired:
            return subprocess.CompletedProcess(args, -1, "", "driver timed out")

    def tac(self, *args, **extra_env):
        """(TAC before the optimizer, TAC after it) or (None, [error rows]); the AST goes to stderr."""
        process = self.invoke(*args, **extra_env)
        try:
            data = json.loads(process.stdout)
        except ValueError:
            return process, None, [(f"driver exited {process.returncode}", "fault"),
                                   *((line, None) for line in process.stdout.splitlines())]
        if "before" not in data:
            return process, None, [(f"TAC generation failed: {json.dumps(data)}", "fault")]
        return process, data["before"], data["after"]

    def paired(self, tac, lines, owner, faulty):
        """TAC instruction on the left, the IFJcode26 lines codegen emitted for it on the right."""
        texts = {index: f"{index:>3} {tac_text(instruction)}" for index, instruction in enumerate(tac)}
        width = min(46, max(map(len, texts.values()), default=0))
        emitted, previous = set(owner.values()), object()
        for number, line in enumerate(lines, 1):
            current = owner[number]
            if current is not previous and current is not None:
                for skipped in range((previous if isinstance(previous, int) else -1) + 1, current):
                    if skipped not in emitted:
                        yield f"{texts.get(skipped, '?'):<{width}} │      (no code)", "dim"
            left = texts.get(current, "") if current is not previous else ""
            previous = current
            row = f"{left[:width]:<{width}} │ {number:>3}  {line}"
            yield (row + "   <- runtime error here", "fault") if number == faulty else (row, None)
        for skipped in range((previous if isinstance(previous, int) else -1) + 1, len(tac)):
            if skipped not in emitted:
                yield f"{texts[skipped]:<{width}} │      (no code)", "dim"

    def steps(self, tac, lines, owner, stdin):
        """Run the generated code in the test executor, one row per executed IFJcode26 instruction."""
        texts = {index: f"{index:>3} {tac_text(instruction)}" for index, instruction in enumerate(tac)}
        width = min(40, max(map(len, texts.values()), default=0))
        code = max((len(line) for line in lines), default=0)
        yield f"{'step':>5}  {'TAC':<{width}} │ line  {'IFJcode26':<{code}}  effect", "dim"
        try:
            vm = Executor("\n".join(lines), stdin)
        except TargetError as exc:
            yield f"error {exc.code}: {exc}", "fault"
            return
        previous = object()
        for step in range(1, self.TRACE_LIMIT + 1):
            if vm.pc >= len(vm.instructions):
                break
            pc, output = vm.pc, vm.output
            number, words, text = vm.instructions[pc]
            current = owner.get(number)
            left = texts.get(current, "") if current is not previous else ""
            previous = current
            row = f"{step:>5}  {left[:width]:<{width}} │ {number:>4}  {text:<{code}}  "
            try:
                exit_code = vm.step()
            except UnsupportedInstruction as exc:
                yield row + f"test executor does not implement {exc}", "fault"
                return
            except (TargetError, ValueError, IndexError, KeyError, TypeError) as exc:
                yield row + f"error {getattr(exc, 'code', '?')}: {exc}", "fault"
                return
            op, *arguments = words
            if op == "WRITE":
                effect = f"stdout += {vm.output[len(output):]!r}"
            elif op in ("JUMPIFEQ", "JUMPIFNEQ"):
                effect = "jump taken" if vm.pc != pc + 1 else "not taken"
            elif op in ("CALL", "RETURN", "JUMP") and vm.pc < len(vm.instructions):
                effect = f"-> line {vm.instructions[vm.pc][0]}"
            elif op in self.NO_EFFECT or not arguments:
                effect = ""
            else:
                frame, key = vm.frame(arguments[0])
                effect = f"{arguments[0]} = {value_text(frame[key])}"
            yield (row + effect).rstrip(), "step"
            if exit_code is not None:
                yield f"exit {exit_code}, stdout {vm.output!r}", "done"
                return
        else:
            yield f"trace stops after {self.TRACE_LIMIT} steps", "dim"
            return
        yield f"ran off the end of the program, stdout {vm.output!r}", "done"

    def sections(self, runner, title, stdin, faulty):
        """[(heading, rows)] with rows of (text, style); None for cases without artifacts (allocation faults)."""
        if "allocation #" in title:
            return None
        key = (runner, title, stdin if self.trace else None)
        if key in self.seen:
            return [("", [(f"same AST/TAC/IFJcode26 as [{self.seen[key]:03d}] above", "dim")])]
        kind, _, name = title.partition("/") if runner == "run_tests.py" else ("ast", "", title)
        if kind != "ast":
            name, _, seed = name.partition(" seed=")
            _, before, after = self.tac("raw", name, seed or "1")
            if before is None:
                return [("TAC", after)]
            return [("", [("raw TAC fixture: no AST and no codegen", "dim")]), ("TAC", tac_listing(before)),
                    ("TAC optimized", tac_listing(after) if after != before else [("(unchanged)", "dim")])]

        process, before, after = self.tac("ast", name, DUMP_AST="1")
        sections = [("AST", [(line, None) for line in process.stderr.splitlines()])]
        if before is None:
            return sections + [("TAC", after)]
        used, other = (after, before) if self.optimize else (before, after)
        generated = self.invoke("codegen-opt" if self.optimize else "codegen", name, TAC_MARKERS="1")
        lines, owner, current = [], {}, None
        for line in generated.stdout.splitlines():
            if line.startswith("#@tac "):
                current = int(line.split()[1])
            else:
                lines.append(line)
                owner[len(lines)] = current
        label = "TAC optimized" if self.optimize else "TAC"
        sections.append((f"{label}  │  IFJcode26", self.paired(used, lines, owner, faulty)))
        if generated.returncode:
            sections.append(("codegen failed", [(f"driver exited {generated.returncode}", "fault"),
                                                *((line, None) for line in generated.stderr.splitlines())]))
        if other == used:
            sections.append(("TAC optimized", [("(unchanged)", "dim")]))
        else:
            sections.append(("TAC before the optimizer" if self.optimize else "TAC optimized (not used for the code above)",
                             tac_listing(other)))
        if self.trace and not generated.returncode:
            sections.append((f"Step by step (test executor, stdin {stdin!r})", self.steps(used, lines, owner, stdin)))
        return sections


class Palette:
    CODES = dict(bold="1", dim="2", red="31", green="32", yellow="33", blue="34", magenta="35", cyan="36")

    def __init__(self, enabled):
        self.enabled = enabled

    def __call__(self, text, *styles):
        if not self.enabled or not styles or not text:
            return text
        return "\033[" + ";".join(self.CODES[s] for s in styles) + "m" + text + "\033[0m"


class Renderer:
    def __init__(self, color, unicode, quiet, show=None, dumper=None, interactive=False):
        self.p, self.g, self.quiet = Palette(color), GLYPHS[unicode], quiet
        self.show, self.dumper, self.interactive = show, dumper, interactive
        self.width = max(60, min(160, shutil.get_terminal_size((100, 24)).columns))

    def rule(self, title, color):
        text = f"{self.g['rule'] * 2} {title} "
        print(self.p(text + self.g["rule"] * max(0, self.width - len(text)), "bold", color))

    def bar(self, passed, total, width):
        filled = round(width * passed / total) if total else 0
        return self.p(self.g["full"] * filled, "green") + self.p(self.g["empty"] * (width - filled), "red")

    def suite(self, suite):
        self.rule(f"IFJ26 codegen {self.g['bullet']} {suite.label} {self.g['bullet']} {len(suite.cases)} cases", "cyan")
        self.build(suite)
        if suite.build_error:
            return
        failures = [case for case in suite.cases if not case.passed]
        if not self.quiet:
            self.groups(suite)
        if failures:
            print()
            print("  " + self.p(f"Failures ({len(failures)})", "bold", "red"))
            for case in failures:
                if self.quiet:
                    print(f"    {self.p(self.g['fail'], 'red')} {case.title}  {self.p(f'[{case.number:03d}]', 'dim')}")
                else:
                    self.failure(case)
                    self.artifacts(suite, case)
        if self.show == "all":
            passes = [case for case in suite.cases if case.passed and "allocation #" not in case.title]
            if passes:
                print()
                print("  " + self.p(f"Passing cases ({len(passes)})", "bold", "green"))
            for case in passes:
                print()
                print(f"  {self.p(self.g['ok'], 'green', 'bold')} {self.p(case.title, 'bold')}  "
                      f"{self.p(f'[{case.number:03d}]', 'dim')}")
                self.artifacts(suite, case)
        print()
        self.totals(suite)

    def build(self, suite):
        diagnostics = list(dict.fromkeys(suite.diagnostics))
        warnings = sum(kind == "warning" for _, kind, _ in diagnostics)
        errors = len(diagnostics) - warnings
        status = self.p(f"{self.g['fail']} ERROR", "bold", "red") if suite.build_error else \
            self.p(f"{self.g['ok']} PASS", "bold", "green")
        counts = [self.p(f"{errors} error(s)", "red")] if errors else []
        counts += [self.p(f"{warnings} warning(s)", "yellow")] if warnings else []
        print(f"  {self.p('Build', 'bold')}  {status}  " + ", ".join(counts))
        for location, kind, text in diagnostics:
            glyph, color = (self.g["warn"], "yellow") if kind == "warning" else (self.g["fail"], "red")
            print(f"    {self.p(glyph, color)} {self.p(location.removeprefix(str(ROOT) + '/'), 'bold')} {text}")
        if suite.build_error:
            print("    " + self.p(suite.build_error, "red"))

    def groups(self, suite):
        stats = {}
        for case in suite.cases:
            stats.setdefault(suite.group_of(case.title), [0, 0])[0 if case.passed else 1] += 1
        if not stats:
            return
        print()
        print("  " + self.p("Groups", "bold"))
        width = max(map(len, stats))
        for name, (passed, failed) in stats.items():
            total = passed + failed
            mark = self.p(self.g["ok"], "green") if not failed else self.p(f"{self.g['fail']} {failed} failed", "red")
            label = self.p(name.ljust(width), "bold" if failed else "dim")
            print(f"    {label}  {f'{passed}/{total}'.rjust(7)}  {self.bar(passed, total, 20)}  {mark}")

    def failure(self, case):
        p = self.p
        print()
        print(f"  {p(self.g['fail'], 'red', 'bold')} {p(case.title, 'bold')}  {p(f'[{case.number:03d}]', 'dim')}")
        styles = {"Program": ("cyan",), "Example": ("cyan",), "Input": ("dim",), "Expected": ("green",),
                  "Actual": ("red",), "Error": ("yellow",), "Inspect": ("blue",), "Writes": ("dim",)}
        for key, value in case.details:
            if key == "Error" and (match := EXPECTED_ACTUAL.match(value)):
                value = (p(match[1], "yellow") + p(match[2], "green") + p(match[3], "yellow") +
                         p(match[4], "red") + p(match[5] or "", "dim"))
                print(f"      {p('error'.ljust(9), 'dim')} {value}")
            elif key:
                label = "generated" if key.startswith("Generated") else key.lower()
                if label == "generated":
                    value = key.removeprefix("Generated ") + ": " + value
                for index, chunk in enumerate(textwrap.wrap(value, self.width - 18) or [""]):
                    print(f"      {p((label if not index else '').ljust(9), 'dim')} {p(chunk, *styles.get(key, ('magenta',)))}")
            else:
                print("        " + p(value, "dim"))

    def artifacts(self, suite, case):
        if not self.dumper:
            return
        faulty = next((int(key.split()[-1]) for key, _ in case.details if key.startswith("Generated line")), None)
        stdin = suite.stdin_of(case)
        sections = self.dumper.sections(suite.runner, case.title, stdin, faulty)
        if sections is None:
            return
        self.dumper.seen.setdefault((suite.runner, case.title, stdin if self.dumper.trace else None), case.number)
        styles = {None: (), "step": (), "dim": ("dim",), "fault": ("bold", "red"), "done": ("bold", "green")}
        pause = self.interactive
        for heading, rows in sections:
            if heading:
                hint = "  (Enter = next step, c = run to the end)" if pause and heading.startswith("Step by step") else ""
                print("      " + self.p(f"{self.g['rule'] * 2} {heading}", "bold", "magenta") + self.p(hint, "dim"))
            empty = True
            for text, style in rows:
                empty = False
                stepping = pause and style == "step"  # the Enter the user types ends the line
                print("        " + self.p(text, *styles[style]), end="" if stepping else "\n", flush=True)
                if stepping:
                    try:
                        pause = input().strip() != "c"
                    except EOFError:
                        pause = False
                        print()
            if empty:
                print("        " + self.p("(empty)", "dim"))

    def totals(self, suite, label=None):
        prefix = f"  {self.p(label.ljust(18), 'bold')}" if label else "  "
        if suite.build_error or not suite.cases:
            print(prefix + self.p(f"{self.g['fail']} no results", "bold", "red"))
            return
        total = len(suite.cases)
        failed = sum(not case.passed for case in suite.cases)
        passed = total - failed
        verdict = self.p(f"{self.g['ok']} ALL PASS", "bold", "green") if not failed else \
            self.p(f"{self.g['fail']} {failed} failed", "bold", "red")
        print(f"{prefix}{verdict}  {self.p(f'{passed} passed', 'green')}  {total} total  "
              f"{self.p(f'{100 * passed / total:5.1f}%', 'bold')}  {self.bar(passed, total, 30)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("all", "tac", "output"), default="all")
    parser.add_argument("-k", "--filter", default="", help="only cases whose name contains this text")
    parser.add_argument("-q", "--quiet", action="store_true", help="build, failed names and totals only")
    parser.add_argument("--sanitize", action="store_true", help="ASan + UBSan build")
    parser.add_argument("--optimize", action="store_true", help="output suite: run the optimizer before codegen")
    parser.add_argument("--no-fault-injection", action="store_true", help="tac suite: skip allocation-failure cases")
    parser.add_argument("--interpreter", help="output suite: official interpreter template containing {source}")
    parser.add_argument("-s", "--show", nargs="?", const="fail", choices=("fail", "all"),
                        help="print AST, TAC and IFJcode26 under failed cases (or all cases with 'all'); "
                             "combine with -k to pick cases")
    parser.add_argument("-t", "--trace", action="store_true",
                        help="with --show: also execute the generated code and print every step with its effect")
    parser.add_argument("--step", action="store_true",
                        help="like --trace, but wait for Enter after each step (c = run to the end)")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto")
    options = parser.parse_args()

    options.trace |= options.step
    if options.trace and not options.show:
        options.show = "fail"
    if options.show and options.quiet:
        parser.error("--show prints full cards; drop -q")
    common = ["--cc", options.cc] + (["--sanitize"] if options.sanitize else []) + (["--filter", options.filter] if options.filter else [])
    suites = []
    if options.suite in ("all", "tac"):
        suites.append(Suite("TAC + optimizer", "run_tests.py",
                            common + (["--no-fault-injection"] if options.no_fault_injection else []), tac_group))
    if options.suite in ("all", "output"):
        suites.append(Suite("program output" + (" (optimized)" if options.optimize else ""), "run_output_tests.py",
                            common + (["--optimize"] if options.optimize else []) +
                            (["--interpreter", options.interpreter] if options.interpreter else []), output_group))
        selected = [entry for entry in [*CASES, *([("main_after_function", "", "", "720", "call")] * options.optimize)]
                    if options.filter in entry[0]]  # same order and numbering as run_output_tests.py
        suites[-1].stdin_of = lambda case: selected[case.number - 1][2] if case.number <= len(selected) else ""

    color = options.color == "always" or (options.color == "auto" and sys.stdout.isatty() and
                                          "NO_COLOR" not in os.environ and os.environ.get("TERM") != "dumb")
    unicode = (sys.stdout.encoding or "").lower().replace("-", "").startswith("utf8")
    with tempfile.TemporaryDirectory(prefix="ifj-pretty-") as directory:
        dumper = None
        if options.show:
            try:
                dumper = Dumper(directory, options.cc, options.sanitize, options.optimize, options.trace)
            except (OSError, RuntimeError) as exc:
                print(f"cannot build the driver for --show: {exc}", file=sys.stderr)
                return 2
        return report(suites, Renderer(color, unicode, options.quiet, options.show, dumper,
                                       options.step and sys.stdin.isatty()), color)


def report(suites, renderer, color):
    for index, suite in enumerate(suites):
        print(Palette(color)(f"Running {suite.label} tests...", "dim"), file=sys.stderr, flush=True)
        if index:
            print()
        renderer.suite(run(suite))
    if len(suites) > 1:
        print()
        renderer.rule("Overall", "magenta")
        for suite in suites:
            renderer.totals(suite, label=suite.label)
    if any(suite.build_error or not suite.cases for suite in suites):
        return 2
    return 1 if any(not case.passed for suite in suites for case in suite.cases) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        sys.exit(1)
