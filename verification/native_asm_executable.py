#!/usr/bin/env python3
"""Execute real Forge emitted assembly and compare it with checked source IR.

This is a stage-0 native-code smoke, NOT Oreslang-in-Oreslang self-hosting.
The assembled entry is currently compile-time constant-folded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

CASES = (
    ("precedence", "2+3*4", "14"),
    ("grouping", "(2+3)*4", "20"),
    ("negative", "-(3+2)*7", "-35"),
    ("i64_min", "-9223372036854775808", "-9223372036854775808"),
    ("i64_max", "9223372036854775807", "9223372036854775807"),
    ("i64_max_minus_one", "9223372036854775807-1", "9223372036854775806"),
    ("zero", "4-4", "0"),
)
# Entire source programs exercising the PR #95 pure-function inliner.
# The returned value is still *constant-folded* before emission.
FUNCTION_CASES = (
    ("immutable-type-first-chain",
     "fnc main():int{int x=7;const int y=x+2;return y*3;}", "27"),
    ("immutable-inferred-const",
     "fnc main():int{const x=7;const y=x+2;return y*3;}", "27"),
    ("immutable-helper-initializer",
     "fnc add(x:int):int{return x+1;}"
     " fnc main():int{int value=add(6);return value*3;}", "21"),
    ("forward-two-calls",
     "define pub fnc main():int{return square(3)+plus(4,5);}"
     " fnc square(x:int):int{return x*x;}"
     " fnc plus(a:int,b:int):int{return a+b;}", "18"),
    ("nested-parameter-binding",
     "fnc inc(a:int):int{return a+1;}"
     " fnc times(x:int):int{return x*3;}"
     " fnc main():int{return times(inc(6))-inc(2);}", "18"),
    ("signed-min-through-parameter",
     "fnc main():int{return id(-9223372036854775808);}"
     " fnc id(x:int):int{return x;}", "-9223372036854775808"),
    ("reused-helper",
     "fnc twice(x:int):int{return x*2;}"
     " fnc main():int{return twice(10)+twice(3);}", "26"),
)
FUNCTION_REJECTIONS = (
    ("immutable-self-reference",
     "fnc main():int{int x=x;return x;}", "expected integer at byte"),
    ("inferred-const-self-reference",
     "fnc main():int{const x=x;return x;}", "expected integer at byte"),
    ("immutable-reassignment",
     "fnc main():int{const int x=1;x=2;return x;}",
     "immutable local reassignment is forbidden"),
    ("immutable-duplicate",
     "fnc main():int{int x=1;int x=2;return x;}",
     "invalid or duplicate immutable local"),
    ("immutable-eager-overflow",
     "fnc main():int{const unused=9223372036854775807+1;return 42;}",
     "i64 add overflow"),
    ("direct-recursion",
     "fnc main():int{return recur(1);}"
     " fnc recur(x:int):int{return recur(x);}", "recursive calls not supported"),
    ("arity-mismatch",
     "fnc main():int{return inc(1,2);}"
     " fnc inc(x:int):int{return x+1;}", "function arity mismatch"),
    ("unknown-function",
     "fnc main():int{return missing(1);}", "unknown function"),
    ("unused-invalid-helper",
     "fnc unused(x:int):int{return not_declared;}"
     " fnc main():int{return 1;}", "expected integer at byte"),
    ("checked-call-overflow",
     "fnc main():int{return inc(9223372036854775807);}"
     " fnc inc(x:int):int{return x+1;}", "i64 add overflow"),
    ("nonascii-identifier",
     "fnc main():int{return café;}", "invalid character"),
    ("nonascii-whitespace",
     "fnc main():int{return 1\u00a0;}", "invalid character"),
)
REJECTIONS = (
    ("division", "3/2", "division requires float result lowering"),
    ("overflow_add", "9223372036854775807+1", "i64 add overflow"),
    ("overflow_sub", "-9223372036854775808-1", "i64 sub overflow"),
    ("overflow_mul", "3037000500*3037000500", "i64 mul overflow"),
    ("unknown", "undefined_name", "expected integer"),
    ("unsupported_import", 'import fnc helper from "./helper";\npub fnc main():int{return 1;}', "source imports require verified module graph lowering"),
)
DRIVER = """#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
extern int64_t oresforge_entry(void);
int main(void) {
    if (printf("%" PRId64 "\\n", oresforge_entry()) < 0) return 1;
    return fflush(stdout) == 0 ? 0 : 1;
}
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(argv, *, cwd, timeout=15):
    proc = subprocess.run(argv, cwd=cwd, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, check=False)
    require(len(proc.stdout) <= 65536 and len(proc.stderr) <= 65536,
            "process output exceeded test budget")
    return proc.returncode, proc.stdout.decode("utf-8", "strict"), proc.stderr.decode("utf-8", "strict")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    require(sys.platform.startswith("linux"), "native smoke requires Linux host")
    machine = platform.machine().lower()
    arch = {"x86_64": "x86_64", "amd64": "x86_64",
            "aarch64": "arm64", "arm64": "arm64"}.get(machine)
    require(arch is not None, "unsupported native assembler host: " + machine)
    require(not args.compiler.is_symlink(), "symlinked compiler is not accepted")
    compiler = args.compiler.resolve(strict=True)
    require(compiler.is_file(), "compiler binary is not a file")
    cc = shutil.which(args.cc)
    require(cc is not None, "native C compiler not available")
    compiler_sha = hashlib.sha256(compiler.read_bytes()).hexdigest()
    observations = []
    with tempfile.TemporaryDirectory(prefix="oresforge-native-asm-") as tmp:
        root = Path(tmp)
        driver = root / "driver.c"
        driver.write_text(DRIVER, encoding="ascii")
        source = root / "source.atto"
        asm = root / "entry.s"
        executable = root / "native-test"
        all_cases = [
            (label, "define pub fnc main():int{return " + expression + ";}\n", expected)
            for label, expression, expected in CASES
        ] + [(label, text + "\n", expected) for label, text, expected in FUNCTION_CASES]
        for label, text, expected in all_cases:
            source.write_text(text, encoding="utf-8")
            status, out, err = run([str(compiler), "run", str(source)], cwd=root)
            require((status, out, err) == (0, expected + "\n", ""),
                    "source interpreter mismatch in " + label)
            status, assembly, err = run([str(compiler), "emit-asm", arch, str(source)], cwd=root)
            require(status == 0 and err == "" and
                    assembly.startswith(".text\n.globl oresforge_entry\noresforge_entry:\n") and
                    assembly.endswith("  ret\n") and len(assembly) <= 1024,
                    "incorrect assembly emission in " + label)
            asm.write_text(assembly, encoding="ascii")
            status, out, err = run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                    str(driver), str(asm), "-o", str(executable)], cwd=root)
            require(status == 0, "assembler/link failed in " + label + ": " + err)
            status, out, err = run([str(executable)], cwd=root)
            require((status, out, err) == (0, expected + "\n", ""),
                    "native/checked-IR divergence in " + label)
            observations.append({"case": label, "value": expected,
                                 "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
                                 "assembly_sha256": hashlib.sha256(assembly.encode()).hexdigest()})
        all_rejections = [
            (label, expression if label == "unsupported_import" else (
                "define pub fnc main():int{return " + expression + ";}\n"), diagnostic)
            for label, expression, diagnostic in REJECTIONS
        ] + [(label, text + "\n", diagnostic)
             for label, text, diagnostic in FUNCTION_REJECTIONS]
        for label, text, diagnostic in all_rejections:
            source.write_text(text, encoding="utf-8")
            for mode in ("run", "ir", "emit-asm"):
                argv = [str(compiler), mode] + ([arch] if mode == "emit-asm" else []) + [str(source)]
                status, out, err = run(argv, cwd=root)
                require(status == 1 and out == "" and diagnostic in err,
                        "invalid source leaked output or used wrong diagnostic: " + label + "/" + mode)
    print(json.dumps({
        "kind": "forge-stage0-native-assembly-execution",
        "compiler_sha256": compiler_sha,
        "host_arch": arch,
        "executed_positive": len(observations),
        "executed_function_positive": len(FUNCTION_CASES),
        "rejected_negative_by_all_three_modes": len(all_rejections),
        "rejected_function_negative_by_all_three_modes": len(FUNCTION_REJECTIONS),
        "self_hosted": False,
        "native_codegen_scope": "constant-folded i64 entry only",
        "observations": observations,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, UnicodeError, ValueError, subprocess.TimeoutExpired) as exc:
        print("Forge stage-0 native assembly smoke FAILED: " + str(exc), file=sys.stderr)
        sys.exit(1)
