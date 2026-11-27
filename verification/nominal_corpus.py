#!/usr/bin/env python3
"""Run the pinned 120-case corpus through the actual bounded Forge CLI.

The compiler digest binds the binary, not its provenance. The caller must
independently establish the binary-to-source/build relationship.
Exit 0 = all cases pass; 1 = failure; 2 = unsupported coverage remains.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile

CORPUS_BLOB = '9fa4f22c0b1ea9468aad2657cfff7b5b02869cde'
DIAGNOSTICS = {
    'OBSOLETE_METHOD_INTRODUCER': b'obsolete method introducer',
    'DUPLICATE_INSTANCE_METHOD': b'duplicate native method name/arity',
    'MISSING_INSTANCE_OBLIGATION': b'MISSING_INSTANCE_OBLIGATION',
}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*args):
    return subprocess.run(args, capture_output=True, timeout=15, check=False)


def verify(manifest, compiler, compiler_sha, linker):
    data = manifest.read_bytes()
    if hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest() != CORPUS_BLOB:
        raise ValueError('wrong corpus Git blob')
    if not re.fullmatch(r'[0-9a-f]{64}', compiler_sha) or digest(compiler) != compiler_sha:
        raise ValueError('wrong compiler SHA-256')
    corpus = json.loads(data)
    rows = []
    for case in corpus['cases']:
        with tempfile.TemporaryDirectory(prefix='forge-corpus-') as tmp:
            work = pathlib.Path(tmp)
            src = work / 'main.atto'
            src.write_bytes(case['source'].encode('utf-8'))
            a = run(compiler, 'run-class', src)
            b = run(compiler, 'emit-class-asm', 'x86_64', src)
            row = {'id': case['id'], 'status': 'FAIL', 'run_exit': a.returncode,
                   'emit_exit': b.returncode}
            if case['expect'] == 'compile_and_run':
                if (a.returncode == b.returncode == 0 and a.stdout.strip() == b'42'
                        and b.stdout and not a.stderr and not b.stderr):
                    asm = work / 'main.s'
                    asm.write_bytes(b.stdout)
                    driver = work / 'main.c'
                    driver.write_text('#include <stdint.h>\nextern int64_t oresforge_entry(void);\n'
                                      'int main(void){return oresforge_entry()==42?0:1;}\n')
                    guest = work / 'guest'
                    linked = run(linker, '-x', 'assembler', asm, '-x', 'c', driver, '-o', guest)
                    if linked.returncode == 0 and run(guest).returncode == 0:
                        row['status'] = 'PASS_GUEST'
                        row['assembly_sha256'] = hashlib.sha256(b.stdout).hexdigest()
            elif (a.returncode == b.returncode == 1 and not a.stdout and not b.stdout
                  and a.stderr == b.stderr):
                expected = DIAGNOSTICS[case['diagnostic_family']]
                if expected in a.stderr:
                    row['status'] = 'PASS_DIAGNOSTIC'
                elif (case['id'].startswith(('actor.', 'struct.'))
                      and b'DECL_KIND_UNSUPPORTED' in a.stderr
                      and b'obsolete method introducer' not in a.stderr):
                    row['status'] = 'UNSUPPORTED_PROFILE'
                row['diagnostic'] = a.stderr.decode('utf-8', errors='replace').strip()
            rows.append(row)
    if digest(compiler) != compiler_sha:
        raise ValueError('compiler digest changed during verification')
    return {'corpus_git_blob': CORPUS_BLOB, 'compiler_sha256': compiler_sha,
            'profile': 'bounded-forge-nominal-x86_64',
            'counts': dict(collections.Counter(row['status'] for row in rows)),
            'cases': rows}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifest', type=pathlib.Path)
    p.add_argument('compiler', type=pathlib.Path)
    p.add_argument('compiler_sha256')
    p.add_argument('linker', type=pathlib.Path)
    args = p.parse_args()
    result = verify(args.manifest, args.compiler.resolve(strict=True), args.compiler_sha256,
                    args.linker.resolve(strict=True))
    print(json.dumps(result, indent=2))
    counts = result['counts']
    if counts.get('FAIL', 0) or len(result['cases']) != 120:
        return 1
    return 2 if counts.get('UNSUPPORTED_PROFILE', 0) else 0


if __name__ == '__main__':
    raise SystemExit(main())
