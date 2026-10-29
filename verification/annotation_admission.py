#!/usr/bin/env python3
"""Run canonical annotation parity cases against the real Forge stage-0 compiler."""
import argparse
import base64
import binascii
from pathlib import Path
import re
import subprocess
import tempfile

if not __debug__:
    raise SystemExit("annotation admission gate forbids python -O")

CORPUS = Path(__file__).resolve().parents[1] / "contracts/v1/annotation-admission-v1.cases"


def load_cases():
    seen, result = set(), []
    for line in CORPUS.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("|")
        assert len(fields) == 7, "invalid annotation corpus row width"
        key = fields[0]
        assert re.fullmatch(r"[a-z][a-z_0-9]*", key) and key not in seen, key
        seen.add(key)
        assert all(x in ("pass", "reject") for x in fields[1:6]), key
        try:
            payload = base64.b64decode(fields[6], validate=True)
            source = payload.decode("utf-8", errors="strict")
        except (ValueError, UnicodeError, binascii.Error) as error:
            raise AssertionError("malformed annotation parity bytes: " + key) from error
        assert base64.b64encode(payload).decode("ascii") == fields[6], key
        result.append((key, source, fields[5]))
    assert len(result) == 17, "incomplete annotation corpus"
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", required=True)
    args = ap.parse_args()
    compiler = str(Path(args.compiler).resolve(strict=True))
    cases = load_cases()
    with tempfile.TemporaryDirectory(prefix="oresforge-annotations-") as tmp:
        for mode in ("run", "run-calls"):
            for key, source, expectation in cases:
                workdir = Path(tmp) / (mode + "_" + key)
                workdir.mkdir()
                input_file = workdir / "input.ores"
                input_file.write_text(source, encoding="utf-8")
                proc = subprocess.run(
                    [compiler, mode, str(input_file)], cwd=workdir,
                    capture_output=True, text=True, timeout=30, check=False)
                artifacts = sorted(str(f.relative_to(workdir))
                                   for f in workdir.rglob("*")
                                   if f.is_file() and f != input_file)
                assert not artifacts, (mode, key, "sidecar artifacts", artifacts)
                if expectation == "reject":
                    assert proc.returncode != 0, (mode, key, "accepted annotation")
                    assert "source annotations require compiler-validated lowering" in proc.stderr, (
                        mode, key, proc.stderr)
                    assert not proc.stdout.strip(), (mode, key, proc.stdout)
                else:
                    assert proc.returncode == 0, (mode, key, proc.stderr)
                    assert proc.stdout.strip() == "42", (mode, key, proc.stdout)
                    assert not proc.stderr.strip(), (mode, key, proc.stderr)
    print("PASS Forge shared annotation corpus: 17 cases x 2 executable compiler modes")


if __name__ == "__main__":
    main()
