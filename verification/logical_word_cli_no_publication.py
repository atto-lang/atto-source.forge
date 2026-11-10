#!/usr/bin/env python3
"""Compile real Forge CLI paths; rejected reserved words must publish no stdout."""
import subprocess
import sys
import tempfile
from pathlib import Path

def run(compiler, mode, source):
    with tempfile.TemporaryDirectory(prefix="atto-forge-logical-") as d:
        path = Path(d) / "case.atto"
        path.write_text(source, encoding="utf-8")
        args = [compiler, *mode, str(path)]
        return subprocess.run(args, capture_output=True, timeout=12, check=False)

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: logical_word_cli_no_publication.py <oresforge>")
    compiler = str(Path(sys.argv[1]).resolve(strict=True))
    profiles = [
        ("run",), ("ir",), ("emit-asm", "x86_64"),
        ("run-runtime",), ("ir-runtime",), ("emit-runtime-asm", "x86_64"),
        ("run-calls",), ("emit-call-asm", "x86_64"),
    ]
    accepted = "fnc candy(): int { return 5; } pub fnc main(): int { return candy()+2; }"
    for profile in [("run",), ("run-calls",)]:
        r = run(compiler, profile, accepted)
        if r.returncode != 0 or r.stdout.strip() != b"7":
            raise RuntimeError(
                f"positive control failed: {profile}: "
                f"exit={r.returncode} stdout={r.stdout!r} stderr={r.stderr!r}")
    negatives = []
    for word in ("and","or","xor"):
        negatives.extend([
            "fnc " + word + "(): int { return 1; } pub fnc main(): int { return 7; }",
            "fnc helper(" + word + ": int): int { return 7; } pub fnc main(): int { return helper(2); }",
            "pub fnc main(): int { return 1 " + word + " 2; }",
            "fnc hidden(): int { return 1 " + word + " 2; } pub fnc main(): int { return 7; }",
            "pub fnc main(): int { return " + word + "; }",
            "pub fnc main(): int { return " + word + "(); }",
        ])
    executions = 0
    for profile in profiles:
        for source in negatives:
            r = run(compiler, profile, source)
            if r.returncode == 0:
                raise RuntimeError(f"unexpected acceptance: {profile}: {source}: {r.stdout!r}")
            if r.stdout != b"":
                raise RuntimeError(f"partial IR/ASM/result publication: {profile}: "
                                   f"{source}: {r.stdout!r}")
            if b"logical word operator requires typed bool/CFG lowering" not in r.stderr:
                raise RuntimeError(f"unexpected compiler rejection: {profile}: "
                                   f"{source}: exit={r.returncode} stderr={r.stderr!r}")
            executions += 1
    print("FORGE_LOGICAL_WORD_CLI_REJECTION_PASS negative_invocations=" + str(executions))
if __name__ == "__main__":
    main()
