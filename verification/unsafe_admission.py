#!/usr/bin/env python3
"""Exact CLI source admission tests, not native AOT execution proof."""
import pathlib
import subprocess
import sys
import tempfile

INVALID = {
    'unsafe-callable': 'unsafe fnc main():int{return 42;}',
    'unsafe-block': 'fnc main():int{unsafe{return 42;}}',
    'unsafe-name': 'fnc unsafe():int{return 42;}',
    'unsafe-parameter': 'fnc main(unsafe:int):int{return 42;}',
    'unsafe-after-main': 'fnc main():int{return 42;} unsafe',
    'unsafe-after-cr-comment': 'fnc main():int{return 42;} // comment\runsafe',
    'unsafe-after-crlf-comment': 'fnc main():int{return 42;} // comment\r\nunsafe',
}
MODES = [('run',), ('ir',), ('run-runtime',), ('ir-runtime',),
         ('emit-asm', 'x86_64'), ('emit-runtime-asm', 'x86_64'), ('run-calls',), ('emit-call-asm', 'x86_64')]


def execute(binary, mode, source):
    with tempfile.TemporaryDirectory(prefix='oresforge-unsafe-') as tmp:
        path = pathlib.Path(tmp) / 'input.ores'
        path.write_text(source + '\n', encoding='utf-8')
        return subprocess.run([binary, *mode, str(path)],
                              capture_output=True, text=True, timeout=20)


def main(binary):
    for mode in MODES:
        for name, source in INVALID.items():
            result = execute(binary, mode, source)
            if (result.returncode == 0 or result.stdout.strip() or
                    'unsafe boundary unsupported:' not in result.stderr):
                raise AssertionError((mode, name, result.returncode,
                                      result.stdout, result.stderr))
    # Positives guard against a blanket substring scan. Only supported
    # scalar source is expected to run (no FFI or unsafe execution).
    for mode in (('run',), ('run-calls',)):
        result = execute(binary, mode,
                         'fnc main():int{return 42;} // unsafe must stay comment')
        if result.returncode != 0 or result.stdout.strip() != '42':
            raise AssertionError((mode, 'safe-comment', result.stderr))
    print('Forge unsafe G0 lexical rejection: PASS (CLI source subset only)')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: unsafe_admission.py /path/to/oresforge')
    main(sys.argv[1])
