# Forge stage-0 `with` fail-closed admission v1

Contextual `with {...}` is now diagnosed by the **real Forge C++ lexer**,
skipping whitespace, line comments and nested block comments while leaving
`with(...)`, inert comments and lookalike words alone. The diagnostic is
`WITH_BACKEND_UNSUPPORTED`; the bootstrap compiler does not emit IR for it.

The regression script `tests/test_with_ownership_v1.sh /path/to/oresforge`
runs four actual `.atto` fixtures and requires two diagnosed compile
rejections and two positive baseline scalar IR results. Do not count a
host-only parser simulation or scalar constant fold as `with` execution.

Full parity requires typed `WithExpr` IR, actor-owned zero-GC allocation,
explicit copy/move/drop capability proofs, class identity and immutable
field updates, interpreter + native JIT/AOT guest assertions, and the
identical conformance fixtures from platform-conformance PR #693.

**NOT EXECUTED on this exact head:** full Forge compiler tests, native AOT
`with`, LLVM/JVM/.NET/Wasm parity. No source transferred to another org.
