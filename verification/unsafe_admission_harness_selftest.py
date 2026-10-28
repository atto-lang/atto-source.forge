#!/usr/bin/env python3
"""Source-free oracle mutation tests for the Forge G0 admission runner.

This verifies the *test harness*, not compiler execution or native AOT.
"""
import contextlib
import hashlib
import importlib.util
import io
import pathlib
import re
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE / "unsafe_admission.py"
EXPECTED_BLOB = "a0e87636a01fde8fc1fff28ef542e3dbd3e068d6"
raw = SOURCE.read_bytes()
actual_blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
assert actual_blob == EXPECTED_BLOB, (actual_blob, EXPECTED_BLOB)
spec = importlib.util.spec_from_file_location("forge_unsafe_g0", SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
real_run = module.subprocess.run


def oracle(args, *_, fault=None, counts=None, **kwargs):
    raw = pathlib.Path(args[-1]).read_bytes()
    mode = args[1]
    bad_bytes = any(x in raw for x in (b"\xff", b"\xc0\xaf", b"\xf4\x90\x80\x80", b"\x00"))
    match = re.search(rb"(?:/\*){100,}", raw)
    nested = len(match.group()) // 2 if match else 0
    too_deep = nested == 129
    harmless = not bad_bytes and not too_deep and (
        nested == 128 or b"unsafe must stay comment" in raw or
        (b"/* outer /* unsafe */ outer */" in raw and not raw.rstrip().endswith(b"unsafe"))
    )
    if bad_bytes:
        kind = "malformed"
        diagnostic = "source contains NUL byte" if b"\x00" in raw else "source is not valid UTF-8"
        reply = subprocess.CompletedProcess(args, 1, "", "oresforge: " + diagnostic)
    elif too_deep:
        kind = "overdepth"
        reply = subprocess.CompletedProcess(args, 1, "", "block comment nesting too deep")
    elif harmless:
        kind = "safe"
        reply = subprocess.CompletedProcess(args, 0, "42\n", "")
    else:
        kind = "unsafe"
        reply = subprocess.CompletedProcess(args, 1, "", "unsafe boundary unsupported: effects required")
    counts.append(kind)
    if fault == "emitted-ir" and kind == "malformed" and mode == "ir":
        return subprocess.CompletedProcess(args, 0, "IR", "")
    if fault == "emitted-asm" and kind == "malformed" and mode == "emit-asm":
        return subprocess.CompletedProcess(args, 0, ".text", "")
    if fault == "depth128" and kind == "safe" and nested == 128:
        return subprocess.CompletedProcess(args, 1, "", "unexpectedly rejected")
    if fault == "depth129" and kind == "overdepth":
        return subprocess.CompletedProcess(args, 0, "42\n", "")
    if fault == "unsafe-accepted" and kind == "unsafe":
        return subprocess.CompletedProcess(args, 0, "42\n", "")
    if fault == "masked-diagnostic" and kind == "malformed":
        return subprocess.CompletedProcess(args, 1, "", "generic error")
    return reply


def run_with_fault(fault=None):
    counts = []
    module.subprocess.run = lambda *args, **kwargs: oracle(*args, counts=counts, fault=fault, **kwargs)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            module.main("/fake/oresforge")
        return True, counts
    except AssertionError:
        return False, counts
    finally:
        module.subprocess.run = real_run


if __name__ == "__main__":
    ok, observed = run_with_fault()
    assert ok, "baseline oracle unexpectedly fails"
    expected = {"unsafe": 80, "safe": 6, "overdepth": 2, "malformed": 32}
    actual = {kind: observed.count(kind) for kind in expected}
    assert len(observed) == 120 and actual == expected, (len(observed), actual)
    for mutation in ("emitted-ir", "emitted-asm", "depth128", "depth129", "unsafe-accepted", "masked-diagnostic"):
        passed, _ = run_with_fault(mutation)
        assert not passed, "undetected deliberate failure: " + mutation
    print("PASS: Forge G0 source-free harness selftest (120-path oracle + 6 caught mutations)")
