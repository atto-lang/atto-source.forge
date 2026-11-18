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

## Native ownership host-reference model (PR in progress)

The header-only C++20 include/atto/ownership_v1.hpp provides a native
ownership model with **concrete** data-structure behavior, not a parsed Atto
compiler or generated guest bytecode:

- UniqueGraph performs bounded whole-graph validation, iterative cycle
  traversal, duplicate/missing-node rejection and value-allocated deep copy
  preserving internal topology while allocating independent nested buffers.
- Consuming freeze publishes an immutable graph with unique owner authority.
  Shared readers only obtain const graph views. Weak handles perform atomic
  shared_ptr lifetime claims plus domain/generation and retirement checks.
- Pinned<T> keeps its heap pointee at a stable address across handle and
  thread moves; OwnedResource<T>::drop_now() uses unique ownership and RAII
  to release exactly once. The source borrow checker still owns loan rules.

The tests/ownership_v1.cpp binary verifies native cycles, copy independence,
unsafe graph rejection, weak-upgrade/retire races, stale generation, frozen
owner move replacement, pinned address stability and double-release safety.

The audited native host API now rejects *nested* pointer-bearing read
snapshots (not just raw pointers), constructs pinned pointees in place rather
than moving a previously constructed potentially self-referential object,
and requires noexcept destruction for explicit drop. Host callback bodies
remain trusted C++ code; this API does not sandbox arbitrary pointer capture.

The tests/ownership_stress_v1.cpp binary adds deterministic cyclic graph
cases at exact node/edge/byte budget boundaries, independent clone mutation,
32 rounds of 4-thread weak upgrade/retire races, and compile-time rejection
checks for unsafe snapshots and movable self-referential pin inputs.

Exact locally executed Git blob identities:
- include/atto/ownership_v1.hpp: 0050651637e265a6e660af77f302e6a9f955e3e8
- tests/ownership_v1.cpp: 5b77e09cec211164276cf46c89a1382c7f33465c
- tests/ownership_stress_v1.cpp: c8bae893835b05f211274e0cb2ecfabb74b77a45

All four native combinations passed locally: GCC and Clang, each running
both executables under -std=c++20 -O1 -g -Wall -Wextra -Werror -pedantic
-fsanitize=address,undefined -fno-omit-frame-pointer -pthread;
ASAN_OPTIONS=detect_leaks=1. Tests are assertions compiled without NDEBUG,
including Release CMake builds. The GitHub Actions runner failure is separate
and must not be reported as passing CI.

This establishes only **native host-model behavior**, not production
Forge IR lowering, actor memory isolation or AttoLang guest execution. This establishes only **native host-model behavior**.
It does not establish Forge guest IR lowering, actor memory isolation or
AttoLang syntax execution. Related frontend/spec/conformance branches
are in atto-source.rs#344, atto-borrow-checker.rs#43, atto-spec#92,
and atto-platform-conformance#76.
