#!/usr/bin/env python3
"""Source-free mutation check for unsafe stdlib CLI harness (not guest execution)."""
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess

SOURCE = Path(__file__).with_name("unsafe_stdlib_import_admission.py")
EXPECTED_BLOB = "131070647409cd81cc13481ec156251c1df74f9d"
raw = SOURCE.read_bytes()
blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
assert blob == EXPECTED_BLOB, f"unaudited unsafe import harness blob {blob}"

spec = importlib.util.spec_from_file_location("forge_unsafe_stdlib_harness", SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
real_run = module.subprocess.run

EXPECTED_MODES = {
    ("run",), ("ir",), ("run-runtime",), ("ir-runtime",),
    ("emit-asm", "x86_64"), ("emit-runtime-asm", "x86_64"),
    ("run-calls",), ("emit-call-asm", "x86_64"),
}
assert set(module.MODES) == EXPECTED_MODES and len(module.MODES) == 8
assert len(module.PATHS) == 11 and len(set(module.PATHS)) == 11


def check(fault=None):
    seen = []

    def oracle(args, **kwargs):
        assert kwargs == {"capture_output": True, "text": True, "timeout": 20}
        assert args[0] == "/fake/oresforge"
        mode = tuple(args[1:-1])
        source = Path(args[-1]).read_text(encoding="utf-8")
        assert source.startswith("import module ") and 'pub fnc main()' in source
        seen.append((mode, source))
        rc, out, err = 1, "", "oresforge: source imports require verified module graph lowering"
        if fault == "accepted" and len(seen) == 1:
            rc = 0
        if fault == "emitted-ir" and mode == ("ir",):
            out = "define i64 @main()"
        if fault == "emitted-asm" and mode == ("emit-asm", "x86_64"):
            out = ".text\n"
        if fault == "masked-diagnostic" and mode == ("run-calls",):
            err = "oresforge: parser error"
        return subprocess.CompletedProcess(args, rc, out, err)

    module.subprocess.run = oracle
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            module.main("/fake/oresforge")
        return True, seen
    except AssertionError:
        return False, seen
    finally:
        module.subprocess.run = real_run


if __name__ == "__main__":
    ok, seen = check()
    assert ok and len(seen) == 176 and len(set(seen)) == 176, (
        "baseline must exercise 11 paths x 2 declarations x 8 CLI modes")
    for fault in ("accepted", "emitted-ir", "emitted-asm", "masked-diagnostic"):
        ok, _ = check(fault)
        assert not ok, "uncaught deliberate admission regression: " + fault
    print("PASS: Forge unsafe stdlib harness, 176 distinct simulated paths + 4 caught mutations")
