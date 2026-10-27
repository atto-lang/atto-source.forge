#!/usr/bin/env python3
"""Exercise Forge's real compiler CLI: unsupported optimizations fail closed."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

VARS = ("ORES_OPT_ESCAPE", "ORES_JIT", "ORES_OPT_ESCAPE_REPORT")


def run(executable, args, extra_env=None):
    env = os.environ.copy()
    for key in VARS:
        env.pop(key, None)
    env.update(extra_env or {})
    return subprocess.run([executable, *args], capture_output=True, text=True,
                          timeout=12, env=env, check=False)


def expect_success(executable, args, env=None):
    result = run(executable, args, env)
    if result.returncode != 0 or result.stdout != "42\n":
        raise AssertionError(f"expected normal execution: {args!r} {result!r}")


def expect_reject(executable, args, diagnostic, env=None):
    result = run(executable, args, env)
    if (result.returncode != 1 or result.stdout or diagnostic not in result.stderr):
        raise AssertionError(f"failed closed for wrong reason: {args!r} {result!r}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: optimization_cli_admission.py <oresforge>")
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="forge-opt-policy-") as directory:
        source = Path(directory) / "scalar.ores"
        source.write_text("define pub fnc main():int { return 42; }\n", encoding="utf-8")
        command = ["run", str(source)]
        expect_success(exe, command)
        expect_success(exe, ["--opt-escape=off", "--jit=off", *command])
        expect_success(exe, [*command, "--opt-escape=off", "--opt-escape-report=false"])
        expect_success(exe, command, {"ORES_OPT_ESCAPE": "off",
                                      "ORES_JIT": "off", "ORES_OPT_ESCAPE_REPORT": "false"})
        # Direct spelling takes precedence over flags-2-env environment mapping.
        expect_success(exe, ["--opt-escape=off", *command], {"ORES_OPT_ESCAPE": "safe"})
        expect_success(exe, ["--opt-escape-report=false", *command],
                       {"ORES_OPT_ESCAPE_REPORT": "true"})
        # Direct overrides take precedence even over malformed mapped values.
        # The overridden env is not an additional invalid CLI argument.
        expect_success(exe, ["--opt-escape-report=false", *command],
                       {"ORES_OPT_ESCAPE_REPORT": "not-a-boolean"})
        expect_success(exe, ["--jit=off", *command], {"ORES_JIT": "unsupported"})
        expect_success(exe, ["--opt-escape=off", *command],
                       {"ORES_OPT_ESCAPE": "invalid"})
        # With no override, malformed mapped bool must fail closed.

        for mode in ("safe", "verify"):
            expect_reject(exe, ["--opt-escape=" + mode, *command],
                          "verified escape optimization not implemented")
            expect_reject(exe, command, "verified escape optimization not implemented",
                          {"ORES_OPT_ESCAPE": mode})
        expect_reject(exe, ["--opt-escape=speculative", *command], "requires --jit=on")
        expect_reject(exe, ["--opt-escape=speculative", "--jit=on", *command],
                      "Forge has no executable JIT")
        expect_reject(exe, ["--jit=on", *command], "Forge has no executable JIT")
        expect_reject(exe, command, "Forge has no executable JIT", {"ORES_JIT": "on"})
        expect_reject(exe, ["--opt-escape-report", *command],
                      "no allocation evidence to report")
        expect_reject(exe, command, "no allocation evidence to report",
                      {"ORES_OPT_ESCAPE_REPORT": "true"})
        for args, diagnostic in [
            (["--opt-escape=typo"], "invalid --opt-escape mode"),
            (["--opt-escape="], "invalid --opt-escape mode"),
            (["--jit=maybe"], "invalid --jit mode"),
            (["--opt-escape"], "invalid optimization flag syntax"),
            (["--jit"], "invalid optimization flag syntax"),
            (["--opt-escape-report=nope"], "invalid --opt-escape-report boolean"),
            (["--opt-escape=off", "--opt-escape=off"], "duplicate --opt-escape"),
            (["--jit=off", "--jit=off"], "duplicate --jit"),
            (["--opt-escape-report", "--opt-escape-report"], "duplicate --opt-escape-report"),
        ]:
            expect_reject(exe, [*args, *command], diagnostic)
        expect_reject(exe, command, "invalid --opt-escape mode", {"ORES_OPT_ESCAPE": "bad"})
        expect_reject(exe, command, "invalid --jit mode", {"ORES_JIT": "bad"})
        expect_reject(exe, command, "invalid --opt-escape-report boolean",
                      {"ORES_OPT_ESCAPE_REPORT": "bad"})

        # Failure occurs at policy admission, even for missing source files;
        # unsupported configurations must never print IR or assembly.
        missing = str(Path(directory) / "missing.ores")
        for operation in (["run", missing], ["ir", missing],
                          ["emit-asm", "x86_64", missing],
                          ["run-runtime", missing], ["ir-runtime", missing],
                          ["emit-runtime-asm", "x86_64", missing],
                          ["run-calls", missing], ["emit-call-asm", "x86_64", missing]):
            expect_reject(exe, ["--opt-escape=safe", *operation],
                          "verified escape optimization not implemented")
    print("Forge allocation/JIT CLI fail-closed admission passed")


if __name__ == "__main__":
    main()
