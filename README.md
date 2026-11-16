# atto-source.forge

Independent AttoLang execution engine with JIT, AOT, and native runtime infrastructure.

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

AttoLang is the public name; preserve existing Oreslang repository, CLI and `.ores` compatibility while specifying new portable source in `.atto`. Do not duplicate normative semantics from [atto-spec](https://github.com/atto-lang/atto-spec).

**Evidence gate:** record exact compiler/runtime head and tree, target/toolchain, nonzero executed steps, positive and negative guest results, and explicit passed/failed/unsupported/not-executed states. Host-only CI and `steps=[]` do not establish guest support. README scope is not a feature-completeness claim.
