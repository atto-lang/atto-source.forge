#!/usr/bin/env python3
"""Real Forge scalar frontend gates; no claim of full AttoLang runtime parity."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

# Byte-for-byte identical to the Java/LLVM canonical source fixtures.
CASES = (
    ("removed-modulo", "pub fnc main(): int { return 9 % 2; }\n",
     "45d5794870e93074d9b11b90833a849e96aaa4c1",
     "legacy % operator is removed", "SEMANTIC_REJECT"),
    ("removed-caret", "pub fnc main(): int { return 2 ^ 3; }\n",
     "6889c7e83c8d94b906fbfe78723caee9580da6aa",
     "legacy ^/^^ operators are removed", "SEMANTIC_REJECT"),
    ("removed-double-caret", "pub fnc main(): int { return 2 ^^ 3; }\n",
     "7ee99948018e927cce74c9ad0edc1975e4251689",
     "legacy ^/^^ operators are removed", "SEMANTIC_REJECT"),
    ("type-first-reassignment",
     "pub fnc main(): int { int x = 7; x = 8; return x; }\n",
     "b118f53e68565dcb9e6acd58deb8ee47dca67606",
     "E_LOCALS_UNSUPPORTED", "UNSUPPORTED_NOT_SEMANTIC_PROOF"),
    ("typed-const-reassignment",
     "pub fnc main(): int { const int x = 7; x = 8; return x; }\n",
     "784ebd47a50211e6f00cb732cc74319ed1e58fac",
     "E_LOCALS_UNSUPPORTED", "UNSUPPORTED_NOT_SEMANTIC_PROOF"),
    ("legacy-async-modifier",
     "// Legacy async is forbidden for functions; use suspends instead.\n"
     "async fnc main(): int {\n  return 0;\n}\n",
     "26d392c8f609a730e81b503d9495de7e29b46755",
     "E_CALLABLE_MODIFIERS", "SEMANTIC_REJECT"),
    ("critical-suspends-modifier",
     "// A critical function must never carry a suspending modifier.\n"
     "critical suspends fnc main(): int {\n  return 0;\n}\n",
     "6be781ff89f9ebc48ee5870d3401943d50b0f9db",
     "E_CALLABLE_MODIFIERS", "SEMANTIC_REJECT"),
    ("suspends-critical-modifier",
     "// Reversing the illegal modifiers must be rejected too.\n"
     "suspends critical fnc main(): int {\n  return 0;\n}\n",
     "c8bb7c921749f06e9d187a6dfbd71685952e3d88",
     "E_CALLABLE_MODIFIERS", "SEMANTIC_REJECT"),
)
POSITIVE = (
    ("constant-return", "pub fnc main(): int { return 40 + 2; }\n", "42"),
    ("checked-arithmetic", "pub fnc main(): int { return 7 * 6; }\n", "42"),
    ("ignored-comment", "pub fnc main(): int { /* async critical suspends % ^ ^^ */ return 42; }\n", "42"),
)
ERROR_TOKENS = ("unexpected token", "invalid character", "missing main", "internal error",
                "stack overflow", "expected identifier", "unknown symbol")


def git_blob(text):
    b = text.encode("utf-8")
    return hashlib.sha1(b"blob " + str(len(b)).encode("ascii") + b"\0" + b).hexdigest()


def require(value, reason):
    if not value:
        raise AssertionError(reason)


def expected_error(case, stderr):
    name, _source, _blob, diagnostic, _stage = case
    if len(stderr) > 65536 or any(tok in stderr.lower() for tok in ERROR_TOKENS):
        return False
    # Must be the actual compiler-owned error line, not a source echo or a log.
    lines = [line for line in stderr.splitlines() if line.startswith("oresforge: ")]
    if len(lines) != 1:
        return False
    line = lines[0]
    if diagnostic not in line or not re.search(r"\bat byte [0-9]+\s*$", line):
        return False
    if name.startswith("critical-") or name.startswith("suspends-"):
        return "critical suspends modifier combination forbidden" in line
    if name == "legacy-async-modifier":
        return "legacy async modifier forbidden" in line
    if name.endswith("-reassignment"):
        return "local binding ownership lowering" in line
    return "operator" in line and "removed" in line


def preflight():
    require(len(CASES) == 8 and len({c[0] for c in CASES}) == 8,
            "missing or duplicate canonical negative")
    for name, source, expected, _diag, stage in CASES:
        require(git_blob(source) == expected, "canonical source blob drift: " + name)
        require(stage in ("SEMANTIC_REJECT", "UNSUPPORTED_NOT_SEMANTIC_PROOF"),
                "invalid evidence stage: " + name)
    require(sum(c[4] == "SEMANTIC_REJECT" for c in CASES) == 6,
            "incorrect native frontend semantic credit")
    require(len(POSITIVE) == 3 and all(x[2] == "42" for x in POSITIVE),
            "positive-control coverage drift")


def self_test():
    preflight()
    for case in CASES:
        diag = case[3]
        if case[0] == "legacy-async-modifier":
            phrase = "legacy async modifier forbidden; use suspends"
        elif case[0] in ("critical-suspends-modifier", "suspends-critical-modifier"):
            phrase = "critical suspends modifier combination forbidden"
        elif case[0].endswith("-reassignment"):
            phrase = "local binding ownership lowering is not implemented"
        else:
            phrase = diag
        require(expected_error(case, "oresforge: " + phrase + " at byte 20\n"),
                "positive diagnostic oracle not recognized: " + case[0])
        for decoy in ("oresforge: unexpected token at byte 1",
                      "oresforge: internal error " + phrase + " at byte 2",
                      "source: " + phrase + " at byte 2",
                      "oresforge: " + phrase + " without byte location",
                      "note: " + phrase + " at byte 5"):
            require(not expected_error(case, decoy), "forged oracle accepted: " + case[0])
    print("FORGE_ATTO_SEMANTIC_ORACLE_PASS cases=8 negatives=6 unsupported=2")


def run_checked(exe, arguments):
    result = subprocess.run([str(exe), *arguments], capture_output=True, timeout=8,
                            check=False)
    require(result.returncode >= 0 and result.returncode < 128,
            "signal/crash or invalid exit")
    require(len(result.stdout) < 65536 and len(result.stderr) < 65536,
            "unbounded CLI output")
    return result


def compiler_test(executable):
    preflight()
    info = executable.lstat()
    require(stat.S_ISREG(info.st_mode) and not executable.is_symlink(),
            "compiler must be a local native regular file")
    with tempfile.TemporaryDirectory(prefix="atto-forge-semantic-") as temp:
        root = Path(temp)
        for name, source, expected, diagnostic, stage in CASES:
            entry = root / (name + ".atto")
            entry.write_bytes(source.encode("utf-8"))
            require(git_blob(entry.read_text("utf-8")) == expected, name + " bytes drift")
            for command in (["run", str(entry)], ["ir", str(entry)],
                            ["emit-asm", "x86_64", str(entry)]):
                result = run_checked(executable, command)
                require(result.returncode == 1 and not result.stdout,
                        name + " accepted or emitted IR/ASM in " + command[0])
                require(expected_error((name, source, expected, diagnostic, stage),
                                       result.stderr.decode("utf-8", "strict")),
                        name + " wrong semantic/unsupported diagnostic in " + command[0])
            print("FORGE_ATTO_NEGATIVE|" + name + "|stage=" + stage + "|blob=" + expected)
        for name, source, expected in POSITIVE:
            entry = root / (name + ".atto")
            entry.write_text(source, encoding="utf-8")
            result = run_checked(executable, ["run", str(entry)])
            require(result.returncode == 0 and result.stdout.decode().strip() == expected,
                    "false-positive rejection or wrong scalar execution: " + name)
            print("FORGE_ATTO_POSITIVE|" + name + "|value=" + expected)
    print("FORGE_ATTO_COMPILER_GATE_PASS|negative=8|semantic=6|unsupported=2|positive=3")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--compiler", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.compiler is not None:
        compiler_test(args.compiler.resolve(strict=True))
    else:
        parser.error("select --self-test or --compiler")
