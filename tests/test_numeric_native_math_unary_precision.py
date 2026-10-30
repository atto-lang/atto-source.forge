import hashlib
import json
import struct
import unittest
from decimal import Decimal, Context, ROUND_HALF_EVEN, localcontext
from fractions import Fraction
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



class ForgeBigIntExactReferenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / "contracts/v1/bigint-exact-v1.json"
        raw = path.read_bytes()
        assert git_blob_sha(raw) == "b49c7a7d9395f7298e5534c4a6e5b90d4a2a518c"
        cls.contract = json.loads(raw)

    def test_div_rem_reference_and_fail_closed_backend(self):
        self.assertEqual("oreslang.bigint-exact.v1", self.contract["contract"])
        for item in self.contract["division_cases"]:
            a, b = int(item["left"]), int(item["right"])
            if item.get("error"):
                self.assertEqual(0, b)
                continue
            q = abs(a) // abs(b)
            if (a < 0) != (b < 0):
                q = -q
            r = a - q * b
            self.assertEqual(item["quot"], str(q), item["id"])
            self.assertEqual(item["rem"], str(r), item["id"])
            self.assertEqual(a, q * b + r, item["id"])

        caps = self.backend["capabilities"]
        for name in ("bigint_div_rem", "bigint_to_i64_checked",
                     "bigint_to_f64_exact", "bigint_to_f64_nearest"):
            self.assertEqual("unsupported_explicit", caps[name], name)


class ForgeDecimalPrecisionReferenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / "contracts/v1/decimal-precision-v1.json"
        raw = path.read_bytes()
        assert git_blob_sha(raw) == "3bbb44277f3816add53da40a1e161e466ab162b1"
        cls.contract = json.loads(raw)

    def test_decimal_reference_and_backend_fail_closed(self):
        self.assertEqual("oreslang.decimal-precision.v1", self.contract["contract"])
        for item in self.contract["exact_cases"]:
            a, b = Decimal(item["left"]), Decimal(item["right"])
            if item["operation"] == "add":
                actual = a + b
            elif item["operation"] == "sub":
                actual = a - b
            elif item["operation"] == "mul":
                actual = a * b
            else:
                if b == 0:
                    self.assertEqual("DIVISOR_ZERO", item["error"], item["id"])
                    continue
                fraction = Fraction(a) / Fraction(b)
                denominator = fraction.denominator
                while denominator % 2 == 0: denominator //= 2
                while denominator % 5 == 0: denominator //= 5
                if denominator != 1:
                    self.assertEqual("NONTERMINATING_REQUIRES_CONTEXT",
                                     item["error"], item["id"])
                    continue
                actual = a / b
            self.assertEqual(Decimal(item["expect"]), actual, item["id"])

        for item in self.contract["context_cases"]:
            ctx = Context(prec=item["precision"], rounding=ROUND_HALF_EVEN)
            with localcontext(ctx):
                actual = (Decimal(item["left"]) / Decimal(item["right"])
                          if item["operation"] == "div"
                          else +Decimal(item["operand"]))
            self.assertEqual(Decimal(item["expect"]), actual, item["id"])

        caps = self.backend["capabilities"]
        for name in ("decimal_unary", "decimal_round_context",
                     "decimal_to_f64_exact", "decimal_to_f64_nearest"):
            self.assertEqual("unsupported_explicit", caps[name], name)

if __name__ == "__main__":
    unittest.main()
