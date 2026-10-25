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

    for suite, name, program in ALL_CASES:
        source.write_text(program)
        for mode in ("ir", "run"):
            result = subprocess.run([compiler, mode, str(source)],
                capture_output=True, text=True, timeout=20)
            require(result.returncode == 1, suite, name, mode, result.returncode, result.stderr)
            require(result.stdout == "", suite, name, mode, "published output for rejected source", result.stdout)
            require("oresforge:" in result.stderr, suite, name, mode, result.stderr)
            if suite == "domain-v1":
                require("unsupported execution domain" in result.stderr and
                        "no implicit CPU fallback" in result.stderr,
                        name, mode, "generic parser failure is not domain rejection", result.stderr)
        print(f"{suite}/{name}: rejected in IR and run modes without output")
print(f"{len(ALL_CASES)} combined value, CPU/GPU/nb and Task-v2 rejections passed in both modes")
