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

## Restricted compiled scalar .atto guest (stacked prototype)

`python tools/forge_scalar.py fixtures/scalar-v1/arithmetic.atto -o /tmp/atto-scalar.cpp` parses a restricted single-`main` `.atto` function with a single `return` expression of signed 64-bit decimal literals, parentheses, unary +/- and binary +, -, *. It emits deterministic C++20 that uses Forge's `checked_i64.hpp` helpers, prints the resulting signed value followed by a newline and exits 0. Checked overflow emits `E_OVERFLOW` to stderr and exits 70. Unsupported language syntax fails before emitting code (exit 2).

Build a native guest with `c++ -std=c++20 -I include /tmp/atto-scalar.cpp -o /tmp/atto-scalar && /tmp/atto-scalar`. Use `CXX=clang++ python -m unittest discover -s tests -p test_scalar_compiler.py -v` for compiler-produced guest execution with ASan/UBSan; the CI matrix also uses GCC.

**Limitations:** This is a Python bootstrap frontend compiling a **single scalar expression** to native C++; not the independent Forge JIT/AOT engine, full AttoLang semantics, actor runtime, package linker, or cross-backend parity. It is stacked on checked-i64 feature PR #2; merge parent first and require real compiled-guest CI and independent approval.
