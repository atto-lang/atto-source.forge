#!/usr/bin/env python3
"""Unsupported source features must fail with a diagnostic and no code output.

The arithmetic control executes first, preventing a broken compiler from
passing a rejection-only suite. This is an admission test, not feature parity.
"""
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
    source.write_text("pub fnc main():int{return (5+3)*7-14;}")
    for mode in ("ir", "run"):
        command = [compiler, mode, str(source)]
        control = subprocess.run(command, capture_output=True, text=True, timeout=20)
        require(control.returncode == 0 and control.stderr == "",
                "positive-control", mode, control.returncode, control.stderr)
        if mode == "run":
            require(control.stdout.strip() == "42", "guest not executed",
                    mode, control.stdout)
        else:
            require(control.stdout.strip(), "no compiler IR output", mode)

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
