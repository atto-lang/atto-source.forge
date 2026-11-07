#!/usr/bin/env python3
"""Real OresForge CLI input-budget regression (not a parser-only unit test)."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile


def run(executable: str, path: Path):
    return subprocess.run(
        [executable, "ir", str(path)],
        capture_output=True, text=True, timeout=12, check=False,
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: cli_source_budget.py <oresforge>")
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="oresforge-budget-") as temp:
        good = Path(temp) / "good.atto"
        good.write_text("define pub fnc main() : int { return 2+3*4; }")
        control = run(executable, good)
        if control.returncode != 0 or "ret %" not in control.stdout:
            raise SystemExit("FAIL: executable did not compile a valid control")
        if os.name == "posix":
            fifo = Path(temp) / "unwritten-fifo.atto"
            os.mkfifo(fifo)
            linked_fifo = Path(temp) / "fifo-link.atto"
            linked_fifo.symlink_to(fifo)
            linked_good = Path(temp) / "regular-link.atto"
            linked_good.symlink_to(good)
            for flags in (["run"], ["ir"], ["emit-asm", "x86_64"]):
                for special in (fifo, linked_fifo, Path(temp), type(good)("/dev/zero")):
                    rejected = subprocess.run([executable, *flags, str(special)],
                                              capture_output=True, text=True, timeout=3)
                    if (rejected.returncode != 1 or rejected.stdout
                            or "source must be a regular file" not in rejected.stderr):
                        raise SystemExit("FAIL: nonregular source admission: " + str(special))
                accepted = subprocess.run([executable, *flags, str(linked_good)],
                                          capture_output=True, text=True, timeout=3)
                if accepted.returncode != 0 or not accepted.stdout:
                    raise SystemExit("FAIL: regular source symlink rejected")
        # Publication failures must not report a successful compilation.
        for command in (["run", str(good)], ["ir", str(good)],
                        ["emit-asm", "x86_64", str(good)]):
            with good.open("rb") as readonly_stdout:
                failed = subprocess.run([executable, *command], stdout=readonly_stdout,
                                        stderr=subprocess.PIPE, text=True, timeout=12)
            if failed.returncode == 0:
                raise SystemExit("FAIL: unwritable stdout reported success")
        # Exactly the source-byte limit must reach parsing, not fail admission.
        exact = Path(temp) / "exact-limit.atto"
        exact.write_bytes(b" " * (1024 * 1024))
        at_limit = run(executable, exact)
        if "source exceeds 1 MiB bootstrap limit" in at_limit.stderr:
            raise SystemExit("FAIL: exact 1 MiB source rejected by byte budget")
        bad = Path(temp) / "too-large.atto"
        bad.write_bytes(b" " * (1024 * 1024 + 1))
        rejected = run(executable, bad)
        if rejected.returncode == 0 or rejected.stdout:
            raise SystemExit("FAIL: oversized source was accepted or emitted IR")
        if "source exceeds 1 MiB bootstrap limit" not in rejected.stderr:
            raise SystemExit("FAIL: rejection did not originate from the byte budget")
        for label, payload, diagnostic in (
            ("overlong", b"// " + bytes([0xC0, 0xAF]), "not valid UTF-8"),
            ("surrogate", b"// " + bytes([0xED, 0xA0, 0x80]), "not valid UTF-8"),
            ("truncated", b"// " + bytes([0xE2, 0x82]), "not valid UTF-8"),
            ("out-of-range", b"// " + bytes([0xF4, 0x90, 0x80, 0x80]), "not valid UTF-8"),
            ("stray-continuation", b"// " + bytes([0x80]), "not valid UTF-8"),
            ("nul", b"// " + bytes([0x00]), "contains NUL byte"),
        ):
            bad_encoding = Path(temp) / (label + ".atto")
            bad_encoding.write_bytes(payload)
            invalid = run(executable, bad_encoding)
            if invalid.returncode == 0 or invalid.stdout:
                raise SystemExit(f"FAIL: {label} malformed source accepted or emitted IR")
            if diagnostic not in invalid.stderr:
                raise SystemExit(f"FAIL: {label} bypassed encoding admission")
    print("Forge CLI source input budget enforced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
