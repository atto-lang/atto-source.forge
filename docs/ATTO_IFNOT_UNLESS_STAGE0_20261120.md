# Forge ifnot/unless stage-0 admission — 2026-11-20

Forge's executable stage-0 compiler supports a checked scalar `int` subset, **not** a typed Boolean conditional control-flow graph. AttoLang's `ifnot` and `unless` surface (proposed in [spec #174](https://github.com/atto-lang/atto-spec/pull/174)) must therefore **fail closed**, including in unreachable functions, until the compiler implements typed CFG branch merges, ownership-safe early exits, and real native lowering.

The token-level rejection is exact: comments and longer identifiers such as `unless_else` remain valid. Both `parse_source` and `parse_runtime_source` have negative and positive test controls in `verification/tests.cpp`. This PR reserves spelling but does **not** claim feature implementation, LLVM/JVM/NativeAOT parity, or measured performance.

### Related, intentionally independent work

- [Rust #479](https://github.com/atto-lang/atto-source.rs/pull/479) — executable typed `ifnot`/`unless` forms
- [.NET #181](https://github.com/atto-lang/atto-source.net/pull/181) — guarded source-to-SSA/CIL lowering
- [Forge #210](https://github.com/atto-lang/atto-source.forge/pull/210) — return-first callable syntax and separate parameter ABI admission

### Required before ready/merge

Execute real full-project CMake build and CTest `oresforge_unit` with GCC and Clang on the exact PR head, with real completed job steps, sanitizer checks and independent review. Since private-source export is currently disabled under Forge's independently reviewed policy, **do not mirror this private source to another organization without prior authorized snapshot+destination approval**. Tests authored here have **not** yet executed on a trusted runner.
