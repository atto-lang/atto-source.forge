"""Adversarial tests for the reviewed portable i64 to C++ case generator."""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
GENERATE = ROOT / "verification/generate_numeric_i64_cases.py"
CORPUS = ROOT / "contracts/v1/numeric-parity/source-i64-cases.json"
SPEC = importlib.util.spec_from_file_location("numeric_fixture_codegen", GENERATE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sha_blob(raw):
    return hashlib.sha1(b"blob " + str(len(raw)).encode("ascii")
                        + bytes((0,)) + raw).hexdigest()


class NumericGeneratorTest(unittest.TestCase):
    def test_seven_cases_are_generated_from_exact_git_object(self):
        self.assertEqual(sha_blob(CORPUS.read_bytes()), MODULE.FIXTURE_SHA)
        out = MODULE.generate(CORPUS)
        self.assertIn("inline constexpr auto kPortableNumericalCases", out)
        self.assertEqual(out.count("std::pair{"), 7)
        self.assertIn("std::numeric_limits<std::int64_t>::min()", out)
        self.assertIn("9223372036854775806", out)

    def test_tampered_corpus_cannot_replace_previous_valid_header(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            invalid = p / "invalid.json"
            out = p / "result.hpp"
            out.write_text("previous verified output\n", encoding="ascii")
            invalid.write_bytes(CORPUS.read_bytes().replace(b"2+3*4", b"2+3*5"))
            finished = subprocess.run([
                sys.executable, str(GENERATE), "--corpus", str(invalid),
                "--output", str(out)], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(finished.returncode, 0)
            self.assertIn("identity differs", finished.stderr)
            self.assertEqual(out.read_text(encoding="ascii"), "previous verified output\n")

    def test_invalid_semantics_cannot_pass_with_a_recomputed_blob_hash(self):
        raw = CORPUS.read_bytes()
        mutations = [
            ("expression", "1/0"),
            ("expression", "not_numeric()"),
            ("expression", "9223372036854775807+1"),
            ("expected", "9999"),
        ]
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "modified.json"
            for field, value in mutations:
                payload = json.loads(raw)
                payload["cases"][0][field] = value
                serialized = json.dumps(payload).encode("utf-8")
                path.write_bytes(serialized)
                # Simulate a contributor updating the pin as well.
                with mock.patch.object(MODULE, "FIXTURE_SHA", sha_blob(serialized)):
                    with self.subTest(field=field, value=value):
                        with self.assertRaises(ValueError):
                            MODULE.generate(path)

    def test_duplicate_case_ids_are_rejected_even_with_updated_pin(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "duplicate.json"
            fixture = json.loads(CORPUS.read_bytes())
            fixture["cases"][1]["id"] = fixture["cases"][0]["id"]
            payload = json.dumps(fixture).encode("utf-8")
            p.write_bytes(payload)
            with mock.patch.object(MODULE, "FIXTURE_SHA", sha_blob(payload)):
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    MODULE.generate(p)

    def test_numeric_ast_is_bounded(self):
        for value in ("1/3", "2**300", "9223372036854775808+1", "True",
                      "9223372036854775807 * 2", "-(-9223372036854775808)"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    MODULE.evaluate(ast.parse(value, mode="eval").body)


if __name__ == "__main__":
    unittest.main()
