#!/usr/bin/env python3
"""Import admission parity: Forge does not yet link multi-file Oreslang sources."""
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()
cases = [
    'import fnc helper from "+/src/helper";',
    'import fnc helper from "@/src/helper";',
    'import fnc helper from "pkg:math/src/helper";',
    'import module helper from "./helper";',
    'import fnc helper from "../helper";',
    'import fnc helper from "bare/helper";',
    'import fnc helper from "+/src/./helper";',
    'import fnc helper from "+/src//helper";',
    'import fnc helper from "pkg:math/src/../src/helper";',
]
with tempfile.TemporaryDirectory(prefix="oresforge-import-admission-") as tmp:
    src = Path(tmp) / "main.ores"

    def launch(mode):
        return subprocess.run([str(compiler), mode, str(src)],
                              capture_output=True, text=True, timeout=20)

    src.write_text("pub fnc main(): int { return 42; }\n", encoding="utf-8")
    control = launch("run")
    if control.returncode != 0 or control.stdout.strip() != "42":
        raise AssertionError(f"valid guest control did not execute: {control}")
    src.write_text("// import fnc denied from \"+/missing\";\n"
                   "pub fnc main(): int { return 42; }\n", encoding="utf-8")
    comment = launch("run")
    if comment.returncode != 0 or comment.stdout.strip() != "42":
        raise AssertionError("comment-only import was incorrectly rejected")

    for declaration in cases:
        src.write_text(declaration + "\npub fnc main(): int { return 42; }\n",
                       encoding="utf-8")
        result = launch("ir")
        if (result.returncode != 1 or result.stdout or
                "source imports require verified module graph lowering" not in result.stderr):
            raise AssertionError(f"unsupported import admitted or undiagnosed: {declaration}: {result}")
print(f"Forge import parity admission: {len(cases)} negative cases and 2 executed controls passed")
