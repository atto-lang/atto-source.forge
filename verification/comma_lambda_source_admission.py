#!/usr/bin/env python3
"""Actual Forge stage-0 compiler negative gate for AttoLang comma/lambda syntax.

This is a fail-closed check, NOT closure or expression-body guest parity.
"""
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(compiler: Path, mode: str, source: Path):
    return subprocess.run(
        [str(compiler), mode, str(source)],
        check=False, capture_output=True, timeout=10)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: comma_lambda_source_admission.py FORGE_COMPILER")
    compiler = Path(sys.argv[1]).resolve()
    cases = (
        (
            "comma_scalar",
            "pub fnc main() -> int :: int x = 5, x + 3 end\n",
            b"FORGE_COMMA_BODY_UNSUPPORTED",
        ),
        (
            "typed_inline_lambda",
            "pub fnc main():int{return || => int -> 5;}\n",
            b"lexical captures require checked closure lowering",
        ),
        (
            "parenthesized_type_lambda",
            "pub fnc main():int{return || (=> int) -> 5;}\n",
            b"lexical captures require checked closure lowering",
        ),
        (
            "inferred_inline_comma_lambda",
            "pub fnc main():int{return || -> val int x = 5, x + 3;}\n",
            b"lexical captures require checked closure lowering",
        ),
        (
            "tuple_pipeline_lambda",
            "pub fnc main():int{return 5 |> |x,y| => int -> x+y;}\n",
            b"value pipeline requires named callable lowering",
        ),
        (
            "named_pipeline_without_lowering",
            "pub fnc main():int{return 5 |> plus;}\n",
            b"value pipeline requires named callable lowering",
        ),
    )
    with tempfile.TemporaryDirectory(prefix="forge-comma-lambda-") as dirname:
        source = Path(dirname) / "fixture.atto"
        for identifier, text, expected in cases:
            source.write_text(text, encoding="utf-8")
            for mode in ("run", "ir", "run-runtime", "ir-runtime"):
                actual = invoke(compiler, mode, source)
                if (actual.returncode != 1 or actual.stdout or
                        expected not in actual.stderr):
                    raise AssertionError(
                        f"{identifier}/{mode}: unsafe admission or wrong diagnostic: {actual!r}")
        # Regression: these constructs in comments are not executable tokens.
        source.write_text(
            "pub fnc main():int{return 42;} // || => int -> 5, end\n",
            encoding="utf-8")
        for mode in ("run", "run-runtime"):
            actual = invoke(compiler, mode, source)
            if actual.returncode != 0 or actual.stdout != b"42\n":
                raise AssertionError(f"ordinary .atto scalar guest broken in {mode}: {actual!r}")


if __name__ == "__main__":
    main()
