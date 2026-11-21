#!/usr/bin/env python3
"""Actual Forge CLI grammar acceptance/rejection for both formal orders.

Exercises the stage-0 source interpreter and the separate native-call
compiler grammar. This does NOT certify optional arguments or native AOT.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def execute(compiler, mode, path):
    return subprocess.run([compiler, mode, str(path)], capture_output=True,
                          text=True, check=False, timeout=40)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()

    positive = [
        ("typefirst", "fnc add(int x,int y):int{return x+y;}"
                      "pub fnc main():int{return add(20,22);}"),
        ("namefirst", "fnc add(x:int,y:int):int{return x+y;}"
                      "pub fnc main():int{return add(20,22);}"),
        ("mixed", "fnc add(int x,y:int):int{return x+y;}"
                  "pub fnc main():int{return add(20,22);}"),
        ("reverse_mixed", "fnc add(x:int,int y):int{return x+y;}"
                          "pub fnc main():int{return add(20,22);}"),
    ]
    rejected = [
        ("typefirst_option", "fnc add(int? x):int{return 42;}",
         "E_OPTION_TYPE_ABI_UNSUPPORTED"),
        ("namefirst_option", "fnc add(x:int?):int{return 42;}",
         "E_OPTION_TYPE_ABI_UNSUPPORTED"),
        ("typefirst_omission", "fnc add(int x?):int{return 42;}",
         "E_OPTIONAL_FORMAL_ABI_UNSUPPORTED"),
        ("namefirst_omission", "fnc add(x?:int):int{return 42;}",
         "E_OPTIONAL_FORMAL_ABI_UNSUPPORTED"),
        ("namefirst_default", "fnc add(x?:int=10):int{return x;}",
         "E_OPTIONAL_FORMAL_ABI_UNSUPPORTED"),
        ("namefirst_optional_option", "fnc add(x?:int?):int{return 42;}",
         "E_OPTIONAL_FORMAL_ABI_UNSUPPORTED"),
        ("namefirst_bad_default", "fnc add(x:int=10):int{return x;}",
         "E_PARAMETER_DEFAULT_REQUIRES_NAME_QUESTION"),
        ("typefirst_bad_default", "fnc add(int x=10):int{return x;}",
         "E_PARAMETER_DEFAULT_REQUIRES_NAME_QUESTION"),
        ("unreachable_type_option",
         "fnc dead(int? x):int{return 1;} fnc main():int{return 42;}",
         "E_OPTION_TYPE_ABI_UNSUPPORTED"),
        ("unreachable_name_omission",
         "fnc dead(x?:int):int{return 1;} fnc main():int{return 42;}",
         "E_OPTIONAL_FORMAL_ABI_UNSUPPORTED"),
    ]

    with tempfile.TemporaryDirectory(prefix="atto-forge-formal-order-") as td:
        root = Path(td)
        for mode in ("run", "run-calls"):
            for label, text in positive:
                source = root / (mode + "_" + label + ".atto")
                source.write_text(text, encoding="utf-8")
                result = execute(args.compiler, mode, source)
                check(result.returncode == 0 and result.stdout == "42\n"
                      and not result.stderr,
                      f"{mode} {label} failed executable source: {result}")
            for label, text, expected in rejected:
                source = root / (mode + "_" + label + ".atto")
                source.write_text(text, encoding="utf-8")
                result = execute(args.compiler, mode, source)
                check(result.returncode == 1 and not result.stdout and
                      expected in result.stderr,
                      f"{mode} {label} escaped compiler fail-closed diagnostic: {result}")

    print(f"PASS {len(positive)*2} Forge source-interpreter/call-compiler controls and"
          f" {len(rejected)*2} rejection cases; optional/default ABIs remain unsupported")


if __name__ == "__main__":
    main()
