#!/usr/bin/env python3
"""Lexer + parser integration tests. See README.md next to this file."""
import argparse
import difflib
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent / "src"
CASES = HERE / "cases"
EXPECTED_ERROR = {"valid": 0, "lexical_error": 1, "syntax_error": 2}  # case directory -> compiler error code
TIMEOUT_SECONDS = 5


def build(out_dir, sanitize):
    probe = Path(out_dir) / "probe"
    sources = [HERE / "probe.c", HERE / "ast_print.c"] + sorted(p for p in SRC.glob("*.c") if p.name != "main.c")
    command = shlex.split(os.environ.get("CC", "cc")) + ["-std=gnu11", "-g", "-O0", f"-I{SRC}", f"-I{HERE}"]
    command += ["-include", str(HERE / "lexer_glue.h")]
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += [str(s) for s in sources] + ["-lm", "-o", str(probe)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print("BUILD FAILED\n" + result.stderr)
        sys.exit(2)
    return probe


def run_probe(probe, source):
    """Returns (error code or None, AST text, parser log, reason the run is unusable or None)."""
    try:
        result = subprocess.run([str(probe), str(source)], capture_output=True, text=True, errors="replace",
                                timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return None, "", "", f"timeout after {TIMEOUT_SECONDS} s (endless loop?)"
    log = result.stderr.strip()
    first_line, _, tree = result.stdout.partition("\n")
    if result.returncode != 0 or not first_line.startswith("error: "):
        return None, "", log, f"probe failed (exit status {result.returncode})"
    return int(first_line.split()[1]), tree, log, None


def indent(text):
    return "".join(f"      {line}\n" for line in text.splitlines())


def check(probe, source, update):
    """Returns (list of problems, AST text)."""
    expected_error = EXPECTED_ERROR[source.parent.name]
    error, tree, log, broken = run_probe(probe, source)
    if broken:
        return [broken + ("\n" + indent(log) if log else "")], tree
    problems = []
    if error != expected_error:
        problems.append(f"error code: expected {expected_error}, got {error}\n" + indent(log or "(parser logged nothing)"))
    snapshot = source.with_suffix(".ast")
    if error == 0 and expected_error == 0:
        if update:
            snapshot.write_text(tree)
        elif snapshot.exists() and snapshot.read_text() != tree:
            diff = difflib.unified_diff(snapshot.read_text().splitlines(), tree.splitlines(), "expected", "actual",
                                        lineterm="")
            problems.append("AST differs from " + snapshot.name + "\n" + indent("\n".join(diff)))
    return problems, tree


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-k", metavar="TEXT", default="", help="run only cases whose group/name contains TEXT")
    parser.add_argument("-v", "--verbose", action="store_true", help="print the AST of every case that parsed")
    parser.add_argument("--sanitize", action="store_true", help="build with AddressSanitizer and UBSan")
    parser.add_argument("--update", action="store_true",
                        help="write the current AST of valid cases to .ast snapshots (review them before committing)")
    parser.add_argument("--ast", metavar="FILE", help="only print the error code and AST of one source file")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as build_dir:
        probe = build(build_dir, args.sanitize)
        if args.ast:
            return subprocess.run([str(probe), args.ast]).returncode

        sources = sorted(s for s in CASES.glob("*/*.ifj") if args.k in f"{s.parent.name}/{s.stem}")
        failed = []
        for source in sources:
            name = f"{source.parent.name}/{source.stem}"
            problems, tree = check(probe, source, args.update)
            has_snapshot = source.with_suffix(".ast").exists()
            print(f"{'FAIL' if problems else 'PASS'}  {name}{'  [ast]' if has_snapshot and not problems else ''}")
            for problem in problems:
                print("    " + problem, end="" if problem.endswith("\n") else "\n")
            if args.verbose and tree:
                print(indent(tree), end="")
            if problems:
                failed.append(name)

    print(f"\nTOTAL: {len(sources)} | PASS: {len(sources) - len(failed)} | FAIL: {len(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
