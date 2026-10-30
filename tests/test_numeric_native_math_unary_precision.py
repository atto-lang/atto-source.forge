import hashlib
import json
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "contracts/v1/native-math-unary-precision-v1.json"
BACKEND = ROOT / "contracts/v1/numeric-parity/backend.json"
EXPECTED_BLOB = "3249d837ae6bcf6e2c2c23cac872becf92d0c8e0"
I64_MIN = -(1 << 63)
I64_MAX = (1 << 63) - 1


def git_blob_sha(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def eval_i64(operation, operand):
    value = int(operand)
    if not I64_MIN <= value <= I64_MAX:
        raise ValueError("outside i64")
    if operation == "Math.neg":
        return {"error": "OVERFLOW"} if value == I64_MIN else {"value": str(-value)}
    if operation == "Math.abs":
        return {"error": "OVERFLOW"} if value == I64_MIN else {"value": str(abs(value))}
    raise ValueError(operation)


class ForgeNativeMathContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = CONTRACT.read_bytes()
        assert git_blob_sha(raw) == EXPECTED_BLOB
        cls.contract = json.loads(raw)
        cls.backend = json.loads(BACKEND.read_text(encoding="utf-8"))

    def test_checked_i64_unary_reference(self):
        for item in self.contract["signed_i64_unary"]:
            with self.subTest(item=item["id"]):
                self.assertEqual(item["expect"], eval_i64(item["operation"], item["operand"]))

    def test_bigint_reference_is_never_float_backed(self):
        for item in self.contract["bigint_unary"]:
            value = int(item["operand"])
            actual = -value if item["operation"] == "Math.neg" else abs(value)
            self.assertEqual(item["expect"], str(actual))

    def test_binary64_reference_bits(self):
        for item in self.contract["binary64_unary"]:
            value = struct.unpack(">d", bytes.fromhex(item["operand_bits"]))[0]
            actual = abs(value) if item["operation"] == "Math.abs" else -value
            self.assertEqual(item["expect_bits"], struct.pack(">d", actual).hex())

    def test_backend_claims_remain_fail_closed(self):
        caps = self.backend["capabilities"]
        self.assertEqual("implemented_unverified", caps["math_i64_unary"])
        self.assertIn("unary-minus", self.backend["evidence"]["math_i64_unary"]["scope"])
        for name in ("binary64_unary", "bigint_unary", "decimal_unary"):
            self.assertEqual("unsupported_explicit", caps[name])
        self.assertEqual(EXPECTED_BLOB,
                         self.backend["math_unary_precision_fixture_git_blob_sha"])


if __name__ == "__main__":
    unittest.main()
