"""Forge Atto library sources: source-free integrity/reference test, not guest."""
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHA1 = {
    "math": "1bf48b53e8222c189e01d9172a379888a736690d",
    "hash": "03e621e383e7e51abd43c7dc65b3a10b90ce448e",
    "cell": "752110144ca67c00d6df45d862c919d118a8171b"
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

if __name__ == "__main__":
    unittest.main()
