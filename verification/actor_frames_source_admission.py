#!/usr/bin/env python3
"""Require Forge stage-0 compiler to deny actor source on ALL emission paths.

This is a negative compiler admission check, NOT source actor execution parity.
Every failed invocation must return 1, produce no stdout/IR/assembly, and
report an error instead of aborting or silently interpreting a scalar subset.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ACTORS = {
    "spawn-stop": """define actor Worker as
  pub fnc on_message(int n): int { return n; }
end
pub fnc main(): int {
  const worker = spawn Worker();
  stop worker;
  return 43;
}
""",
    "heap-ownership": """define isolated actor Counter as
  state { count: int = 0; }
  pub on_message(int n): int {
    self.count = self.count + n;
    return self.count;
  }
end
pub fnc main(): int { return 42; }
""",
    "recursive-overflow": """define actor Worker as
  pub fnc recurse(int n): int {
    if n == 0 then return 0; else return recurse(n - 1); fi
  }
end
pub fnc main(): int { return 42; }
""",
}

MODES = [
    ("run",), ("ir",),
    ("emit-asm", "x86_64"),
    ("run-runtime",), ("ir-runtime",),
    ("emit-runtime-asm", "x86_64"),
    ("run-calls",), ("emit-call-asm", "x86_64"),
]


def launch(compiler: Path, source: Path, mode: tuple[str, ...]):
    return subprocess.run(
        [str(compiler), *mode, str(source)],
        capture_output=True, timeout=10, check=False,
    )


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: actor_frames_source_admission.py <oresforge>")
    compiler = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="atto-forge-actor-denial-") as tmp:
        src = Path(tmp) / "entry.atto"
        src.write_text("pub fnc main(): int { return 42; }\n", encoding="utf-8")
        # Guard against a compiler path that merely rejects every input.
        for mode in (("run",), ("ir",)):
            good = launch(compiler, src, mode)
            if good.returncode != 0 or not good.stdout or good.stderr:
                raise AssertionError(f"control rejected in {mode}: {good!r}")
        for label, text in ACTORS.items():
            src.write_text(text, encoding="utf-8")
            for mode in MODES:
                result = launch(compiler, src, mode)
                if result.returncode != 1 or result.stdout or not result.stderr:
                    raise AssertionError(
                        f"Forge published unsupported actor source or crashed: "
                        f"{label} {mode}: {result!r}"
                    )
    print("ATTO_FORGE_ACTOR_SOURCE_DENIED|negative=24|controls=2|guest=NOT_EXECUTED")


if __name__ == "__main__":
    main()
