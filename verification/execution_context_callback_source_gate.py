#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

EXPECTED_FIXTURE_BLOB = "83f7db76fd6437d7d4994210e78034b93a5bff26"
EXPECTED_DIAGNOSTIC = (
    "async/Future callback source requires scheduler-aware continuation lowering"
)


def git_blob_id(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(
        f"blob {len(data)}\0".encode() + data,
        usedforsecurity=False,
    ).hexdigest()


def require(ok: bool, message: str) -> None:
    if not ok:
        raise SystemExit("FAIL: " + message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()

    compiler = Path(args.compiler)
    fixture = Path(args.fixture)
    require(fixture.is_file(), "callback fixture missing")
    require(git_blob_id(fixture) == EXPECTED_FIXTURE_BLOB,
            "callback fixture Git blob drift")

    rejected = subprocess.run(
        [str(compiler), "run", str(fixture)],
        capture_output=True,
        text=True,
        check=False,
        timeout=30,
    )
    require(rejected.returncode == 1,
            f"unsupported callback source returned {rejected.returncode}, expected 1")
    require(not rejected.stdout, "unsupported callback source emitted stdout")
    require(rejected.stderr.startswith("oresforge: "),
            "Forge rejection lost compiler diagnostic prefix")
    require(EXPECTED_DIAGNOSTIC in rejected.stderr,
            "unsupported callback source produced wrong diagnostic")

    with tempfile.TemporaryDirectory(prefix="oresforge-callback-control-") as tmp:
        control = Path(tmp) / "control.ores"
        control.write_text("pub fnc main():int{return 42;}\n", encoding="utf-8")
        accepted = subprocess.run(
            [str(compiler), "run", str(control)],
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
        require(accepted.returncode == 0, "Forge scalar control failed")
        require(accepted.stdout == "42\n", "Forge scalar control output drift")
        require(not accepted.stderr, "Forge scalar control emitted stderr")

    print(
        "FORGE_CALLBACK_SOURCE_EXPECTED_REJECT_PASS "
        f"fixture={EXPECTED_FIXTURE_BLOB}"
    )


if __name__ == "__main__":
    main()
