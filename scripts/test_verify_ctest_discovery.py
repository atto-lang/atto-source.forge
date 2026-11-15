#!/usr/bin/env python3
"""Real CMake/CTest + malformed-inventory regression tests for the CI gate."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_ctest_discovery import REQUIRED, LINUX_X86_REQUIRED, verify_inventory

SCRIPT = Path(__file__).with_name("verify_ctest_discovery.py")


class DiscoveryGateTests(unittest.TestCase):
    def configure(self, names):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        src = directory / "src"
        build = directory / "build"
        src.mkdir()
        source = "cmake_minimum_required(VERSION 3.20)\nproject(TestInventory NONE)\nenable_testing()\n"
        for name in sorted(names):
            source += f"add_test(NAME {name} COMMAND ${CMAKE_COMMAND} -E true)\n"
        (src / "CMakeLists.txt").write_text(source, encoding="utf-8")
        proc = subprocess.run(["cmake", "-S", str(src), "-B", str(build)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return build

    def cli(self, build):
        return subprocess.run([sys.executable, str(SCRIPT), "--build-dir", str(build)],
                              capture_output=True, text=True)

    def test_real_ctest_all_critical_names_accepted(self):
        build = self.configure(REQUIRED | LINUX_X86_REQUIRED)
        out = self.cli(build)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("CTEST_DISCOVERY_VERIFIED", out.stdout)

    def test_real_ctest_missing_security_test_is_rejected(self):
        build = self.configure((REQUIRED | LINUX_X86_REQUIRED) - {"oresforge_declarative_export_reject_name_257"})
        out = self.cli(build)
        self.assertEqual(out.returncode, 1)
        self.assertIn("CTEST_DISCOVERY_MISSING: oresforge_declarative_export_reject_name_257", out.stderr)

    def test_real_empty_ctest_must_fail_even_if_ctest_returns_zero(self):
        build = self.configure(set())
        raw = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"], capture_output=True, text=True)
        self.assertEqual(raw.returncode, 0)
        self.assertEqual(json.loads(raw.stdout)["tests"], [])
        out = self.cli(build)
        self.assertEqual(out.returncode, 1)
        self.assertIn("CTEST_DISCOVERY_MISSING", out.stderr)

    def test_duplicate_names_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_DUPLICATE"):
            verify_inventory({"tests": [{"name": "oresforge_unit"}, {"name": "oresforge_unit"}]}, linux_x86=False)

    def test_invalid_shapes_are_rejected(self):
        for data in (None, [], {}, {"tests": None}, {"tests": [None]}, {"tests": [{"name": ""}]},
                     {"tests": [{"name": 1}]}):
            with self.subTest(data=data), self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_MALFORMED"):
                verify_inventory(data, linux_x86=False)

    def test_host_only_critical_set_stays_enforced(self):
        names = [{"name": n} for n in REQUIRED]
        self.assertEqual(verify_inventory({"tests": names}, linux_x86=False), (len(REQUIRED), len(REQUIRED)))
        with self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_MISSING"):
            verify_inventory({"tests": names}, linux_x86=True)


if __name__ == "__main__":
    unittest.main()
