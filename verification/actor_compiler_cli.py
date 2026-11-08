#!/usr/bin/env python3
"""Opt-in compiled .atto actor frontend; independent default-mode denial."""
from pathlib import Path
import subprocess
import sys
import tempfile

def launch(exe: Path, *args: str):
    return subprocess.run([str(exe), *args], capture_output=True,
                          check=False, timeout=15)

def ok(result, stdout: bytes):
    if result.returncode != 0 or result.stderr or result.stdout != stdout:
        raise AssertionError(f"actor source positive failed: {result!r}")

def deny(result):
    if result.returncode != 1 or result.stdout or not result.stderr:
        raise AssertionError(f"unsupported actor source was published: {result!r}")

def main() -> None:
    exe, counter, depth = map(lambda s: Path(s).resolve(strict=True), sys.argv[1:4])
    if len(sys.argv) != 4:
        raise SystemExit("usage: actor_compiler_cli.py <compiler> <counter.atto> <depth.atto>")
    ok(launch(exe, "run-actor", str(counter), "3", "4", "-1"), b"3\n7\n6\n")
    ok(launch(exe, "run-actor", str(depth), "0", "1", "63"), b"0\n1\n63\n")
    c_ir = launch(exe, "actor-ir", str(counter))
    if c_ir.returncode or c_ir.stderr or b"checked-add-field" not in c_ir.stdout:
        raise AssertionError(f"compiled scalar actor IR missing: {c_ir!r}")
    r_ir = launch(exe, "actor-ir", str(depth))
    if r_ir.returncode or r_ir.stderr or b"stackless-loop-v1" not in r_ir.stdout:
        raise AssertionError(f"compiled recursion IR missing: {r_ir!r}")
    negatives = 0
    source = counter.read_text(encoding="utf-8")
    mutations = [
        source.replace("count + delta", "count - delta"),
        source.replace("count + delta", "count % delta"),
        source.replace("return self.count", "return delta"),
        source.replace("state { count: int", "state { count: any"),
        source.replace("pub fnc on_message", "async fnc on_message"),
        source.replace("on_message(int delta)", "on_message(float delta)"),
        source.replace("self.count = self.count", "self.other = self.count"),
        source.replace("count: int = 0", "count: int = 9223372036854775808"),
        source.replace("on_message", "on_receive"),
        source.replace("end\n", "end\n" + source),
        source + "pub fnc main(): int { return 42; }\n",
        source.replace("return self.count;", "return self.count + 1;"),
        source.replace("define actor", "define virtual actor"),
        source.replace("count + delta", "count ^ delta"),
        source.replace("count + delta", "count + await delta"),
        source.replace("count + delta", "count + 3.14"),
    ]
    # Explicit default frontend stays fail-closed on even the supported actor.
    for mode in (("run",), ("ir",), ("run-runtime",),
                 ("ir-runtime",), ("run-calls",),
                 ("emit-asm", "x86_64"), ("emit-runtime-asm", "x86_64"),
                 ("emit-call-asm", "x86_64")):
        deny(launch(exe, *mode, str(counter)))
        negatives += 1
    with tempfile.TemporaryDirectory(prefix="atto-actor-compiler-") as temp:
        src = Path(temp) / "case.atto"
        for mutated in mutations:
            if mutated == source:
                raise AssertionError("negative mutation did not change source")
            src.write_text(mutated, encoding="utf-8")
            deny(launch(exe, "actor-ir", str(src)))
            deny(launch(exe, "run-actor", str(src), "3"))
            negatives += 2
        src.write_text(source, encoding="utf-8")
        for invalid_args in (("9223372036854775808",),
                             ("1", "not-a-number"), ("1", "2.0"),
                             ("-9223372036854775809",)):
            deny(launch(exe, "run-actor", str(src), *invalid_args))
            negatives += 1
        # Actor A's overflow does not output successful prior turns.
        deny(launch(exe, "run-actor", str(depth), "0", "64"))
        negatives += 1
        # Exhausted instruction budget is a terminal actor fault.
        deny(launch(exe, "run-actor", str(depth), "-1"))
        negatives += 1
    print(f"ATTO_FORGE_COMPILED_ACTOR_CLI|positive=4|negative={negatives}|actor_source=1|general_spawn=NOT_EXECUTED")

if __name__ == "__main__":
    main()
