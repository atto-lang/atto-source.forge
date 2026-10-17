#!/usr/bin/env python3
"""Strict import rejection/positive-control checks for Forge bootstrap.

Classifier self-tests are source-free and DO NOT prove compiler behavior.
The default mode executes the exact native Forge binary supplied by CTest.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

MAX_OUTPUT_BYTES = 4096
EXPECTED_REJECTION = b"oresforge: source imports require verified module graph lowering at byte 0\n"
CASES = (
    'import fnc helper from "+/src/helper";',
    'import fnc helper from "@/src/helper";',
    'import fnc helper from "pkg:math/src/helper";',
    'import module helper from "./helper";',
    'import fnc helper from "../helper";',
    'import fnc helper from "bare/helper";',
    'import fnc helper from "+/src/./helper";',
    'import fnc helper from "+/src//helper";',
    'import fnc helper from "pkg:math/src/../src/helper";',
)


def accepts_control(returncode, stdout, stderr):
    # Exactly one canonical decimal result; never accept extra lines or errors.
    return returncode == 0 and stdout == b"42\n" and stderr == b""


def rejects_import(returncode, stdout, stderr):
    # Byte zero is the actual lexer location for each leading import case.
    # Mere diagnostic substrings, crashes and partial IR are not conformance.
    return returncode == 1 and stdout == b"" and stderr == EXPECTED_REJECTION


def launch(compiler, mode, source):
    # Spool to disk and reject excessive output without capturing it in RAM.
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        try:
            proc = subprocess.run(
                [str(compiler), mode, str(source)],
                stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                timeout=8, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise AssertionError("compiler execution infrastructure failure") from exc
        if out.tell() > MAX_OUTPUT_BYTES or err.tell() > MAX_OUTPUT_BYTES:
            raise AssertionError("compiler produced excessive output")
        out.seek(0)
        err.seek(0)
        return proc.returncode, out.read(), err.read()


def classifier_selftest():
    good = (0, b"42\n", b"")
    if not accepts_control(*good):
        raise AssertionError("valid scalar control rejected")
    bad_control = (
        (1, b"42\n", b""), (0, b"42\n", b"warning\n"),
        (0, b" 42\n", b""), (0, b"42\n\n", b""),
        (0, b"42", b""), (0, b"42\r\n", b""),
    )
    for sample in bad_control:
        if accepts_control(*sample):
            raise AssertionError("false-positive scalar control")
    good_reject = (1, b"", EXPECTED_REJECTION)
    if not rejects_import(*good_reject):
        raise AssertionError("valid compiler-owned rejection denied")
    bad_rejections = (
        (0, b"", EXPECTED_REJECTION),
        (-11, b"", EXPECTED_REJECTION),
        (2, b"", EXPECTED_REJECTION),
        (1, b"partial IR\n", EXPECTED_REJECTION),
        (1, b"", b""),
        (1, b"", EXPECTED_REJECTION + b"unrelated error\n"),
        (1, b"", b"setup failure: " + EXPECTED_REJECTION),
        (1, b"", EXPECTED_REJECTION.replace(b"byte 0", b"byte 99")),
        (1, b"", EXPECTED_REJECTION.replace(b"oresforge:", b"other:")),
        (1, b"", EXPECTED_REJECTION.replace(b"\n", b"\r\n")),
    )
    for sample in bad_rejections:
        if rejects_import(*sample):
            raise AssertionError("false-positive unsupported import rejection")
    print("Forge source-free import classifier: 18 cases passed")


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        classifier_selftest()
        return 0
    if len(argv) != 2:
        print("usage: import_parity_admission.py COMPILER | --selftest", file=sys.stderr)
        return 2
    compiler = Path(argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="oresforge-import-admission-") as tmp:
        source = Path(tmp) / "main.ores"
        source.write_text("pub fnc main(): int { return 42; }\n", encoding="utf-8")
        if not accepts_control(*launch(compiler, "run", source)):
            raise AssertionError("valid guest control did not execute cleanly")
        source.write_text(
            '// import fnc denied from "+/missing";\n'
            'pub fnc main(): int { return 42; }\n', encoding="utf-8"
        )
        if not accepts_control(*launch(compiler, "run", source)):
            raise AssertionError("comment-only import was incorrectly rejected")
        for declaration in CASES:
            source.write_text(declaration + "\npub fnc main(): int { return 42; }\n",
                              encoding="utf-8")
            result = launch(compiler, "ir", source)
            if not rejects_import(*result):
                raise AssertionError("wrong import rejection: " + declaration + " " + repr(result))
    print(f"Forge import parity admission: {len(CASES)} rejects, 2 executed controls passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
