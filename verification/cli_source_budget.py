#!/usr/bin/env python3
"""Real OresForge CLI input-budget regression (not a parser-only unit test)."""
from pathlib import Path
import subprocess
import sys
import tempfile


def run(executable: str, path: Path):
    return subprocess.run(
        [executable, "ir", str(path)],
        capture_output=True, text=True, timeout=12, check=False,
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: cli_source_budget.py <oresforge>")
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="oresforge-budget-") as temp:
        good = Path(temp) / "good.ores"
        good.write_text("define pub fnc main() : int { return 2+3*4; }")
        control = run(executable, good)
        if control.returncode != 0 or "ret %" not in control.stdout:
            raise SystemExit("FAIL: executable did not compile a valid control")
        bad = Path(temp) / "too-large.ores"
        bad.write_bytes(b" " * (1024 * 1024 + 1))
        rejected = run(executable, bad)
        if rejected.returncode == 0 or rejected.stdout:
            raise SystemExit("FAIL: oversized source was accepted or emitted IR")
        if "source exceeds 1 MiB bootstrap limit" not in rejected.stderr:
            raise SystemExit("FAIL: rejection did not originate from the byte budget")
    print("Forge CLI source input budget enforced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
