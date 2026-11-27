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
    # The canonical aliases must emit byte-identical native code, not just
    # coincidentally produce the same scalar result.
    alias_cases = [
        (sealed_source(37, '+'), 'state_w(', ['state_w(', 'handle_state_w(',
          'handle_state(critical-write ', 'handle_state(critical '], 'sealed_add'),
        (read_source(), 'state_ro(', ['state_ro(', 'handle_state_ro(',
          'handle_state(critical-read '], 'sealed_read'),
    ]
    for canonical, old, aliases, label in alias_cases:
        expected_cpp = (work / (label + '.cpp')).read_bytes()
        for alias in aliases:
            for prefix in ('', 'self.'):
                src = work / 'alias.atto'
                src.write_text(canonical.replace(old, prefix + alias), encoding='ascii')
                got = subprocess.run([str(emitter), str(src)], capture_output=True, check=True, timeout=10)
                if got.stdout != expected_cpp:
                    raise RuntimeError('alias changed verified native output')
    for bad in [sealed_source(0, '+').replace('state_w', 'state_ro'),
                sealed_source(0, '+').replace('s.total = s.total + msg', 'total = total + msg'),
                sealed_source(0, '+').replace('return s.total;', 'return s;'),
                sealed_source(0, '+').replace('|s|', '|msg|'),
                sealed_source(0, '+').replace('return s.total;', 'await msg; return s.total;'),
                sealed_source(0, '+').replace('return s.total;', 'return state_ro(|x| -> x.total);'),
                sealed_source(0, '+').replace('state_w(', 'other.state_w('),
                sealed_source(0, '+').replace('state_w(', 'state_w(critical-write '),
                sealed_source(0, '+').replace('state_w(', 'handle_state(critical -write '),
                sealed_source(0, '+').replace('|s|', '|_|').replace('s.total', '_.total'),
                sealed_source(0, '+').replace('|s|', '|Counter|').replace('s.total', 'Counter.total'),
                read_source().replace('state_ro(', 'handle_state(critical-write '),
                source(2**63, '+'), source(-2**63-1, '-'), source(0, '+') + ' junk',
                source(0, '+').replace('total + msg','await msg'),
                source(0, '+').replace('Counter','await'), ' ' * 8193,
                *[source(0, '+').replace('Counter', word) for word in ('panic', 'raise', 'throw', 'trap')]]:
        src = work / 'denied.atto'
        src.write_text(bad, encoding='ascii')
        result = subprocess.run([str(emitter), str(src)], capture_output=True, timeout=10)
        if result.returncode != 1 or result.stdout or b'actor source:' not in result.stderr:
            raise RuntimeError('invalid source was not rejected before emission')
    # Exercise the trusted native heap ABI independently of parser admission.
    # Each denial runs in a fresh process: native invariant failures abort.
    probe = work / 'sealed_heap_probe.c'
    probe.write_text(r'''
#include "runtime/arena/ActorOwnedI64Heap.h"
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    OresActorI64Heap* h = ores_actor_i64_heap_open(1, 1, 2);
    OresActorTurn t = ores_actor_i64_heap_enter(h, 1, 1);
    ores_actor_i64_heap_field_init_protected(h, t.domain, t.epoch, 0, 40);
    ores_actor_i64_heap_field_init_mutable(h, t.domain, t.epoch, 1, 10);
    OresActorSlot forged = {t.domain, 0, 1};
    if (!strcmp(argv[1], "field-read")) (void)ores_actor_i64_heap_field_read(h,t.domain,t.epoch,0);
    else if (!strcmp(argv[1], "field-write")) ores_actor_i64_heap_field_write(h,t.domain,t.epoch,0,99);
    else if (!strcmp(argv[1], "slot-read")) (void)ores_actor_i64_heap_read(h,t,forged);
    else if (!strcmp(argv[1], "slot-write")) ores_actor_i64_heap_write(h,t,forged,99);
    else if (!strcmp(argv[1], "slot-borrow")) (void)ores_actor_i64_heap_borrow(h,t,forged);
    else if (!strcmp(argv[1], "slot-borrow-mut")) (void)ores_actor_i64_heap_borrow_mut(h,t,forged);
    else if (!strcmp(argv[1], "slot-drop")) ores_actor_i64_heap_drop(h,t,forged);
    else if (!strcmp(argv[1], "readonly-write")) {
        OresActorLoan r = ores_actor_i64_heap_field_borrow(h,t,0);
        ores_actor_i64_heap_loan_write(h,t,r,99);
    } else if (!strcmp(argv[1], "ok")) {
        OresActorLoan r = ores_actor_i64_heap_field_borrow(h,t,0);
        if (ores_actor_i64_heap_loan_read(h,t,r)!=40) return 3;
        ores_actor_i64_heap_release(h,r);
        OresActorLoan w = ores_actor_i64_heap_field_borrow_mut(h,t,0);
        ores_actor_i64_heap_loan_write(h,t,w,42);
        if (ores_actor_i64_heap_loan_read(h,t,w)!=42) return 4;
        ores_actor_i64_heap_release(h,w);
        ores_actor_i64_heap_field_write(h,t.domain,t.epoch,1,11);
        if (ores_actor_i64_heap_field_read(h,t.domain,t.epoch,1)!=11) return 5;
    } else return 2;
    ores_actor_i64_heap_leave(h,t);
    ores_actor_i64_heap_retire(h);
    return ores_actor_i64_heap_try_destroy(h) ? 0 : 6;
}
''', encoding='ascii')
    binary = work / 'sealed_heap_probe'
    run([a.cc, '-std=c11', *flags, '-I.', str(probe), str(heap), '-o', str(binary)])
    run([str(binary), 'ok'])
    for mode in ('field-read','field-write','slot-read','slot-write','slot-borrow',
                 'slot-borrow-mut','slot-drop','readonly-write'):
        result = subprocess.run([str(binary), mode], capture_output=True, timeout=10)
        if result.returncode != -6 or result.stdout or b'ERROR: AddressSanitizer' in result.stderr or b'runtime error:' in result.stderr:
            raise RuntimeError('expected native invariant abort: ' + mode)
    print('FORGE_STAGE0_NATIVE: 5 standalone guests, 14 alias emission controls, 22 source denials, 9 heap authority controls PASS')
