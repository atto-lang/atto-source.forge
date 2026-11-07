# Forge canonical AttoLang scalar-stage execution boundary

This proof invokes **real Forge** `oresforge` on a tracked `.atto`
fixture: `run` must return 42; `ir` and `emit-asm x86_64` must
emit nonempty outputs. Negative fixtures importing `atto.core`,
`atto.alloc`, `atto.std` and `atto.runtime.actor` must produce
failure diagnostics **without partial IR**. The parser must not pretend
that the identical `contracts/atto-core-v1/numeric.atto` source
snapshot is an active compiler-owned import.

Executed CTest and CI steps use pinned PR HEAD and source tree with
fixture Git blobs and a log marker. This is **scalar frontend/interpreter
execution plus native assembly generation **and actual linked x86-64 native guest execution** (with a distinct-source 43 mutation), not
Cortex-M freestanding, import graph or canonical stdlib parity.
`native_exec=EXECUTED` applies only to the x86-64 scalar guest; canonical module imports remain unsupported until
typed linking and backend ABI lowering exist.

This gate prevents semantic overclaiming while a future true Forge
`atto.core` module registry is built.

## Native codegen execution hardening

The original stage-0 gate only produced assembly. The revised gate assembles and links the **actual emitted x86-64 assembly** to a C++ harness, checks it returns 42, then changes the guest AttoLang source to return 43 and checks a distinct compiled native binary returns 43. It logs SHA-256 digests for original assembly and executable. This is a real linked native scalar guest once CI executes, not freestanding, import graph or full AttoLang runtime parity. CTest passes the configured C++ compiler; the CI GCC/Clang matrix uses matching toolchains.

## ELF stack and position-independent native proof

The GNU/ELF x86-64 scalar emitter now emits `.note.GNU-stack` so its object does not request an executable stack. The independent integration test links the actual guest assembly with `-fPIE -pie`, uses `readelf` to require ELF type DYN and a non-executable `GNU_STACK`, and still executes results 42 and 43 from separate source programs. This gate is registered only on Linux x86-64; other architectures/host ABIs are not certified. A zero-step hosted CI run is not evidence that this test executed.

Runtime-argument x86-64 ELF emission now carries the same non-executable-stack note as the scalar emitter; the source-level regression checks both codegen paths. This note is platform-specific to GNU/ELF and does not certify ARM64/Linux, macOS or Windows guest execution.

## ELF auditor negative mutation cases

The native x86-64 integration script now self-tests its own `readelf`
`GNU_STACK` predicate before reading the real emitted guest: a normal
`RW` stack must pass, but an `RWE` stack and a missing GNU_STACK
header must fail. This defends against a vacuous stack check alongside
the separate 42→43 guest-source mutation and ELF PIE requirement.
All of these remain proposed executable CI checks until runner allocation
and exact-head test receipts are available.
