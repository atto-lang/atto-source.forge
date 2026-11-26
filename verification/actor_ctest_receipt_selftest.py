#!/usr/bin/env python3
"""Host-only regression for the structured CTest evidence contract."""
from copy import deepcopy
import unittest
import xml.etree.ElementTree as ET

from actor_ctest_receipt import EXPECTED, verify

class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.root = ET.Element('testsuite')
        for name in sorted(EXPECTED):
            ET.SubElement(self.root, 'testcase', name=name, status='run')

    def test_valid(self):
        verify(self.root)

    def test_reject_invalid_receipts(self):
        cases = {}
        root = deepcopy(self.root)
        root.remove(root[0])
        cases['missing'] = root
        root = deepcopy(self.root)
        root[0].set('name', root[1].get('name'))
        cases['duplicate'] = root
        root = deepcopy(self.root)
        root[0].set('name', 'unexpected')
        cases['unexpected'] = root
        root = deepcopy(self.root)
        root[0].set('status', 'notrun')
        cases['notrun'] = root
        root = deepcopy(self.root)
        del root[0].attrib['status']
        cases['missing status'] = root
        for tag in ('failure', 'error', 'skipped'):
            root = deepcopy(self.root)
            ET.SubElement(root[0], tag)
            cases[tag] = root
        self.assertEqual(len(cases), 8)
        for name, root in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    verify(root)

if __name__ == '__main__':
    unittest.main()
