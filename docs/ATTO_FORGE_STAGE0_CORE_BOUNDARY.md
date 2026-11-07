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
