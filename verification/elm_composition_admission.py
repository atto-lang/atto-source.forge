#!/usr/bin/env python3
"""OresForge guest source admission, not a native reactive runtime claim."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

def require(cond, evidence):
    if not cond:
        raise AssertionError(evidence)

compiler = str(Path(sys.argv[1]).resolve())
cases = json.loads(Path(__file__).with_name("elm_composition_rejections.json").read_text())
require(len(cases) == 16, "exact sixteen composition/failure admission cases must be exercised")
with tempfile.TemporaryDirectory(prefix="oresforge-elm-") as root:
    path = Path(root) / "input.ores"
    path.write_text("pub fnc main():int{return (5+3)*7-14;}", encoding="utf-8")
    command = [compiler, "ir", str(path)]
    control = subprocess.run(command, capture_output=True, text=True, timeout=20)
    require(control.returncode == 0 and bool(control.stdout.strip()),
            ("scalar control must produce IR", control.returncode, control.stderr))
    for name, source in cases.items():
        require(name and "_" in name, ("malformed case name", name))
        path.write_text(source, encoding="utf-8")
        p = subprocess.run(command, capture_output=True, text=True, timeout=20)
        require(p.returncode == 1, (name, "unexpected exit", p.returncode, p.stderr))
        require(p.stdout == "", (name, "unsupported source leaked IR", p.stdout[:256]))
        require("oresforge:" in p.stderr, (name, "missing diagnostic", p.stderr))
        print(f"{name}: properly rejected")
