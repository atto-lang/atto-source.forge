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
    'unsafe-after-lf-comment': 'fnc main():int{return 42;} // comment\nunsafe',
    'unsafe-after-block-comment': 'fnc main():int{return 42;} /* unsafe inside */ unsafe',
    'unsafe-after-nested-comment': 'fnc main():int{return 42;} /* outer /* unsafe */ outer */ unsafe',
}
MODES = [('run',), ('ir',), ('run-runtime',), ('ir-runtime',),
         ('emit-asm', 'x86_64'), ('emit-runtime-asm', 'x86_64'), ('run-calls',), ('emit-call-asm', 'x86_64')]


def execute(binary, mode, source):
    with tempfile.TemporaryDirectory(prefix='oresforge-unsafe-') as tmp:
        path = pathlib.Path(tmp) / 'input.ores'
        path.write_text(source + '\n', encoding='utf-8')
        return subprocess.run([binary, *mode, str(path)],
                              capture_output=True, text=True, timeout=20)


def execute_bytes(binary, mode, raw):
    with tempfile.TemporaryDirectory(prefix='oresforge-unsafe-bytes-') as tmp:
        path = pathlib.Path(tmp) / 'input.ores'
        path.write_bytes(raw)
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
        for name, suffix in (
                ('line-comment', ' // unsafe must stay comment'),
                ('nested-block-comment', ' /* outer /* unsafe */ outer */')):
            result = execute(binary, mode, 'fnc main():int{return 42;}' + suffix)
            if result.returncode != 0 or result.stdout.strip() != '42':
                raise AssertionError((mode, name, result.returncode,
                                      result.stdout, result.stderr))
    for mode in (('run',), ('run-calls',)):
        excessive_nesting = 'fnc main():int{return 42;} ' + ('/*' * 129) + ('*/' * 129)
        result = execute(binary, mode, excessive_nesting)
        if (result.returncode == 0 or result.stdout.strip() or
                'block comment nesting too deep' not in result.stderr):
            raise AssertionError((mode, 'nested-comment-budget', result.stderr))
    # Source bytes must be well-formed even when malformed sequences occur
    # inside comments. Both public parsers must fail before source execution.
    ordinary = b'fnc main():int{return 42;} // ignored'
    bad_bytes = {
        'invalid-lead': ordinary + bytes([0xff]),
        'overlong': ordinary + bytes([0xc0, 0xaf]),
        'non-scalar': ordinary + bytes([0xf4, 0x90, 0x80, 0x80]),
        'embedded-nul': ordinary + bytes([0]),
    }
    for mode in (('run',), ('run-calls',)):
        for name, source in bad_bytes.items():
            result = execute_bytes(binary, mode, source)
            expected = 'source contains NUL byte' if name == 'embedded-nul' else 'source is not valid UTF-8'
            if (result.returncode == 0 or result.stdout.strip() or
                    expected not in result.stderr):
                raise AssertionError((mode, name, result.returncode,
                                      result.stdout, result.stderr))
    print('Forge unsafe G0 lexical admission: PASS (80 mode/case rejects, 4 safe comments, 2 depth rejects, 8 malformed-source rejects)')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: unsafe_admission.py /path/to/oresforge')
    main(sys.argv[1])
