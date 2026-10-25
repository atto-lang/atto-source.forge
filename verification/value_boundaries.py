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
TASK_V2 = json.loads(
    (Path(__file__).resolve().parents[1] / "contracts/v2/cpu-gpu-task-unsupported-v2.json").read_text())
require(len(TASK_V2) == 10, "CPU/GPU task v2 corpus must contain exactly 10 cases")
require(not (set(CASES) & set(TASK_V2)), "v2 case IDs must not overwrite local rejection cases")
CASES = {**CASES, **TASK_V2}
MODE = ['ir']
with tempfile.TemporaryDirectory(prefix="ores-value-boundaries-") as work:
    source = Path(work) / "main.ores"
    source.write_text("pub fnc main():int{return (5+3)*7-14;}")
    command = [compiler, *MODE, str(source)]
    control = subprocess.run(command, capture_output=True, text=True, timeout=20)
    require(control.returncode == 0 and control.stdout.strip(), control.stderr)
    for name, program in CASES.items():
        source.write_text(program)
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        require(result.returncode == 1, name, result.returncode, result.stderr)
        require(result.stdout == "", name, "published code for rejected source", result.stdout)
        require('oresforge:' in result.stderr, name, result.stderr)
        print(f"{name}: rejected with no code output")
print(f"{len(CASES)} value boundary rejections passed")
