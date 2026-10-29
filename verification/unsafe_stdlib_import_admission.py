#!/usr/bin/env python3
"""G0 Forge negative guest tests: no unsafe stdlib import may emit IR or asm.

This tests actual Forge CLI subprocess modes when run via CTest, not a native
FFI positive implementation or compiled Oreslang unsafe feature.
"""
import pathlib
import subprocess
import sys
import tempfile

MODES = [
    ("run",), ("ir",), ("run-runtime",), ("ir-runtime",),
    ("emit-asm", "x86_64"), ("emit-runtime-asm", "x86_64"),
    ("run-calls",), ("emit-call-asm", "x86_64"),
]
PATHS = [
    "ores.std/unsafe", "ores.std/unsafe/ffi",
    "std/unsafe", "std/unsafe/native",
    "ores.std.unsafe.memory", "std.unsafe.ffi",
    "ores.std:unsafe", "std:unsafe.ffi",
]


def main(binary):
    observed = 0
    with tempfile.TemporaryDirectory(prefix="oresforge-unsafe-stdlib-") as tmp:
        source = pathlib.Path(tmp) / "guest.ores"
        for path in PATHS:
            # Both independent frontends reject imports before loading or
            # lowering; a local alias cannot make a native module available.
            for declaration in (
                f'import module ffi from "{path}";',
                f'import module ffi as safe_alias from "{path}";',
            ):
                source.write_text(
                    declaration + " pub fnc main():int{return 42;}\n", encoding="utf-8")
                for mode in MODES:
                    result = subprocess.run(
                        [binary, *mode, str(source)],
                        capture_output=True, text=True, timeout=20,
                    )
                    if (
                        result.returncode == 0
                        or result.stdout.strip()
                        or "source imports require verified module graph lowering"
                        not in result.stderr
                    ):
                        raise AssertionError(
                            (mode, declaration, result.returncode,
                             result.stdout, result.stderr)
                        )
                    observed += 1
    assert observed == len(PATHS) * 2 * len(MODES), observed
    print(f"PASS Forge unsafe stdlib guest imports: {observed} actual CLI negative checks")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: unsafe_stdlib_import_admission.py /path/to/oresforge")
    main(sys.argv[1])
