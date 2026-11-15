"""Real .atto source -> restricted compiler -> C++20 -> native guest tests."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import forge_scalar

CXX = os.environ.get("CXX", "c++")
FLAGS = ["-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]


def run_guest(source: str):
    with tempfile.TemporaryDirectory(prefix="atto-forge-") as directory:
        folder = Path(directory)
        source_path = folder / "input.atto"
        cpp_path = folder / "guest.cpp"
        binary = folder / "guest"
        source_path.write_text(source, encoding="utf-8")
        frontend = subprocess.run(
            [sys.executable, str(ROOT / "tools/forge_scalar.py"),
             str(source_path), "-o", str(cpp_path)],
            text=True, capture_output=True, timeout=10, check=False)
        if frontend.returncode:
            return frontend, None
        compiler = subprocess.run(
            [CXX, *FLAGS, "-I", str(ROOT / "include"), str(cpp_path),
             "-o", str(binary)], capture_output=True, text=True, timeout=45, check=False)
        if compiler.returncode:
            raise AssertionError(f"C++ compile failed: {compiler.stderr}")
        guest = subprocess.run([str(binary)], text=True, capture_output=True,
                               timeout=10, check=False)
        return frontend, guest


class ForgeScalarCompilerTests(unittest.TestCase):
    def test_actual_compiled_guest_values(self):
        examples = [
            ("pub fnc main(): int { return 42; }", 42),
            ("fnc main(): int { return 10 - 2 * 3; }", 4),
            ("fnc main(): int { return (10 - 2) * 3; }", 24),
            ("fnc main(): int { return -10 + +3; }", -7),
            ("fnc main(): int { return 4 * -5 + 3; }", -17),
            ("fnc main(): int { return -9223372036854775808; }", -9223372036854775808),
            ("fnc main(): int { return 9223372036854775807; }", 9223372036854775807),
            ("fnc main(): int { return -9223372036854775808 + 1; }", -9223372036854775807),
        ]
        for source, result in examples:
            with self.subTest(source=source):
                frontend, guest = run_guest(source)
                self.assertEqual(frontend.returncode, 0, frontend.stderr)
                self.assertIsNotNone(guest)
                self.assertEqual((guest.returncode, guest.stdout, guest.stderr),
                                 (0, f"{result}\n", ""))

    def test_actual_compiled_guest_overflow(self):
        cases = [
            "9223372036854775807 + 1",
            "-9223372036854775808 - 1",
            "9223372036854775807 * 2",
            "-9223372036854775808 * -1",
            "-(-9223372036854775808)",
        ]
        for expr in cases:
            with self.subTest(expr=expr):
                frontend, guest = run_guest(f"fnc main(): int {{ return {expr}; }}")
                self.assertEqual(frontend.returncode, 0, frontend.stderr)
                self.assertEqual(guest.returncode, 70, guest.stderr)
                self.assertEqual(guest.stdout, "")
                self.assertEqual(guest.stderr, "E_OVERFLOW\n")

    def test_invalid_sources_reject_without_artifact(self):
        malformed = [
            "fnc main(): int { return 9223372036854775808; }",
            "fnc main(): bool { return 1; }",
            "fnc foo(): int { return 1; }",
            "fnc main(): int { return 1 / 0; }",
            "fnc main(): int { return a; }",
            "fnc main(): int { return 1; return 2; }",
            'fnc main(): int { return 1; } system("bad");',
            "fnc main(): int { return 1 +; }",
            "fnc main(): int { return (1 + 2; }",
            "fnc main(): int { return -9223372036854775809; }",
            "fnc main(): int { return 1; } // untrusted",
            "fnc main(): int { return 1; }" + " " * 20000,
            "fnc main(): int { return " + "(" * 40 + "1" + ")" * 40 + "; }",
        ]
        for source in malformed:
            with self.subTest(source=source[:60]):
                with tempfile.TemporaryDirectory() as directory:
                    input_path = Path(directory) / "input.atto"
                    output_path = Path(directory) / "out.cpp"
                    input_path.write_text(source, encoding="utf-8")
                    completed = subprocess.run(
                        [sys.executable, str(ROOT / "tools/forge_scalar.py"),
                         str(input_path), "-o", str(output_path)],
                        capture_output=True, text=True, timeout=5, check=False)
                    self.assertEqual(completed.returncode, 2)
                    self.assertFalse(output_path.exists())
                    self.assertIn("Forge scalar compile error:", completed.stderr)

    def test_generated_cpp_is_deterministic(self):
        source = "fnc main(): int { return 10 * (4 + 2); }"
        first = forge_scalar.compile_source(source)
        self.assertEqual(first, forge_scalar.compile_source(source))
        self.assertIn("atto::forge::checked_add", first)
        self.assertIn("atto::forge::checked_mul", first)
        self.assertNotIn("system(", first)

    def test_source_limits(self):
        with self.assertRaisesRegex(forge_scalar.SourceError, "too many tokens"):
            forge_scalar.compile_source("fnc main(): int { return " + "1+" * 140 + "1; }")
        with self.assertRaisesRegex(forge_scalar.SourceError, "nesting too deep"):
            forge_scalar.compile_source("fnc main(): int { return " + "-" * 50 + "1; }")


if __name__ == "__main__":
    unittest.main()
