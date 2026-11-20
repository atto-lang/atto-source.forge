"""Reference-only immutable-map collision and version oracle.
Not a HAMT implementation or Forge native compiler/actor conformance test.
"""
import unittest
from dataclasses import dataclass

@dataclass(frozen=True)
class Key:
    text: str
    def __hash__(self):
        return 17  # Deliberately force all distinct keys into one collision bucket.

class FrozenReferenceMap:
    def __init__(self, entries=()):
        if len(entries)>4096:
            raise ValueError("bounded snapshot required")
        values={}
        for key,value in entries:
            if not isinstance(key,Key) or not isinstance(value,(int,str,bool)):
                raise TypeError("only admitted scalar values")
            values[key]=value
        self._entries=tuple(sorted(values.items(),key=lambda kv:kv[0].text))
    def get(self,key):
        for k,v in self._entries:
            if k==key:return v
        raise KeyError(key)
    def set(self,key,value):
        if not isinstance(key,Key) or not isinstance(value,(int,str,bool)):
            raise TypeError("unsafe scalar value")
        return FrozenReferenceMap((*self._entries,(key,value)))
    def items(self):
        return self._entries

class PersistentMapReferenceTests(unittest.TestCase):
    def test_collisions_do_not_overwrite_unrelated_keys(self):
        a,b=Key("alpha"),Key("beta")
        old=FrozenReferenceMap()
        first=old.set(a,3)
        second=first.set(b,4)
        self.assertEqual(first.get(a),3)
        self.assertEqual(second.get(a),3)
        self.assertEqual(second.get(b),4)
        with self.assertRaises(KeyError):first.get(b)
        self.assertEqual(old.items(),())
    def test_version_stability_and_deterministic_snapshots(self):
        k1,k2=Key("a"),Key("b")
        left=FrozenReferenceMap(((k2,2),(k1,1)))
        right=FrozenReferenceMap(((k1,1),(k2,2)))
        self.assertEqual(left.items(),right.items())
        newer=left.set(k1,9)
        self.assertEqual(left.get(k1),1)
        self.assertEqual(newer.get(k1),9)
    def test_reject_mutable_or_callable_values(self):
        for invalid in ([1],{"x":1},lambda:1,object()):
            with self.subTest(type=type(invalid)):
                with self.assertRaises(TypeError):
                    FrozenReferenceMap().set(Key("x"),invalid)
    def test_conservative_capacity_guard(self):
        with self.assertRaises(ValueError):
            FrozenReferenceMap(tuple((Key(str(i)),i) for i in range(4097)))

if __name__=="__main__":
    unittest.main()
