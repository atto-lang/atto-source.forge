"""Compile closed legacy stage-0 source to C++, then link real native heap/frames.
No production compiler CLI, general actor syntax or suspension claim.
"""
import argparse
import pathlib
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--cxx', required=True)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
flags = ['-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O1' if a.sanitize else '-O2']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']

def run(cmd):
    return subprocess.run(cmd, check=True, timeout=30, cwd=root)

with tempfile.TemporaryDirectory() as tmp:
    work = pathlib.Path(tmp)
    emitter = work / 'emitter'
    heap = work / 'heap.o'
    run([a.cxx, '-std=c++20', *flags, '-I.', 'compiler/actor_source.cpp',
         'verification/scalar_actor_stage0_emit.cpp', '-o', str(emitter)])
    run([a.cc, '-std=c11', *flags, '-c', 'runtime/arena/ActorOwnedI64Heap.c', '-o', str(heap)])
    def source(initial, op):
        return f'define actor Counter as let int total: {initial} receive(int msg) -> int :: total = total {op} msg; return total; end end\n'
    def sealed_source(initial, op):
        return f'define actor Counter as state {{ let int total: {initial}; }} receive(int msg) -> int :: return state_w(|s| -> {{ s.total = s.total {op} msg; return s.total; }}); end end\n'
    def read_source():
        return 'define actor Counter as state { let int total: 42; } receive(int msg) -> int :: return state_ro(|s| -> s.total); end end\n'
    for label, initial, op, expected in [('add', 37, '+', [39,38,42,40,'INT64_MAX']),
                                          ('sub', 47, '-', [45,46,42,44,'INT64_MIN']),
                                          ('sealed_add', 37, '+', [39,38,42,40,'INT64_MAX']),
                                          ('sealed_sub', 47, '-', [45,46,42,44,'INT64_MIN'])]:
        src = work / (label + '.atto')
        src.write_text((sealed_source if label.startswith('sealed_') else source)(initial, op), encoding='ascii')
        cpp = work / (label + '.cpp')
        with cpp.open('wb') as out:
            subprocess.run([str(emitter), str(src)], stdout=out, check=True, timeout=10)
        binary = work / label
        macros = [f'-D{k}={v}' for k,v in zip(['FIRST','PEER_FIRST','SECOND','PEER_SECOND','OVERFLOW_INPUT'],expected)]
        run([a.cxx, '-std=c++20', *flags, '-pthread', '-I.', *macros, str(cpp), str(heap),
             'runtime/scalar_actor.cpp', 'verification/scalar_actor_stage0_guest.cpp', '-o', str(binary)])
        run([str(binary)])
    # Read-only compiled callback is exercised with the same real actor heap.
    src = work / 'sealed_read.atto'
    src.write_text(read_source(), encoding='ascii')
    cpp = work / 'sealed_read.cpp'
    with cpp.open('wb') as out:
        subprocess.run([str(emitter), str(src)], stdout=out, check=True, timeout=10)
    binary = work / 'sealed_read'
    run([a.cxx, '-std=c++20', *flags, '-pthread', '-I.', str(cpp), str(heap),
         'runtime/scalar_actor.cpp', 'verification/scalar_actor_stage0_read_guest.cpp', '-o', str(binary)])
    run([str(binary)])
    for bad in [sealed_source(0, '+').replace('state_w', 'state_ro'),
                sealed_source(0, '+').replace('s.total = s.total + msg', 'total = total + msg'),
                sealed_source(0, '+').replace('return s.total;', 'return s;'),
                sealed_source(0, '+').replace('|s|', '|msg|'),
                sealed_source(0, '+').replace('return s.total;', 'await msg; return s.total;'),
                sealed_source(0, '+').replace('return s.total;', 'return state_ro(|x| -> x.total);'),
                source(2**63, '+'), source(-2**63-1, '-'), source(0, '+') + ' junk',
                source(0, '+').replace('total + msg','await msg'),
                source(0, '+').replace('Counter','await'), ' ' * 8193,
                *[source(0, '+').replace('Counter', word) for word in ('panic', 'raise', 'throw', 'trap')]]:
        src = work / 'denied.atto'
        src.write_text(bad, encoding='ascii')
        result = subprocess.run([str(emitter), str(src)], capture_output=True, timeout=10)
        if result.returncode != 1 or result.stdout or b'actor source:' not in result.stderr:
            raise RuntimeError('invalid source was not rejected before emission')
    print('FORGE_STAGE0_NATIVE: 5 standalone guests, 16 source denials PASS')
