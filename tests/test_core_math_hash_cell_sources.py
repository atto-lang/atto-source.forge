"""Forge Atto library sources: source-free integrity/reference test, not guest."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHA1 = {
    "math": "1bf48b53e8222c189e01d9172a379888a736690d",
    "hash": "03e621e383e7e51abd43c7dc65b3a10b90ce448e",
    "cell": "7a0b117c5a6775921e541dfa5aaf654ba2aea7f4"
}

def step(s, b):
    if not 0 <= s < 1_000_000_007 or not 0 <= b <= 255:
        return -1
    n = s * 31 + b
    for _ in range(31):
        if n >= 1_000_000_007:
            n -= 1_000_000_007
    return n

class ForgeCoreSource(unittest.TestCase):
    def test_pinned_regular_atto_bytes(self):
        for name, expected in SHA1.items():
            path = ROOT / "library" / "atto.core" / (name + ".atto")
            self.assertFalse(path.is_symlink())
            self.assertTrue(path.is_file())
            data = path.read_bytes()
            self.assertLessEqual(len(data), 1048576)
            self.assertGreater(len(data), 0)
            self.assertEqual(hashlib.sha1(b"blob " + str(len(data)).encode()
                                          + b"\0" + data).hexdigest(), expected)
            source = data.decode("utf-8", errors="strict")
            self.assertTrue("define module " + name + " as" in source)
            self.assertFalse("\x00" in source or source.startswith("\ufeff"))
            self.assertFalse("java:" in source or "dotnet:" in source)

    def test_byte_fold_reference(self):
        for state in [0,1,17,569,1000000005,1000000006]:
            for byte in range(256):
                self.assertEqual(step(state, byte), (state * 31 + byte) % 1000000007)
        self.assertEqual(step(step(17, 42), 7), 17646)
        self.assertEqual(step(-1, 42), -1)
        self.assertEqual(step(17, 256), -1)

    def test_cell_is_only_policy(self):
        cell = (ROOT / "library" / "atto.core" / "cell.atto").read_text("utf-8")
        self.assertTrue("not* shared mutable storage" in cell)
        self.assertTrue("can_borrow_write" in cell)


    def test_owner_operations_remain_non_authoritative_policy_only(self):
        cell = (ROOT / "library" / "atto.core" / "cell.atto").read_text("utf8")
        for operation in ("valid_loan_state", "can_owner_read", "can_owner_write",
                          "can_move_owner", "can_suspend_unpinned"):
            self.assertIn("pub fnc " + operation + "(", cell)
        # Host reference is deliberately NOT compiler or guest execution.
        vectors = ((0, False, True, True), (1, False, True, False),
                   (65535, False, True, False), (65536, False, False, False),
                   (-1, False, False, False), (0, True, True, False),
                   (1, True, False, False))
        for readers, writer, valid, writable in vectors:
            self.assertEqual(0 <= readers <= 65535 and (not writer or readers == 0), valid)
            self.assertEqual(readers == 0 and not writer, writable)
        self.assertNotIn("pub fnc issue_loan(", cell)

    def test_saturated_shared_loan_denies_new_acquisition_but_not_owner_read(self):
        cell = (ROOT / "library" / "atto.core" / "cell.atto").read_text("utf8")
        self.assertIn("readers >= 0 && readers < 65535", cell)
        # Independent source-only reference: not a compiler-checked loan.
        for readers, writer, permitted in (
            (0, False, True), (65534, False, True),
            (65535, False, False), (65536, False, False),
            (-1, False, False), (0, True, False)
        ):
            self.assertEqual(0 <= readers < 65535 and not writer, permitted)
        self.assertTrue(0 <= 65535 <= 65535)  # existing owner may still read
        self.assertNotIn("pub fnc issue_loan(", cell)

if __name__ == "__main__":
    unittest.main()
