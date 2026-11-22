# core/types v1 — Forge JIT/AOT engine

This compiler owns its own `core/types.atto`; no backend is required to import byte-identical sources from another compiler or from a shared repository.

Public contract: logical value bit widths for bool, int and byte; signed-64-bit int min/max; `fits_i8/i16/i32/i64/u8/u16/u32/u64(int)` range admission. These functions cannot enforce real sized integer types, cast values, describe an ABI layout, or reflect over arbitrary objects. `fits_u64` only checks whether a *signed int* input is representable.

`void` is valueless and not a first-class `Unit`. Opaque `TypeId`, static `TypeInfo<T>`, real `Unit`, and checked sized-integer representations must come from explicit compiler mechanisms in separate work; no invented reflection or host fallback.

Conformance fixture: compile/link the actual `edge-positive.atto` and execute the guest (exit 0). Reject `void-value-negative.atto` at compile time and emit no guest artifact. Report exact source/head SHA and nonzero runner steps. **NOT_EXECUTED:** this is a staged library surface, not verified compiler/AOT parity.

## Namespace-only core source

Canonical `use namespace types;` file header plus public top-level functions; no `define module` wrapper. Consumers still explicitly import the symbols. Compiler parse, namespace source identity, static imports, and executed linked guests require separate proofs. Until the current PR compiles and runs, namespace guest parity remains NOT_EXECUTED.

## Compiler-backed core/types admission gate

An additional exact-head Actions job now invokes the **real forge compiler** directly on the unchanged backend-owned `core/types.atto`. It fails closed when namespace, type or multi-function lowering is unsupported; there is no source-only/fake-guest fallback. This deliberately exposes incomplete parity rather than reporting passing guest semantics. Stage-0 Forge parser/interpreter only. Native JIT/AOT and imported-file guest execution require separate proofs. No claim of compiler guest success is made without executed runner steps and independently verified output. Keep the PR draft until semantic positive/negative checks and target-appropriate runtime execution are proven.
