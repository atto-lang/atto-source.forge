#!/usr/bin/env python3
"""Exact-byte canonical AttoLang stage-0 Forge interpreter + native assembly gate.

Not compiler self-hosting, full language parity, or a signed CI receipt.
"""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

BLOB = "015f8b0f79aafa796a148d2adeff1574e8d6c491"
SOURCE = b"pub fnc main(): int { return 40 + 2; }\n"
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

def run(args, cwd):
    proc = subprocess.run(args, cwd=cwd, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=15, check=False)
    require(len(proc.stdout) <= 65536 and len(proc.stderr) <= 65536,
            "oversized compiler or executable output")
    return proc.returncode, proc.stdout, proc.stderr

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    require(sys.platform.startswith("linux"), "native execution requires Linux")
    arch = {"x86_64": "x86_64", "amd64": "x86_64",
            "aarch64": "arm64", "arm64": "arm64"}.get(platform.machine().lower())
    require(arch is not None, "unsupported native host architecture")
    require(not args.compiler.is_symlink(), "compiler must not be a symlink")
    compiler = args.compiler.resolve(strict=True)
    require(compiler.is_file(), "compiler must be a regular file")
    require(not args.source.is_symlink(), "source must not be a symlink")
    source = args.source.resolve(strict=True)
    require(source.is_file(), "canonical source must be a regular file")
    # Pin exact guest bytes, not a loose output-42 lookalike.
    content = source.read_bytes()
    require(content == SOURCE, "canonical .atto bytes differ")
    git_blob = hashlib.sha1(b"blob " + str(len(content)).encode("ascii") +
                            b"\0" + content).hexdigest()
    require(git_blob == BLOB, "canonical source Git blob mismatch")
    cc = shutil.which(args.cc)
    require(cc is not None, "host C compiler missing")

    with tempfile.TemporaryDirectory(prefix="atto-forge-canonical-") as tmp:
        root = Path(tmp)
        status, output, error = run([str(compiler), "run", str(source)], root)
        require((status, output, error) == (0, b"42\n", b""),
                "Forge compiler + IR interpreter did not return 42")
        status, assembly, error = run([str(compiler), "emit-asm", arch, str(source)], root)
        require(status == 0 and error == b"" and len(assembly) <= 1024 and
                assembly.startswith(b".text\n.globl oresforge_entry\noresforge_entry:\n") and
                assembly.rstrip().endswith(b"ret"),
                "Forge native assembly emission rejected canonical source")
        asm = root / "entry.s"
        asm.write_bytes(assembly)
        driver = root / "driver.c"
        driver.write_text(DRIVER, encoding="ascii")
        executable = root / "native-atto-42"
        status, output, error = run([cc, "-std=c11", "-Wall", "-Wextra",
                                     "-Werror", str(driver), str(asm),
                                     "-o", str(executable)], root)
        require(status == 0, "native compiler/linker failed: " +
                error.decode("utf-8", "replace")[:1000])
        status, output, error = run([str(executable)], root)
        require((status, output, error) == (0, b"42\n", b""),
                "linked native Forge guest diverged from interpreter")
        # In the same process modes, unsupported imports must fail before IR.
        rejected = root / "unsupported-import.atto"
        rejected.write_text('import fnc helper from "./helper";\n' +
                            'pub fnc main(): int { return 42; }\n', encoding="utf-8")
        for mode in ("run", "ir", "emit-asm"):
            command = [str(compiler), mode] + ([arch] if mode == "emit-asm" else [])
            status, output, error = run(command + [str(rejected)], root)
            require(status != 0 and output == b"" and
                    b"source imports require verified module graph lowering" in error,
                    "unsupported import must fail closed in " + mode)
        print(json.dumps({
            "status": "STAGE0_NATIVE_EXECUTED",
            "source_git_blob": git_blob,
            "source_sha256": hashlib.sha256(content).hexdigest(),
            "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
            "native_executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "host_arch": arch,
            "observed_i64": 42,
            "rejected_unsupported_import_modes": 3,
            "codegen_scope": "constant-folded i64 single-file stage-0",
            "cross_backend_certified": False,
        }, sort_keys=True))
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, UnicodeError, subprocess.TimeoutExpired) as exc:
        print("ATTO_FORGE_CANONICAL_NATIVE_FAIL: " + str(exc), file=sys.stderr)
        sys.exit(1)
