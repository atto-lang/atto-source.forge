"""Fail-closed manifest integrity checks for all five Oreslang numeric backends.

Passing these tests verifies *reference data and honest capability reporting*.
It is NOT compiled guest execution or evidence that any backend has parity.
"""
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "contracts/v1/numeric-parity/backend.json"
GOLDEN = ROOT / "contracts/v1/numeric-precision/cases.json"
SHA = "80436f21a95816e2d5e40274daba9b923b3bcec9"
BASE_SHA = "cb28b5678d380f02c5e4e43bd763da04bdb996fe"
FEATURES = frozenset((
    "source_i64", "checked_i64_overflow", "binary64_ordering",
    "binary32_arithmetic", "bigint_exact", "decimal_exact",
    "complex_binary64", "decimal_matrix", "source_division_to_float",
    "math_i64_unary",
    "binary64_unary",
    "bigint_unary",
    "decimal_unary",
    "bigint_div_rem",
    "bigint_to_i64_checked",
    "bigint_to_f64_exact",
    "bigint_to_f64_nearest",
    "decimal_round_context",
    "decimal_to_f64_exact",
    "decimal_to_f64_nearest",
    "binary64_strict_profile",
    "binary64_next_after",
    "binary64_ulp",
    "binary64_approx_eq",
    "binary64_fma",
    "binary64_sqrt",
    "binary64_transcendentals",
))
# Exact reviewed scope notes explain unsupported capability boundaries; they are
# not proof fields and cannot be substituted for independent CI evidence.
UNSUPPORTED_SCOPE_NOTES = {
    "math_i64_unary": "Checked unary-minus scalar operation exists in Forge, but Math.abs(int)/Math.neg(int) source module dispatch and typed guest overload lowering are UNSUPPORTED; this source-only contract is not compiled guest evidence.",
    "bigint_div_rem": "Shared exact BigInt contract pinned; no guest arbitrary-precision lowering for this operation.",
    "bigint_to_i64_checked": "Shared exact BigInt contract pinned; no guest arbitrary-precision lowering for this operation.",
    "bigint_to_f64_exact": "Shared exact BigInt contract pinned; no guest arbitrary-precision lowering for this operation.",
    "bigint_to_f64_nearest": "Shared exact BigInt contract pinned; no guest arbitrary-precision lowering for this operation.",
    "decimal_round_context": "Shared Decimal precision contract pinned; no guest Decimal implementation for this operation.",
    "decimal_to_f64_exact": "Shared Decimal precision contract pinned; no guest Decimal implementation for this operation.",
    "decimal_to_f64_nearest": "Shared Decimal precision contract pinned; no guest Decimal implementation for this operation.",
    "binary64_strict_profile": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_next_after": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_ulp": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_approx_eq": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_fma": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_sqrt": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
    "binary64_transcendentals": "Strict floating contract pinned; Forge currently fails closed on unlowered float semantics.",
}
STATUSES = frozenset((
    "unsupported_explicit", "delegated_unverified",
    "implemented_unverified", "reference_only", "blocked_unsound",
    "needs_audit", "verified_compiled"))
BACKENDS = frozenset(("java", "llvm", "net", "wasm", "forge"))
EXPECTED_BACKEND = "forge"  # fixed by repository identity, not user-editable manifest


def check_claim_is_not_self_attested(status, proof):
    if status == "verified_compiled":
        raise ValueError(
            "compiled parity requires independently verified protected CI evidence")
    if proof != {}:
        raise ValueError("unverified feature cannot attach self-reported test evidence")


