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
    "late-unreachable-actor": """pub fnc main(): int { return 42; }
define actor Unreachable as
  pub fnc skipped(): int { return 0; }
end
""",
    "virtual-actor-key": """virtual actor Durable as
  pub fnc on_message(int n): int { return n; }
end
pub fnc main(): int { return 42; }
""",
    "actor-group-pubsub": """define actor Publisher as
  pub fnc broadcast(int n): int {
    const group = actor.group("examples");
    return n;
  }
end
pub fnc main(): int { return 42; }
""",
    "critical-suspends": """define actor Guarded as
  critical fnc update(int n): int { return n; }
  suspends fnc schedule(): int { return 1; }
end
pub fnc main(): int { return 42; }
""",
    "actor-local-storage": """define actor TaskHolder as
  state { task_local_count: int = 0; }
  pub fnc run(): int { return task_local_count; }
end
pub fnc main(): int { return 42; }
""",
    "actor-state-generic": """define actor Container as
  state { cache: Box<int>; }
  pub fnc read(): int { return 0; }
end
pub fnc main(): int { return 42; }
""",
    "actor-supervisor-restart": """define actor Child as
  pub fnc on_message(int n): int { return n; }
end
pub fnc main(): int {
  const ref = spawn Child();
  supervisor.restart(ref);
  return 42;
}
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
        # Guard against a compiler that simply rejects every input. The
        # second valid source must ignore the word actor inside comments.
        controls = (
            "pub fnc main(): int { return 42; }\n",
            "// define actor Ignored as\n"
            "/* virtual actor Nested /* actor */ end */\n"
            "pub fnc main(): int { return 42; }\n",
        )
        for index, source in enumerate(controls):
            src.write_text(source, encoding="utf-8")
            for mode in (("run",), ("ir",)):
                good = launch(compiler, src, mode)
                if good.returncode != 0 or not good.stdout or good.stderr:
                    raise AssertionError(f"control {index} rejected in {mode}: {good!r}")
                if mode == ("run",) and good.stdout != b"42\n":
                    raise AssertionError(f"scalar result changed in control {index}: {good!r}")
        for label, text in ACTORS.items():
            src.write_text(text, encoding="utf-8")
            for mode in MODES:
                result = launch(compiler, src, mode)
                if result.returncode != 1 or result.stdout or not result.stderr:
                    raise AssertionError(
                        f"Forge published unsupported actor source or crashed: "
                        f"{label} {mode}: {result!r}"
                    )
                # Only the scalar/runtime frontends share the actor-specific
                # lexer admission. The separate callable parser may diagnose
                # the construct differently but must still fail closed.
                if mode[0] not in {"run-calls", "emit-call-asm"}:
                    if label == "virtual-actor-key":
                        expected = b"VIRTUAL_ACTOR_BACKEND_UNSUPPORTED"
                    else:
                        expected = b"class/actor methods require typed native lowering"
                    if expected not in result.stderr:
                        raise AssertionError(
                            f"wrong actor denial diagnostic: {label} {mode}: {result!r}"
                        )
    print("ATTO_FORGE_ACTOR_SOURCE_DENIED|negative=80|controls=4|guest=NOT_EXECUTED")


if __name__ == "__main__":
    main()
