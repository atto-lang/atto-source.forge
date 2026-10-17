#!/usr/bin/env python3
"""Unsupported source features must fail with a diagnostic and no code output.

The arithmetic control executes first, preventing a broken compiler from
passing a rejection-only suite. This is an admission test, not feature parity.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

def require(condition, *evidence):
    if not condition:
        raise RuntimeError(repr(evidence))

compiler = str(Path(sys.argv[1]).resolve())
CASES = json.loads(Path(__file__).with_name("value-boundary-rejections.json").read_text())
# IR validation is not execution. Both paths must agree on admission:
# first prove that a compiled guest ACTUALLY runs and returns the expected
# value, then reject unsupported source in both modes without producing IR
# or guest values. A frontend that merely rejects everything is not parity.
with tempfile.TemporaryDirectory(prefix="ores-value-boundaries-") as work:
    source = Path(work) / "main.ores"
    shared_dir = Path(__file__).resolve().parents[1] / "contracts/v1/semantics/fixtures"
    # Distinct Java/LLVM canonical positive controls: accepting only 40+2
    # cannot prove parenthesized precedence lowering is correct.
    for fixture, expected, pinned_blob in (
        ("add.ores", "42", "015f8b0f79aafa796a148d2adeff1574e8d6c491"),
        ("precedence.ores", "40", "2d09e697ad4eeac4ee263c3656b92998c67f0bfc"),
    ):
        data = (shared_dir / fixture).read_bytes()
        actual_blob = hashlib.sha1(
            b"blob " + str(len(data)).encode("ascii") + b"\0" + data
        ).hexdigest()
        require(actual_blob == pinned_blob, "canonical source changed",
                fixture, actual_blob)
        source.write_bytes(data)
        for mode in ("ir", "run"):
            command = [compiler, mode, str(source)]
            control = subprocess.run(command, capture_output=True, text=True, timeout=20)
            require(control.returncode == 0 and control.stderr == "",
                    "positive-control", fixture, mode, control.returncode, control.stderr)
            if mode == "run":
                require(control.stdout.strip() == expected, "guest not executed",
                        fixture, mode, control.stdout)
            else:
                require(control.stdout.strip(), "no compiler IR output", fixture, mode)

    for name, program in CASES.items():
        source.write_text(program)
        for mode in ("ir", "run"):
            result = subprocess.run(
                [compiler, mode, str(source)],
                capture_output=True, text=True, timeout=20)
            require(result.returncode == 1, name, mode,
                    result.returncode, result.stderr)
            require(result.stdout == "", name, mode,
                    "compiler emitted output for rejected source", result.stdout)
            require("oresforge:" in result.stderr, name, mode, result.stderr)
        print(f"{name}: rejected by IR and executable paths, no output")
print(f"{len(CASES)} value boundary rejections passed in both modes")
