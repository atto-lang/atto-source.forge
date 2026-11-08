#!/usr/bin/env python3
"""Fail closed on Forge actor workflow syntax and missing exact test evidence.

This is a host-side workflow contract, not C++ or .atto guest execution.
Uses only Python stdlib and bash, so it runs before native compilation.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys


def require(ok: bool, why: str) -> None:
    if not ok:
        raise AssertionError('Forge actor CI contract: ' + why)


def run_blocks(source: str) -> list[str]:
    lines = source.splitlines()
    starts = [i for i, line in enumerate(lines) if line == '        run: |']
    require(len(starts) == 2, 'expected exactly two Bash run blocks')
    blocks: list[str] = []
    for start in starts:
        body: list[str] = []
        for line in lines[start + 1:]:
            if line and not line.startswith('          '):
                break
            body.append(line[10:] if line else '')
        require(bool(body), 'empty Bash run block')
        blocks.append('\n'.join(body) + '\n')
    return blocks


def check(root: pathlib.Path, workflow: pathlib.Path) -> None:
    source = workflow.read_text(encoding='utf-8')
    require(source.startswith('name: Forge actor incarnation lifecycle owner host verification\n'),
            'wrong workflow identity')
    require('\non:\n  pull_request:\n' in source and '\n  workflow_dispatch:\n' in source,
            'missing intended PR/manual trigger')
    require('pull_request_target:' not in source,
            'privileged PR target trigger forbidden for private source CI')
    require('    runs-on: ubuntu-24.04\n' in source,
            'missing hosted Linux runner selection')
    require('        compiler: [g++, clang++]' in source and
            '        mode: [optimized, sanitized]' in source,
            'missing GCC/Clang optimized/sanitized test matrix')
    # Keep private source off unreviewed Actions: only one pinned checkout
    # and two audited local shell steps are authorized for this workflow.
    uses = re.findall(r'^      - uses: ([^\\s#]+)\\s*$', source, re.MULTILINE)
    require(uses == ['actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683'],
            'unexpected or unpinned third-party action')
    require(len(re.findall(r'^      - ', source, re.MULTILINE)) == 3 and
            source.count('      - name: ') == 2,
            'unexpected workflow step outside the three reviewed steps')
    require(source.count('\\npermissions:\\n  contents: read\\njobs:\\n') == 1 and
            len(re.findall(r'^\\s*permissions:', source, re.MULTILINE)) == 1,
            'unexpected workflow or job token permissions')
    require('          persist-credentials: false' in source,
            'source checkout must not persist GitHub token')
    require("          ref: ${{ github.event.pull_request.head.sha || github.sha }}" in source,
            'checkout must pin the exact PR head')

    admission, test = run_blocks(source)
    for name, body in (('admission', admission), ('build', test)):
        result = subprocess.run(['bash', '-n'], input=body, text=True,
                                capture_output=True, check=False)
        require(result.returncode == 0,
                name + ' shell syntax failed: ' + result.stderr.strip())
        require(body.startswith('set -euo pipefail\n'),
                name + ' script does not fail closed')
    require('test "$(git rev-parse HEAD)" = "$EXPECTED"' in admission,
            'missing exact-head checkout admission')
    require("git rev-parse 'HEAD^{tree}'" in admission,
            'missing immutable Git tree provenance')
    require(test.count('cmake --build "$build" --target ') == 1 and
            'oresforge_actor_incarnation_owner_v1 oresforge_actor_incarnation_memory_v1' in test,
            'both native targets must build in one exact-head tree')
    require(test.count('ctest --test-dir ') == 1 and
            "-R '^oresforge_actor_incarnation_(owner|memory)_v1_host$'" in test,
            'one anchored CTest filter must select both cases')
    require('-fsanitize=address,undefined' in test and '-fno-omit-frame-pointer' in test,
            'sanitized test mode missing')
    markers = (
        'FORGE_ACTOR_OWNER_HOST_PASS|turn=1|stop_restart=1|sibling=42|guest_atto=0|physical_stack=0',
        'FORGE_ACTOR_INCAR_MEMORY_HOST_PASS|private_heap=1|logical_stack=1|guest_atto=0|physical_stack=0',
    )
    for marker in markers:
        require(test.count("grep -Fx '" + marker + "'") == 1,
                'missing or duplicate exact success marker: ' + marker)
    require(test.count('"$build/oresforge_actor_incarnation_owner_v1"') == 1 and
            test.count('"$build/oresforge_actor_incarnation_memory_v1"') == 1,
            'host binary must run once per test')
    require('test "$(git rev-parse HEAD)" = "${{ github.event.pull_request.head.sha || github.sha }}"' in test,
            'missing post-test source head readback')

    cmake = root / 'CMakeLists.txt'
    if cmake.exists():
        value = cmake.read_text(encoding='utf-8')
        for target in ('oresforge_actor_incarnation_owner_v1', 'oresforge_actor_incarnation_memory_v1'):
            require('add_executable(' + target in value,
                    'CTest target executable not registered: ' + target)
            require('COMMAND ' + target + ')' in value,
                    'CTest binary command not registered: ' + target)
        print('FORGE_ACTOR_CI_CMAKE_TARGETS_PASS')
    for filename, marker in (
        ('verification/actor_incarnation_owner_v1_test.cpp', 'FORGE_ACTOR_OWNER_HOST_PASS'),
        ('verification/actor_incarnation_memory_v1_test.cpp', 'FORGE_ACTOR_INCAR_MEMORY_HOST_PASS'),
    ):
        path = root / filename
        if path.exists():
            require(marker in path.read_text(encoding='utf-8'),
                    'fixture must emit its required marker: ' + filename)
    print('FORGE_ACTOR_CI_SHELL_SYNTAX_PASS')
    print('FORGE_ACTOR_CI_EXACT_HEAD_AND_RECEIPT_PASS')


if __name__ == '__main__':
    root = pathlib.Path(__file__).resolve().parents[1]
    chosen = pathlib.Path(sys.argv[1]) if len(sys.argv) == 2 else root / '.github/workflows/forge-actor-owner-v1.yml'
    check(root, chosen)
