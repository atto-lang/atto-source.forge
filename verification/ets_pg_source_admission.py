#!/usr/bin/env python3
"""Fail-closed Forge ETS/pg guest-source test (real compiler executable)."""
from pathlib import Path
import subprocess
import sys
import tempfile

def main() -> None:
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="oresforge-ets-pg-") as tmp:
        source = Path(tmp) / "fixture.ores"
        for expression in [
            "ets.new(2)", "ets.lookup(1,2)", "ets.compare_exchange_versioned(1,2,3)",
            "pg.broadcast(1)", "pg.join(1,2)", "pg /* trivia */ . leave(1,2)"
        ]:
            source.write_text("pub fnc main():int{return " + expression + ";}\n", encoding="utf-8")
            for mode in ("run", "ir", "run-runtime", "ir-runtime"):
                out = subprocess.run([str(compiler), mode, str(source)],
                                     capture_output=True, timeout=10, check=False)
                if out.returncode != 1 or out.stdout or (
                    b"Forge ETS/pg guest APIs require authenticated isolate" not in out.stderr
                ):
                    raise AssertionError(f"{mode} failed closed incorrectly: {expression} {out!r}")
        # Words in comments must not trigger the capability rule.
        source.write_text(
            "pub fnc main():int{return 42;} // ets.lookup pg.broadcast\n",
            encoding="utf-8")
        for mode in ("run", "run-runtime"):
            out = subprocess.run([str(compiler), mode, str(source)],
                                 capture_output=True, timeout=10, check=False)
            if out.returncode != 0 or out.stdout != b"42\n":
                raise AssertionError(f"{mode}: harmless comments changed semantics: {out!r}")

if __name__ == "__main__":
    main()
