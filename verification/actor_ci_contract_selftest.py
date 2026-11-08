#!/usr/bin/env python3
"""Mutation-test the strict Forge actor Actions workflow admission gate.

Host-only workflow verification; this does not compile or execute guest actors.
"""
from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import sys

from actor_ci_contract import check


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    workflow = (Path(sys.argv[1]) if len(sys.argv) == 2 else
                root / '.github/workflows/forge-actor-owner-v1.yml')
    source = workflow.read_text(encoding='utf-8')
    check(root, workflow)

    mutations = {
        'truncated_ctest_quote': (
            "-R '^oresforge_actor_incarnation_(owner|memory)_v1_host$'",
            "-R '^oresforge_actor_incarnation_(owner|memory)_v1_host"),
        'duplicate_success_receipt': (
            "grep -Fx 'FORGE_ACTOR_OWNER_HOST_PASS|turn=1|stop_restart=1|sibling=42|guest_atto=0|physical_stack=0'",
            "grep -Fx 'FORGE_ACTOR_OWNER_HOST_PASS|turn=1|stop_restart=1|sibling=42|guest_atto=0|physical_stack=0'\n"
            "          grep -Fx 'FORGE_ACTOR_OWNER_HOST_PASS|turn=1|stop_restart=1|sibling=42|guest_atto=0|physical_stack=0'"),
        'unanchored_ctest_selection': (
            "-R '^oresforge_actor_incarnation_(owner|memory)_v1_host$'",
            "-R 'actor'"),
        'persistent_checkout_credentials': (
            '          persist-credentials: false',
            '          persist-credentials: true'),
        'missing_native_sanitizer': (
            '-fsanitize=address,undefined', '-O0'),
        'inserted_unreviewed_action': (
            '      - name: Read back exact immutable source identity',
            '      - uses: attacker/unreviewed@0123456789012345678901234567890123456789\n'
            '      - name: Read back exact immutable source identity'),
        'job_level_write_token': (
            '    runs-on: ubuntu-24.04',
            '    permissions:\n      contents: write\n    runs-on: ubuntu-24.04'),
    }
    with TemporaryDirectory(prefix='forge-actor-ci-negative-') as temp:
        for case, (old, replacement) in mutations.items():
            if source.count(old) != 1:
                raise AssertionError('Nonunique mutation location: ' + case)
            candidate = Path(temp) / (case + '.yml')
            candidate.write_text(source.replace(old, replacement), encoding='utf-8')
            try:
                check(root, candidate)
            except AssertionError:
                print('FORGE_ACTOR_CI_NEGATIVE_REJECTED=' + case)
            else:
                raise AssertionError('CI workflow mutation was admitted: ' + case)
    print('FORGE_ACTOR_CI_NEGATIVE_MATRIX_PASS=7/7')


if __name__ == '__main__':
    main()
