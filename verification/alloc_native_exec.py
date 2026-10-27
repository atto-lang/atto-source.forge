#!/usr/bin/env python3
"""Execute Forge's isolated Point source slice as real x86-64 ELF code.

The test links the emitted assembly and observes malloc/free via GNU ld --wrap.
This is *not* a claim that the general Oreslang frontend or other backends
implement allocation elimination.
"""
import argparse
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import tempfile

DRIVER = r"""
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern long long oresforge_alloc_entry(long long, long long);
void *__real_malloc(size_t size);
void __real_free(void *ptr);
static volatile unsigned long observed_malloc;
static volatile unsigned long observed_free;
static volatile int observing;

void *__wrap_malloc(size_t size) {
    if (observing) ++observed_malloc;
    return __real_malloc(size);
}
void __wrap_free(void *ptr) {
    if (observing) ++observed_free;
    __real_free(ptr);
}
static int parse_i64(const char *text, long long *out) {
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno || !text[0] || !end || *end) return 0;
    *out = value;
    return 1;
}
int main(int argc, char **argv) {
    long long left = 0, right = 0;
    if (argc != 3 || !parse_i64(argv[1], &left) ||
        !parse_i64(argv[2], &right)) return 84;
    observed_malloc = observed_free = 0;
    observing = 1;
    const long long value = oresforge_alloc_entry(left, right);
    observing = 0;
    if (printf("%lld|%lu|%lu\n", value,
               observed_malloc, observed_free) < 0) return 85;
    return 0;
}
"""

def source(x="a", y="b", result="p.x + p.y"):
    return (
        "define struct Point as\n"
        "  int x\n"
        "  int y\n"
        "end\n"
        "define pub fnc main(a:int,b:int):int {\n"
        f"  let p = Point {{ x: {x}, y: {y} }};\n"
        f"  return {result};\n"
        "}\n"
    )

def command(argv, timeout=15):
    return subprocess.run(argv, text=True, capture_output=True, check=False,
                          timeout=timeout)

def check(ok, description):
    if not ok:
        raise AssertionError(description)

def emit(compiler, src, safe):
    argv = [compiler, "emit-alloc-asm", "x86_64", str(src)]
    if safe:
        argv.insert(1, "--opt-escape=safe")
    p = command(argv)
    check(p.returncode == 0 and p.stdout and not p.stderr,
          f"compiler emission failed: {argv!r}: {p!r}")
    text = p.stdout
    check(".globl oresforge_alloc_entry" in text and
          "jo .Loresforge_alloc_overflow" in text,
          "emitted asm lacks symbol or checked add overflow")
    # Make accidental dead-branch, no-op promotion fail explicitly.
    check(("call malloc@PLT" in text) != safe, "unexpected malloc emission")
    check(("call free@PLT" in text) != safe, "unexpected free emission")
    return text

def link(cc, folder, asm, tag):
    assembly = folder / (tag + ".s")
    assembly.write_text(asm, encoding="utf-8")
    exe = folder / (tag + ".exe")
    p = command([cc, "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                 str(folder / "driver.c"), str(assembly),
                 "-Wl,--wrap=malloc", "-Wl,--wrap=free", "-o", str(exe)])
    check(p.returncode == 0, f"native assembly/link failed: {p.stderr}")
    return exe

def result(exe, a, b):
    return command([str(exe), str(a), str(b)])

