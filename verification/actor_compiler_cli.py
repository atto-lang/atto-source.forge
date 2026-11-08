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
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: actor_compiler_cli.py <compiler> <counter.atto> "
            "<depth.atto> <counter-main.atto> <recursive-main.atto>"
        )
    exe, counter, depth, counter_main, recursive_main = map(
        lambda s: Path(s).resolve(strict=True), sys.argv[1:6]
    )
    ok(launch(exe, "run-actor", str(counter), "3", "4", "-1"), b"3\n7\n6\n")
    ok(launch(exe, "run-actor", str(depth), "0", "1", "63"), b"0\n1\n63\n")
    ok(launch(exe, "run-actor-main", str(counter_main)), b"42\n")
    ok(launch(exe, "run-actor-main", str(recursive_main)), b"5\n")
    c_ir = launch(exe, "actor-ir", str(counter))
    if c_ir.returncode or c_ir.stderr or b"checked-add-field" not in c_ir.stdout:
        raise AssertionError(f"compiled scalar actor IR missing: {c_ir!r}")
    r_ir = launch(exe, "actor-ir", str(depth))
    if r_ir.returncode or r_ir.stderr or b"stackless-loop-v1" not in r_ir.stdout:
        raise AssertionError(f"compiled recursion IR missing: {r_ir!r}")
    main_ir = launch(exe, "actor-ir", str(counter_main))
    if (main_ir.returncode or main_ir.stderr or
            b"main : synchronous-source-actor-v0" not in main_ir.stdout or
            b"spawn Counter as left" not in main_ir.stdout or
            b"spawn Counter as right" not in main_ir.stdout):
        raise AssertionError(f"source actor spawn IR missing: {main_ir!r}")
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
    source_main = counter_main.read_text(encoding="utf-8")
    source_recursion = recursive_main.read_text(encoding="utf-8")
    main_mutations = [
        source_main.replace("const right = spawn", "const left = spawn"),
        source_main.replace("spawn Counter();", "spawn Worker();", 1),
        source_main.replace("right.on_message(5);", "unknown.on_message(5);"),
        source_main.replace("left.on_message(3);", "left.send(3);"),
        source_main.replace("right.on_message(5);", "right.on_message(5 + 1);"),
        source_main.replace("return left.on_message(4) +", "return left.on_message(4) -"),
        source_main.replace("return left.on_message(4) +", "return left.on_message(4) %"),
        source_main.replace("return left.on_message(4) +", "return left.on_message(4) ^"),
        source_main.replace("left.on_message(3);", "await left.on_message(3);"),
        source_main.replace("const left = spawn", "const self = spawn"),
        source_main.replace("return left.on_message(4) + right.on_message(30);", "return 42;"),
        source_main.replace("const right = spawn Counter();", ""),
        source_main + "pub fnc main(): int { return 1; }\n",
        source_main.replace("left.on_message(3);", "left.on_message(9223372036854775808);"),
        source_main.replace("const right = spawn Counter();", "const right = spawn Counter(4);"),
        source_main.replace("right.on_message(5);", "right.on_message(-9223372036854775809);"),
        source_main.replace("const right = spawn Counter();", "const right = spawn Counter(); const left = spawn Counter();"),
        source_main.replace("return left.on_message(4) + right.on_message(30);", "return left.on_message(4) + unknown.on_message(30);"),
        source_recursion.replace("first.on_message(16);", "first.on_message(64);"),
    ]
    with tempfile.TemporaryDirectory(prefix="atto-actor-source-main-") as temp:
        src = Path(temp) / "case.atto"
        for i, mutant in enumerate(main_mutations):
            if mutant in (source_main, source_recursion):
                raise AssertionError(f"actor-main mutation {i} was a no-op")
            src.write_text(mutant, encoding="utf-8")
            deny(launch(exe, "run-actor-main", str(src)))
            # Except the overflow mutant, which is syntactically valid,
            # all cases must fail static actor source admission.
            if i + 1 < len(main_mutations):
                deny(launch(exe, "actor-ir", str(src)))
            negatives += 1 if i + 1 == len(main_mutations) else 2
        # The two entrypoints are intentionally disjoint.
        deny(launch(exe, "run-actor-main", str(counter)))
        deny(launch(exe, "run-actor", str(counter_main), "3"))
        negatives += 2
        # The general scalar and callable pipelines remain closed to actor
        # syntax even when this new opt-in source main profile is accepted.
        for mode in (("run",), ("ir",), ("run-runtime",),
                     ("ir-runtime",), ("run-calls",),
                     ("emit-asm", "x86_64"), ("emit-runtime-asm", "x86_64"),
                     ("emit-call-asm", "x86_64")):
            deny(launch(exe, *mode, str(counter_main)))
            negatives += 1
    print(f"ATTO_FORGE_SOURCE_ACTOR_MAIN_CLI|positive=3|negative={negatives-46}|spawn_profile=sync-v0|guest_source=1|native_aot=NOT_EXECUTED")
    print(f"ATTO_FORGE_COMPILED_ACTOR_CLI|positive=4|negative=46|actor_source=1|general_spawn=NOT_EXECUTED")


if __name__ == "__main__":
    main()
