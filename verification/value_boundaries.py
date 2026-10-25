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
DOMAIN_CASES = json.loads(
    (Path(__file__).resolve().parents[1] / "contracts/v1/cpu-gpu-nb-unsupported-v1.json").read_text())
TASK_V2 = json.loads(
    (Path(__file__).resolve().parents[1] / "contracts/v2/cpu-gpu-task-unsupported-v2.json").read_text())
require(len(CASES) == 15, "incomplete preexisting value-boundary corpus")
require(len(DOMAIN_CASES) == 10, "incomplete shared CPU/GPU/nb domain corpus")
require(len(TASK_V2) == 10, "incomplete CPU/GPU task v2 corpus")
# Stable IDs are local to each manifest. Keep all suites without dictionary
# union, so a shared spelling cannot silently overwrite an older fixture.
ALL_CASES = [(suite, name, body) for suite, cases in (
    ("value", CASES), ("domain-v1", DOMAIN_CASES), ("task-v2", TASK_V2))
    for name, body in cases.items()]
require(len(ALL_CASES) == 35, "incomplete combined admission inventory")
MODE = ['ir']
with tempfile.TemporaryDirectory(prefix="ores-value-boundaries-") as work:
    source = Path(work) / "main.ores"
    source.write_text("pub fnc main():int{return (5+3)*7-14;}")
    command = [compiler, *MODE, str(source)]
    control = subprocess.run(command, capture_output=True, text=True, timeout=20)
    require(control.returncode == 0 and control.stdout.strip(), control.stderr)
    for suite, name, program in ALL_CASES:
        source.write_text(program)
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        require(result.returncode == 1, name, result.returncode, result.stderr)
        require(result.stdout == "", name, "published code for rejected source", result.stdout)
        require('oresforge:' in result.stderr, name, result.stderr)
        if suite == "domain-v1":
            require("oresforge:" in result.stderr and
                    "unsupported execution domain" in result.stderr and
                    "no implicit CPU fallback" in result.stderr,
                    name, "generic parser failure is not domain rejection", result.stderr)
        print(f"{suite}/{name}: rejected with no code output")
print(f"{len(ALL_CASES)} combined value, CPU/GPU/nb and task-v2 rejections passed")
