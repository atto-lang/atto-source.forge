#!/usr/bin/env python3
"""Real Forge CLI macro-denial gate: no guest, IR, ASM or runtime fallback."""
import argparse
import hashlib
import os
import pathlib
import signal
import subprocess
import tempfile

DECLARATIONS = [
    "define macro identity(Syntax<Expr> value) -> Syntax<Expr> { return value; } "
    "pub fnc main():int{return 42;}",
    "define pub macro literal(Const<int> value) -> Syntax<Expr> "
    "{ return quote(value); } pub fnc main():int{return 42;}",
    "pub define macro twice(Syntax<Expr> value) -> Syntax<Expr> "
    "{ return quote(unquote(value) + unquote(value)); } "
    "pub fnc main():int{return 42;}",
]
CALLS = [
    "pub fnc main():int{return identity!(syntax expr 42);}",
    "pub fnc main():int{return literal!(const 42);}",
    "pub fnc main():int{return absent!(syntax expr 42);}",
]
COMMANDS = [
    ("run",), ("ir",), ("emit-asm", "x86_64"),
    ("run-runtime",), ("ir-runtime",),
    ("emit-runtime-asm", "x86_64"),
    ("run-calls",), ("emit-call-asm", "x86_64"),
]


def compiler_digest(compiler):
    size = compiler.stat().st_size
    if size <= 0 or size > 256 * 1024 * 1024:
        raise AssertionError("Forge compiler binary exceeds 256 MiB execution budget")
    digest = hashlib.sha256()
    with compiler.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def run(compiler, options, source, cwd):
    cmd = [str(compiler), *options, str(source)]
    process = subprocess.Popen(
        cmd, cwd=cwd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        start_new_session=(os.name == "posix"))
    try:
        stdout, stderr = process.communicate(timeout=10)
    except subprocess.TimeoutExpired as error:
        # A timed-out compiler must not leave assembler or child processes
        # alive to publish artifacts after the negative case has finished.
        if os.name == "posix":
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        else:
            process.kill()
        process.communicate()
        raise AssertionError("Forge macro compiler timed out") from error
    if len(stdout) > 8192 or len(stderr) > 8192:
        raise AssertionError("Forge macro compiler emitted unbounded diagnostic or IR")
    return subprocess.CompletedProcess(cmd, process.returncode, stdout, stderr)


def main():
    arg = argparse.ArgumentParser()
    arg.add_argument("--compiler", required=True, type=pathlib.Path)
    opts = arg.parse_args()
    if opts.compiler.is_symlink():
        raise AssertionError("compiler symlink is not an executed exact binary")
    compiler = opts.compiler.resolve(strict=True)
    if not compiler.is_file():
        raise AssertionError("compiler is not a regular file")
    compiler_sha256 = compiler_digest(compiler)
    count = 0
    with tempfile.TemporaryDirectory(prefix="forge-macro-deny-") as folder:
        root = pathlib.Path(folder)
        source = root / "macro-negative.atto"
        for text in DECLARATIONS + CALLS:
            source.write_text(text + "\n", encoding="utf-8")
            for command in COMMANDS:
                before = {p.name for p in root.iterdir()}
                source_bytes = source.read_bytes()
                source_identity = source.stat()
                outcome = run(compiler, command, source, folder)
                after = {p.name for p in root.iterdir()}
                current_identity = source.stat()
                source_changed = (source.is_symlink() or
                    source.read_bytes() != source_bytes or
                    (current_identity.st_dev, current_identity.st_ino) !=
                    (source_identity.st_dev, source_identity.st_ino))
                if (outcome.returncode != 1 or outcome.stdout or
                        b"FORGE_MACRO_EXPANDER_REQUIRED" not in outcome.stderr or
                        len(outcome.stderr) > 8192 or after != before or
                        source_changed):
                    raise AssertionError(
                        f"unexpected macro admission {command}: "
                        f"status={outcome.returncode} stdout={outcome.stdout!r} "
                        f"stderr={outcome.stderr[:256]!r} outputs={after-before}")
                count += 1
        source.write_text(
            "pub fnc main():int{return 42;} // define macro decoy\n",
            encoding="utf-8")
        positive_bytes = source.read_bytes()
        positive_identity = source.stat()
        actual = run(compiler, ("run",), source, folder)
        unchanged = (not source.is_symlink() and
            source.read_bytes() == positive_bytes and
            (source.stat().st_dev, source.stat().st_ino) ==
            (positive_identity.st_dev, positive_identity.st_ino))
        if not unchanged or (actual.returncode, actual.stdout, actual.stderr) != (0, b"42\n", b""):
            raise AssertionError("macro comment decoy modified real scalar guest")
    if compiler_digest(compiler) != compiler_sha256:
        raise AssertionError("Forge compiler executable changed during macro denial tests")
    print(f"PASS Forge real CLI macro fail-closed: {count} negatives, 1 scalar positive; "
          f"compiler_sha256={compiler_sha256}; NOT a macro-guest parity claim")


if __name__ == "__main__":
    main()
