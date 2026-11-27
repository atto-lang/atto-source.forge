# atto-source.forge std.fs API and byte-I/O policy candidate

The compiler owns `stdlib/std/fs_api_policy.atto` and
`stdlib/std/fs_io_policy.atto` as **independent native Atto source**.
No central shared-source compiler import is required. The first file
contains 11 pure rules for valid OpenOptions, owned handle generation,
bounded file reads, copy/dir quotas and recursive-delete admission.
The second contains five pure rules for chunk selection, partial write
progress, read/EOF bounds and append budgets.

Pure Atto regression fixtures:
- `fixtures/fs/fs_api_policy_main.atto`: 32 bounded guards; exit 42 target
- `fixtures/fs/fs_io_policy_main.atto`: 31 bounded guards; exit 42 target

**Status: SOURCE_ONLY / COMPILED_GUEST_NOT_EXECUTED.** These are not
`std.fs` host calls, an actual file/stream API implementation, a real
compiled guest receipt, or a grant of filesystem authority. Forge source policy is independent and requires native Forge compiler admission.

Native qualification (Linux x86_64, GNU coreutils):
```sh
bash verification/fs_policy_native_guest.sh "$(realpath build/oresforge)" "$(command -v clang++)"
```
The gate composes each unchanged library and fixture into a derived source unit,
runs `emit-call-asm`, links and executes the actual machine code, and requires
exact stdout `42\n` and successful exit from both guests. Compiler rejection,
partial emission, linking failure, timeout or guest mismatch is a failing gate,
not a source-only pass. It deliberately does not claim import resolution.

Exact prior-head alternate CI:
[run 45938950293](https://github.com/ClaimGraph-test/atto-ci/actions/runs/45938950293)
passed 83/83 existing CTest cases; [run 45940617819](https://github.com/ClaimGraph-test/atto-ci/actions/runs/45940617819)
rejected both composed policy guests in the callable frontend, before emission.
At that head, `fs_api_policy` failed at byte 248 (Boolean formal), and
`fs_io_policy` at byte 524 (imperative if). Do not rewrite Boolean parameters
as integers or translate fixtures to a different dialect to claim parity.
Coordinate the typed callable work in #256 and conditional/guard work in
#246/#247; those PRs are not evidence of full Boolean-parameter support.


Canonical user import: `import * as fs from "std.fs";`, without
`pkg`. The compiler must resolve only its own registered standard
library; never interpret `std.fs` as a local file or external package.

The full API target is [atto-spec #117](https://github.com/atto-lang/atto-spec/pull/117).
The eight-backend SHA-pinned source manifest and all-backend guest gates
are in [atto-platform-conformance #101](https://github.com/atto-lang/atto-platform-conformance/pull/101).
Wasm [#43](https://github.com/atto-lang/atto-source.wasm/pull/43)
and JavaScript [#39](https://github.com/atto-lang/atto-source.js/pull/39)
contain separate, memory-only host VFS implementations; those are not
Atto guest import links.

A production adapter must independently authenticate actor incarnation,
intersect READ/WRITE/directory/remove/recursive permissions, confine
all operations to a tenant-granted root without symlink/TOCTOU escape,
enforce byte/handle quotas, honor cancellation, avoid blocking actor
carriers, and return typed FsError results. Tests must cover exact-head
compiled guest success and denial paths, not simply source equivalence.
