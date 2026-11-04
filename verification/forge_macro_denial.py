#!/usr/bin/env python3
"""Real Forge CLI macro-denial gate: no guest, IR, ASM or runtime fallback."""
import argparse
import pathlib
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
]


def run(compiler, options, source, cwd):
    cmd = [str(compiler), *options, str(source)]
    return subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=10, check=False)


def main():
    arg = argparse.ArgumentParser()
    arg.add_argument("--compiler", required=True, type=pathlib.Path)
    opts = arg.parse_args()
    if opts.compiler.is_symlink():
        raise AssertionError("compiler symlink is not an executed exact binary")
    compiler = opts.compiler.resolve(strict=True)
    if not compiler.is_file():
        raise AssertionError("compiler is not a regular file")
    count = 0
    with tempfile.TemporaryDirectory(prefix="forge-macro-deny-") as folder:
        root = pathlib.Path(folder)
        source = root / "macro-negative.atto"
        for text in DECLARATIONS + CALLS:
            source.write_text(text + "\n", encoding="utf-8")
            for command in COMMANDS:
                before = {p.name for p in root.iterdir()}
                outcome = run(compiler, command, source, folder)
                after = {p.name for p in root.iterdir()}
                if (outcome.returncode != 1 or outcome.stdout or
                        b"FORGE_MACRO_EXPANDER_REQUIRED" not in outcome.stderr or
                        len(outcome.stderr) > 8192 or after != before):
                    raise AssertionError(
                        f"unexpected macro admission {command}: "
                        f"status={outcome.returncode} stdout={outcome.stdout!r} "
                        f"stderr={outcome.stderr[:256]!r} outputs={after-before}")
                count += 1
        source.write_text(
            "pub fnc main():int{return 42;} // define macro decoy\n",
            encoding="utf-8")
        actual = run(compiler, ("run",), source, folder)
        if (actual.returncode, actual.stdout, actual.stderr) != (0, b"42\n", b""):
            raise AssertionError("macro comment decoy modified real scalar guest")
    print(f"PASS Forge real CLI macro fail-closed: {count} negatives, 1 scalar positive; "
          "NOT a macro-guest parity claim")


if __name__ == "__main__":
    main()
