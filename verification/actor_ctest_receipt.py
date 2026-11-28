#!/usr/bin/env python3
"""Require exactly seven executed Forge host CTests from CTest JUnit XML."""
import sys
import xml.etree.ElementTree as ET

EXPECTED = frozenset({
    'oresforge_actor_incarnation_owner_v1_host',
    'oresforge_actor_incarnation_memory_v1_host',
    'oresforge_supervisor_data_fence_parity_v1_host',
    'oresforge_mailbox_incarnation_reuse_v1_host',
    'oresforge_monotonic_domain_v1_host',
    'oresforge_actor_arena_domain_reuse_v1_host',
    'oresforge_actor_owner_lifetime_v1_host',
})

def verify(root):
    cases = root.findall('.//testcase')
    names = [case.get('name') for case in cases]
    if len(names) != len(EXPECTED) or set(names) != EXPECTED:
        raise ValueError('missing, duplicate or unexpected actor CTest names')
    for case in cases:
        if case.get('status') != 'run' or any(
                case.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            raise ValueError('actor CTest did not execute successfully: ' + case.get('name', '?'))

if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: actor_ctest_receipt.py RECEIPT.xml')
    verify(ET.parse(sys.argv[1]).getroot())
    print('FORGE_ACTOR_CTEST_STRUCTURED_PASS=7/7')
