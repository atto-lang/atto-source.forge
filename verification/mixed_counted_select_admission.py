#!/usr/bin/env python3
"""Actual CLI rejection gate; not positive counted guest parity."""
import hashlib
import pathlib
import subprocess
import sys
import tempfile
binary = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(__file__).resolve().parent / "fixtures" / "select-source-v2"
fixtures = [["counted-mixed-future-alias","8f57de03c9fb44eb5594c99a7095f34b702a266a"],["counted-multi-channel-fair","4b9b79b9b8670f5b282d33e63b7a4b5691c2979a"],["counted-mixed-zero","458323db6019da434b97e5b966ee646411310acf"]]
def run(mode, path):
    args = [str(binary), mode]
    if mode.startswith("emit-"):
        args += ["x86_64"]
    return subprocess.run(args + [str(path)], capture_output=True, timeout=10)
modes = ["check", "run", "ir", "emit-asm", "run-runtime", "ir-runtime", "emit-runtime-asm", "run-calls", "emit-call-asm"]
def deny(path):
    for mode in modes:
        out = run(mode, path)
        assert out.returncode > 0 and not out.stdout and out.stderr, (mode, path, out)
for name, sha in fixtures:
    path = root / (name + ".atto")
    raw = path.read_bytes()
    assert hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest() == sha
    deny(path)
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp) / "fixture.atto"
    for prefix in ["select", "nb select", "await select", "await nb select"]:
        for header in ["(0)", "<int>(3)", "(count)", "(true)"]:
            path.write_text(f"fnc main():int{{return {prefix}{header} over when readch c: int n -> {{return n;}} end;}}")
            deny(path)
    path.write_text("fnc main():int{return 42;}\n")
    out = run("run", path)
    assert out.returncode == 0 and out.stdout.strip() == b"42", out
    out = run("emit-asm", path)
    assert out.returncode == 0 and out.stdout, out
print("FORGE_MIXED_COUNTED_ADMISSION_PASS exact=3 isolated=16 modes=9 scalar_control=42; NOT counted guest")
