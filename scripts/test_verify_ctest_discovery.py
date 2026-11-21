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
    def configure(self, names, *, policy=None, missing_executable=None, exit_nonzero=None):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        src = directory / "src"
        build = directory / "build"
        src.mkdir()
        source = "cmake_minimum_required(VERSION 3.20)\nproject(TestInventory NONE)\nenable_testing()\n"
        for name in sorted(names):
            if name == missing_executable:
                source += f"add_test(NAME {name} COMMAND ./never-created-binary)\n"
            elif name == exit_nonzero:
                source += f'add_test(NAME {name} COMMAND /bin/sh -c "echo 14; exit 7")\n'
            else:
                source += f"add_test(NAME {name} COMMAND ${{CMAKE_COMMAND}} -E true)\n"
            if name == "oresforge_unit" and policy is not None:
                prop, value = policy
                source += f"set_tests_properties({name} PROPERTIES {prop} {value})\n"
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

    def test_real_ctest_skip_and_inverted_result_are_rejected(self):
        for prop, value in (
            ("DISABLED", "TRUE"),
            ("SKIP_RETURN_CODE", "0"),
            ("SKIP_REGULAR_EXPRESSION", "skip"),
            ("WILL_FAIL", "TRUE"),
            ("PASS_REGULAR_EXPRESSION", "14"),
        ):
            with self.subTest(property=prop):
                build = self.configure(REQUIRED | LINUX_X86_REQUIRED, policy=(prop, value))
                output = self.cli(build)
                self.assertEqual(output.returncode, 1, output.stdout)
                self.assertIn("CTEST_DISCOVERY_SKIP_POLICY", output.stderr)
                self.assertIn(prop, output.stderr)

    @unittest.skipUnless(sys.platform == "linux", "shell reproduction runs on Linux CI")
    def test_regex_can_mark_nonzero_exit_success_and_gate_rejects_it(self):
        build = self.configure(REQUIRED | LINUX_X86_REQUIRED,
                               policy=("PASS_REGULAR_EXPRESSION", "14"),
                               exit_nonzero="oresforge_unit")
        fooled = subprocess.run(
            ["ctest", "--test-dir", str(build), "-R", "^oresforge_unit$", "--output-on-failure"],
            capture_output=True, text=True)
        self.assertEqual(fooled.returncode, 0, fooled.stdout + fooled.stderr)
        self.assertIn("100% tests passed", fooled.stdout)
        result = self.cli(build)
        self.assertEqual(result.returncode, 1)
        self.assertIn("CTEST_DISCOVERY_SKIP_POLICY", result.stderr)
        self.assertIn("PASS_REGULAR_EXPRESSION", result.stderr)

    def test_real_ctest_registration_with_unbuilt_executable_is_rejected(self):
        build = self.configure(REQUIRED | LINUX_X86_REQUIRED,
                               missing_executable="oresforge_unit")
        # CTest JSON lists names even for missing binaries; the command is
        # absent, and a name-only check would incorrectly treat it as ready.
        raw = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                             capture_output=True, text=True)
        self.assertEqual(raw.returncode, 0)
        entry = next(item for item in json.loads(raw.stdout)["tests"]
                     if item["name"] == "oresforge_unit")
        self.assertNotIn("command", entry)
        output = self.cli(build)
        self.assertEqual(output.returncode, 1, output.stdout)
        self.assertIn("CTEST_DISCOVERY_NO_EXECUTABLE: oresforge_unit", output.stderr)

    def test_duplicate_names_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_DUPLICATE"):
            verify_inventory({"tests": [{"name": "oresforge_unit"}, {"name": "oresforge_unit"}]}, linux_x86=False)

    def test_invalid_shapes_are_rejected(self):
        for data in (None, [], {}, {"tests": None}, {"tests": [None]}, {"tests": [{"name": ""}]},
                     {"tests": [{"name": 1}]}):
            with self.subTest(data=data), self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_MALFORMED"):
                verify_inventory(data, linux_x86=False)

    def test_host_only_critical_set_stays_enforced(self):
        names = [{"name": n, "command": ["/usr/bin/true"]} for n in REQUIRED]
        self.assertEqual(verify_inventory({"tests": names}, linux_x86=False), (len(REQUIRED), len(REQUIRED)))
        with self.assertRaisesRegex(ValueError, "CTEST_DISCOVERY_MISSING"):
            verify_inventory({"tests": names}, linux_x86=True)


if __name__ == "__main__":
    unittest.main()
