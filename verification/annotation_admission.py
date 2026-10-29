#!/usr/bin/env python3
"""Real Forge compiler annotation rejection, no IR or executable on failure."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def invoke(binary, command, source):
    return subprocess.run([binary, command, str(source)],
                          capture_output=True, text=True, timeout=30)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", required=True)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="oresforge-annotations-") as tmp:
        file = Path(tmp) / "input.ores"
        for mode in ("run", "run-calls"):
            for annotation in (
                    "@Ret(int)", "@Critical(1)", "@FromJson(\"id\")",
                    "@RequiresPermission(\"ffi\")", "@UnknownMacro"):
                file.write_text(annotation + "\nfnc main():int{return 42;}\n",
                                encoding="utf-8")
                result = invoke(args.compiler, mode, file)
                assert result.returncode != 0, (mode, annotation, result)
                assert "source annotations require compiler-validated lowering" in result.stderr, (
                    mode, annotation, result.stderr)
                assert not result.stdout.strip(), (mode, annotation, result.stdout)
            file.write_text("fnc main():int{return 42;} // @Critical(1)\n",
                            encoding="utf-8")
            result = invoke(args.compiler, mode, file)
            assert result.returncode == 0, (mode, result.stderr)
    print("PASS Forge annotation source admission for scalar and native-call paths")

if __name__ == "__main__":
    main()
