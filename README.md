# atto-source.forge

Independent AttoLang execution engine with JIT, AOT, and native runtime infrastructure.

## Naming and specification migration

This is the canonical [AttoLang backend repository](https://github.com/atto-lang/atto-source.forge). Use `.atto` for portable AttoLang source and update local remotes to `https://github.com/atto-lang/atto-source.forge.git`; do not use historical repository links or naming in new examples.

- **Normative grammar and semantics:** [atto-spec](https://github.com/atto-lang/atto-spec). Check the current versioned contracts for ownership operations (`borrow!`, `take!`, `share!`, `copy!`, `copy!!`), implicit argument borrowing, `lt!` binders, `eq`/`neq` value equality, `susp` effects and file/module `define const`. These are *specification targets*, not claims of executable support in this backend.
- **Positive and negative guest execution evidence:** [atto-platform-conformance](https://github.com/atto-lang/atto-platform-conformance) and [atto-compilers-parity-testing](https://github.com/atto-lang/atto-compilers-parity-testing). Report `passed`, `failed`, `unsupported` or `not executed`, and pin exact compiler, source tree, target and toolchain.
- **Portable shared sources and target constraints:** [atto-common-source](https://github.com/atto-lang/atto-common-source) and [atto-target-profiles](https://github.com/atto-lang/atto-target-profiles). Keep backend-specific gaps explicit, and do not infer guest parity from host tests or zero-step CI runs.

## Scope

This repository is the AttoLang ecosystem home for:

- Independent runtime semantics, JIT/AOT compilation experiments, actors, and backend conformance.
- Compatibility with the [canonical language specification](https://github.com/atto-lang/atto-spec) where applicable.
- Evidence-based tests, benchmark results, and cross-backend conformance reporting.

## Architecture and compatibility

AttoLang is designed around actors, explicit ownership, and portable concurrency semantics. Backend-specific optimizations must not silently change the shared language contract. Runtime capabilities, target restrictions, and unsupported features should be documented explicitly.

## Building and testing

Consult the checked-in source tree and CI workflows for verified commands and supported targets. Add target-specific build instructions and exact-head execution results as they become available; do not treat a passing host-unit test as proof of guest-language conformance.

## Project status

This README describes the repository's intended responsibilities. Feature support and release readiness must be established from implementation, tests, and CI evidence rather than this description alone.

## Related repositories

- [AttoLang organization](https://github.com/atto-lang)
- [Language specification](https://github.com/atto-lang/atto-spec)
- [Platform conformance](https://github.com/atto-lang/atto-platform-conformance)
- [Commercial infrastructure](https://github.com/atto-co)

## Checked i64 native primitive (preview)

`include/atto/checked_i64.hpp` provides portable C++20 `checked_add`, `checked_sub`, and `checked_mul` returning `std::optional<int64_t>` on overflow, without signed-overflow UB or heap allocation. The unsigned-magnitude multiplication path handles `INT64_MIN * -1` explicitly.

Build the independent primitive tests with `cmake -S . -B build -DATTO_FORGE_SANITIZERS=ON && cmake --build build && ctest --test-dir build --output-on-failure`. CI exercises GCC and Clang with ASan/UBSan. These are native library tests only: no Forge compiler, actor runtime, AOT code emission, or Atto guest execution has been implemented or verified by this PR.

## Repository ownership and contribution routing

**Belongs here:** Independent Forge execution backend, native code generation, runtime ABI, actor/scheduler integration, AOT/JIT experiments and backend conformance receipts.

**Belongs elsewhere:** Other backends' source and canonical language definitions belong in atto-source.* and atto-spec; common contracts belong in atto-common-source.

**Security and portability boundary:** Target-specific JIT is optional and governed; isolate untrusted tenant capabilities, bound heap/stack/fuel and distinguish emitted native machine code from executed AttoLang guests.

AttoLang is the canonical project identity. Use `.atto` source, `atto-*` package and repository names, and the active AttoLang CLI contract for current documentation. Do not duplicate normative semantics from [atto-spec](https://github.com/atto-lang/atto-spec).

**Evidence gate:** record exact compiler/runtime head and tree, target/toolchain, nonzero executed steps, positive and negative guest results, and explicit passed/failed/unsupported/not-executed states. Host-only CI and `steps=[]` do not establish guest support. README scope is not a feature-completeness claim.

## Restricted compiled scalar .atto guest (draft, Python bootstrap not merge-ready)

> **Not for merge:** this draft uses a Python prototype and Python tests, which must be replaced by native Forge compiler code and appropriate non-Python tests before readiness.

`python tools/forge_scalar.py fixtures/scalar-v1/arithmetic.atto -o /tmp/atto-scalar.cpp` parses a restricted single-`main` `.atto` function with a single `return` expression of signed 64-bit decimal literals, parentheses, unary +/- and binary +, -, *. It emits deterministic C++20 that uses Forge's `checked_i64.hpp` helpers, prints the resulting signed value followed by a newline and exits 0. Checked overflow emits `E_OVERFLOW` to stderr and exits 70. Unsupported language syntax fails before emitting code (exit 2). The compiler removes any previous output artifact before parsing, refuses source/output aliases, and atomically publishes complete output to avoid stale or partial generated C++ after failure.

Build a native guest with `c++ -std=c++20 -I include /tmp/atto-scalar.cpp -o /tmp/atto-scalar && /tmp/atto-scalar`. Use `CXX=clang++ python -m unittest discover -s tests -p test_scalar_compiler.py -v` for compiler-produced guest execution with ASan/UBSan; the CI matrix also uses GCC.

**Limitations:** This is a Python bootstrap frontend compiling a **single scalar expression** to native C++; not the independent Forge JIT/AOT engine, full AttoLang semantics, actor runtime, package linker, or cross-backend parity. Feature PR #2 is already merged; require a native rewrite, real compiled-guest CI, and independent approval.
