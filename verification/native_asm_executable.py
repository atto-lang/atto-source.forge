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
        source = root / "source.ores"
        asm = root / "entry.s"
        executable = root / "native-test"
        for label, expression, expected in CASES:
            text = "define pub fnc main():int{return " + expression + ";}\n"
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
        for label, expression, diagnostic in REJECTIONS:
            text = expression if label == "unsupported_import" else (
                "define pub fnc main():int{return " + expression + ";}\n")
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
        "rejected_negative_by_all_three_modes": len(REJECTIONS),
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