class NumericParityAdmissionTest(unittest.TestCase):
    def test_reference_fixture_preserves_shared_base_and_pins_complex_extension(self):
        raw = GOLDEN.read_bytes()
        actual = hashlib.sha1(
            b"blob " + str(len(raw)).encode("ascii") + bytes((0,)) + raw).hexdigest()
        self.assertEqual(actual, SHA, "reference vectors changed without cross-backend review")
        data = json.loads(raw)
        self.assertEqual(data["schema_version"], "1.0.0")
        self.assertEqual(data["status"], "reference-only-not-guest-execution")
        # Preserve the exact common binary64/decimal reference population.
        # The four complex128 cases are an explicit additive reference extension.
        base = dict(data)
        self.assertEqual(len(base.pop("complex128_division")), 4)
        base.pop("complex128_policy")
        base["notes"] = "Golden vectors for future Java/LLVM numeric conformance. Binary64 operands use raw IEEE bits and decimal matrices use exact base-10 strings; no backend may claim success simply by parsing this file."
        projected = (json.dumps(base, indent=2) + "\n").encode()
        digest = hashlib.sha1(b"blob " + str(len(projected)).encode() + b"\0" + projected).hexdigest()
        self.assertEqual(digest, BASE_SHA, "shared numeric base drifted")


    def test_manifest_does_not_claim_unexecuted_guest_parity(self):
        d = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(d["schema_version"], "1.0.0")
        self.assertIn(d["backend"], BACKENDS)
        self.assertEqual(d["backend"], EXPECTED_BACKEND)
        self.assertEqual(d["reference_fixture_git_blob_sha"], SHA)
        self.assertEqual(d["source_i64_fixture_git_blob_sha"],
                         "bbc319c19ecd007109d33592e210091eb267e5a2")
        self.assertEqual(d["source_i64_overflow_fixture_git_blob_sha"],
                         "2ce0b0ff5c52e54a788280fd64d67fa8dc32614a")
        self.assertEqual(set(d["capabilities"]), FEATURES)
        self.assertEqual(set(d["evidence"]), FEATURES)
        for name, status in d["capabilities"].items():
            with self.subTest(feature=name):
                self.assertIn(status, STATUSES)
                proof = d["evidence"][name]
                self.assertIsInstance(proof, dict)
                # A manifest, URL and case counts from this same PR cannot
                # attest to their own compiler execution.
                if name in UNSUPPORTED_SCOPE_NOTES:
                    self.assertEqual(status, "unsupported_explicit")
                    self.assertEqual(proof, {"scope": UNSUPPORTED_SCOPE_NOTES[name]})
                    check_claim_is_not_self_attested(status, {})
                else:
                    check_claim_is_not_self_attested(status, proof)

    def test_forged_compiled_status_and_proofs_are_refused(self):
        # Synthetic malicious/self-reported metadata must never promote a
        # capability even when its URL and SHA appear well-formed.
        fabricated = {
            "exact_source_commit_sha": "f" * 40,
            "workflow_run_url": "https://github.com/example/verify/actions/runs/123",
            "executed_positive_cases": 1000,
            "executed_negative_cases": 1000,
            "actual_compiler_guest_execution": True
        }
        with self.assertRaisesRegex(ValueError, "independently verified"):
            check_claim_is_not_self_attested("verified_compiled", fabricated)
        with self.assertRaisesRegex(ValueError, "self-reported"):
            check_claim_is_not_self_attested("implemented_unverified", fabricated)
        check_claim_is_not_self_attested("implemented_unverified", {})

    def test_manifest_states_are_conservatively_scoped(self):
        d = json.loads(MANIFEST.read_text(encoding="utf-8"))
        b, c = d["backend"], d["capabilities"]
        if b == "wasm":
            self.assertEqual(c["source_i64"], "delegated_unverified")
            self.assertNotIn("verified_compiled", c.values())
        elif b in ("net", "forge"):
            for name in ("binary64_ordering", "binary32_arithmetic",
                         "bigint_exact", "decimal_exact", "decimal_matrix"):
                self.assertEqual(c[name], "unsupported_explicit")
        elif b == "llvm":
            self.assertNotEqual(c["bigint_exact"], "verified_compiled")
        elif b == "java":
            self.assertNotEqual(c["binary32_arithmetic"], "verified_compiled")


if __name__ == "__main__":
    unittest.main()
