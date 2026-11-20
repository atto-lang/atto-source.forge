#!/usr/bin/env python3
"""Real Forge compiler admission: unsupported refinements must fail closed."""
from __future__ import annotations
import argparse
import pathlib
import subprocess
import tempfile

CONTROL = "fnc main(): int { return 42; }\n"
UNSUPPORTED = {
    "annotation": "@Size(min=1,max=32)\ntype Username = string;\nfnc main(): int { return 42; }\n",
    "with": "type NonEmpty = string with length gt 0;\nfnc main(): int { return 42; }\n",
    "where": "type NonBlank = string where it.trim().length > 0;\nfnc main(): int { return 42; }\n",
    "newtype": "define newtype Username(string) with length gte 1;\nfnc main(): int { return 42; }\n",
    "port-keyword": "type Port = int with value gte 1 and value lte 65535;\nfnc main(): int { return 42; }\n",
    "port-symbol": "type Port = int with value >= 1 and value <= 65535;\nfnc main(): int { return 42; }\n",
    "percentage-keyword": "type Percentage = int with value gte 0 and value lte 100;\nfnc main(): int { return 42; }\n",
    "percentage-symbol": "type Percentage = int with value >= 0 and value <= 100;\nfnc main(): int { return 42; }\n",
    "legacy-ge": "type Legacy = int with value ge 1;\nfnc main(): int { return 42; }\n",
    "legacy-le": "type Legacy = int with value le 10;\nfnc main(): int { return 42; }\n",
    "invalid-with": "type T = string with length gt 0 or length eq 10;\nfnc main(): int { return 42; }\n",
}

def run(compiler: pathlib.Path, source: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(compiler), "ir", str(source)], capture_output=True,
                          text=True, timeout=30, check=False)

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", type=pathlib.Path, required=True)
    args = ap.parse_args()
    compiler = args.compiler.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="oresforge-refinement-") as tmp:
        root = pathlib.Path(tmp)
        control = root / "control.ores"
        control.write_text(CONTROL, encoding="utf-8")
        ok = run(compiler, control)
        if ok.returncode != 0 or not ok.stdout.strip():
            raise AssertionError("Forge positive scalar control failed")
        for name, source in UNSUPPORTED.items():
            path = root / f"{name}.ores"
            path.write_text(source, encoding="utf-8")
            result = run(compiler, path)
            if result.returncode == 0:
                raise AssertionError(f"{name}: unsupported refinement compiled")
            if result.stdout.strip():
                raise AssertionError(f"{name}: rejection leaked partial IR/stdout")
            if not result.stderr.strip():
                raise AssertionError(f"{name}: rejection had no diagnostic")
        print(f"Forge refinement fail-closed admission: {len(UNSUPPORTED)} negatives + control")

if __name__ == "__main__":
    main()

