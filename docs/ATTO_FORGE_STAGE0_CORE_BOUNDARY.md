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
execution plus native assembly generation**, not linked native guest,
Cortex-M freestanding, import graph or canonical stdlib parity.
`native_exec=NOT_EXECUTED` and imports remain unsupported until
typed linking and backend ABI lowering exist.

This gate prevents semantic overclaiming while a future true Forge
`atto.core` module registry is built.