def negative_compilation(compiler, folder):
    wrong = [
        ("escape-by-return", source(result="p"), "unsupported allocation"),
        ("extra-statement", source().replace("return p.x", "p.x; return p.x"),
         "unsupported allocation"),
        ("foreign-call", source(x="read_env()"), "unsupported allocation"),
        ("unknown-field", source(result="p.z + p.x"), "unsupported allocation"),
        ("wrong-layout", source().replace("int y", "int z"), "unsupported allocation"),
        ("non-ascii", source().replace("Point", "Pöint"), "unsupported allocation"),
        ("suspend", source(result="await p.x + p.y"), "unsupported allocation"),
        ("trailing-expr", source() + "\n0\n", "unexpected trailing allocation"),
        ("shadow-parameter", source().replace("let p =", "let a ="),
         "allocation local shadows parameter"),
    ]
    for name, text, _diagnostic in wrong:
        case = folder / (name + ".ores")
        case.write_text(text, encoding="utf-8")
        for safe in (False, True):
            argv = [compiler, "emit-alloc-asm", "x86_64", str(case)]
            if safe:
                argv.insert(1, "--opt-escape=safe")
            p = command(argv)
            check(p.returncode == 1 and not p.stdout and
                  "oresforge:" in p.stderr,
                  f"unsafe source emitted assembly: {name} safe={safe} {p!r}")

    invalid = folder / "not-here.ores"
    for args in (
        ["--opt-escape=safe", "run", str(invalid)],
        ["--opt-escape=safe", "ir-alloc", str(invalid)],
        ["--opt-escape=speculative", "emit-alloc-asm", "x86_64", str(invalid)],
        ["--jit=on", "emit-alloc-asm", "x86_64", str(invalid)],
        ["--opt-escape=safe", "emit-alloc-asm", "arm64", str(invalid)],
    ):
        p = command([compiler, *args])
        check(p.returncode == 1 and not p.stdout and "oresforge:" in p.stderr,
              f"failed to reject unsupported feature: {args}: {p!r}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()
    machine = platform.machine().lower()
    check(platform.system() == "Linux" and machine in ("x86_64", "amd64"),
          "allocation ELF execution requires Linux x86_64")
    compiler = str(args.compiler.resolve(strict=True))
    cc = os.environ.get("CC", "cc")
    check(shutil.which(cc) is not None, "missing Linux C compiler/linker")
    with tempfile.TemporaryDirectory(prefix="oresforge-alloc-native-") as d:
        folder = Path(d)
        (folder / "driver.c").write_text(DRIVER, encoding="utf-8")
        # Each source is parsed by the actual compiler from a real file,
        # not a pre-built assembly fixture.
        cases = (
            ("params", source(), [(10, 20, 30), (-30, 7, -23),
                                (0, 0, 0), (2**63 - 2, 1, 2**63 - 1),
                                (-(2**63), 0, -(2**63))]),
            ("reversed", source(result="p.y + p.x"),
             [(99, -5, 94), (0, 1, 1)]),
            ("repeated-field", source(result="p.x + p.x"),
             [(13, 5, 26), (-8, 1, -16)]),
            ("literal", source(x="7", y="35"),
             [(10, 20, 42), (-11, 200, 42)]),
        )
        evaluations = 0
        for case_id, text, vectors in cases:
            src = folder / (case_id + ".ores")
            src.write_text(text, encoding="utf-8")
            ir = command([compiler, "ir-alloc", str(src)])
            check(ir.returncode == 0 and not ir.stderr and
                  "alloc Point[2]" in ir.stdout and "ret add.i64.checked" in ir.stdout,
                  f"typed allocation IR not admitted: {case_id}")
            baseline = link(cc, folder, emit(compiler, src, False), case_id + "-off")
            optimized = link(cc, folder, emit(compiler, src, True), case_id + "-safe")
            for a, b, expected in vectors:
                left = result(baseline, a, b)
                right = result(optimized, a, b)
                check(left.returncode == right.returncode == 0,
                      f"native run failed: {case_id} {(a,b)} {left!r} {right!r}")
                check(left.stdout == f"{expected}|1|1\n" and
                      right.stdout == f"{expected}|0|0\n",
                      f"heap/optimized discrepancy: {case_id} {(a,b)} "
                      f"{left.stdout!r} vs {right.stdout!r}")
                evaluations += 1
            # Both paths must trap on signed i64 overflow rather than wrap.
            if case_id != "literal":
                for a, b in ((2**63 - 1, 1), (-(2**63), -1)):
                    baseline_overflow = result(baseline, a, b)
                    optimized_overflow = result(optimized, a, b)
                    # repeated-field doubles x, regardless of b.
                    if case_id == "repeated-field":
                        a, b = 2**63 - 1, 0
                        baseline_overflow = result(baseline, a, b)
                        optimized_overflow = result(optimized, a, b)
                    check(baseline_overflow.returncode == -signal.SIGILL and
                          optimized_overflow.returncode == -signal.SIGILL and
                          not baseline_overflow.stdout and not optimized_overflow.stdout,
                          f"overflow not trapped: {case_id}: "
                          f"{baseline_overflow!r} {optimized_overflow!r}")
        negative_compilation(compiler, folder)
    print(f"native heap-vs-scalar equivalence passed ({evaluations} vectors; "
          "observed malloc/free 1/1 vs 0/0; overflow & negative admission)")

if __name__ == "__main__":
    main()
