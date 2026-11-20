# Restricted scalar WebAssembly cage experiment (v1)

**Status: EXPERIMENTAL / PARTIAL P0 / NOT A GENERAL HOSTILE-Atto SANDBOX.**

This change adds `include/atto/scalar_wasm_v1.hpp`. It reuses the checked Forge scalar frontend (`parse_source`, `validate_ast`, `evaluate`) to emit *actual WebAssembly binary* for the existing one-function, constant-only scalar `.atto` subset. The compiler refuses invalid source, overflowing checked integer arithmetic, division by zero, and every unsupported syntax/effect. It emits no import section, no shared memory and no arbitrary host calls. It executes the expression within the Wasm engine (not by returning the precomputed result). This is an independent proof-oriented experiment, not the general native Forge compiler or standard Atto runtime.

The generated diagnostic module has a *single 64 KiB private WebAssembly linear memory*, a `main() -> i64` export and **test-only** `peek(offset)` and `poke(offset,value)` functions. The probes are deliberately included to exercise real engine bounds checks. They are **not an Atto stdlib or permitted production guest interface**, and must be removed or gated behind a test-only build before shipping a production emitter.

## Test pipeline

Run `cmake -S . -B build -DATTO_FORGE_SANITIZERS=ON -DATTO_FORGE_WASM_CAGE_PROBE=ON && cmake --build build && ctest --test-dir build --output-on-failure -R '(scalar_wasm_fixture_v1|tenant_wasm_v1_engine)'` with GCC or Clang, Node.js and CMake installed.

The C++ fixture compiles seven real restricted `.atto` source strings into Wasm modules using the repository parser. A separate Node WebAssembly engine validates them with `WebAssembly.validate`, rejects imports, executes `main`, starts A and B in **one measured PID** as separate instances, asserts private memory independence and non-shared, fixed-size memory, triggers real engine out-of-bounds traps, checks module validation failures and tests fresh-instance memory initialization. Both fixture-generation and execution tests are required; CTest fixtures preserve ordering under parallel jobs.

This is **not** an independently attested exact-head CI receipt; local testing with a temporary parser shim is an encoder/engine smoke test, not a full repo build. Keep the PR in Draft until the *real checked parser*, sanitizer builds and engine tests execute at this exact commit.

## Explicitly unsupported and denied

- Arbitrary guest code, actor scheduling, continuation frames, host imports/FFI, sockets, filesystem, native pointer or machine-code confinement, true per-tenant fuel/interrupt and supervisor lifecycle.
- Malicious `.atto` memory-load/store syntax (not supported by the stage-0 scalar parser). Current OOB exercises diagnostic Wasm code, not a hostile Atto source fixture.
- Sealed artifact signature/provenance, guest ABI linking, actual tenant identity/generation, per-tenant socket capabilities, network ACL and resource accounting.
- Production same-process **untrusted** admission: `TenantDispatchGate::open(untrusted_in_process)` MUST continue returning `unsupported`. The separate trusted-host gate PR code is not coupled to this experiment and must not be silently promoted.

Wasm validation and per-instance memory bounds give real memory isolation to the **validated Wasm modules** under the chosen engine; they do not protect against engine/compiler vulnerabilities, host context misuse or side channels. This experiment is not a substitute for a reviewed same-PID TenantVM integration.

## Next integrated security milestone

1. Seal and verify source/compiled artifacts; define an immutable versioned manifest and an import allowlist that is empty by default.
2. Replace the test-only probe exports with validated guest instruction support and an explicit host gateway, enforcing per-tenant generation, resource and socket capabilities.
3. Integrate a supported, independently reviewed Wasm runtime into Forge, with tenant-specific instance memories/tables/stacks, interruptible fuel metering, watchdog and quiescent cancellation. Bind admission to real sandbox state rather than a caller flag.
4. Run two separately compiled **hostile** Atto programs with actual memory and I/O requests in a single PID. Add cross-tenant forgery, FD/handle reuse, runaway compute, lifecycle and neighbor-survival tests. Only those executed receipts can close Forge issue #31.

## Overlap audit

Complements Forge [#37](https://github.com/atto-lang/atto-source.forge/pull/37) trusted-host queue authority and [#32](https://github.com/atto-lang/atto-source.forge/pull/32) SocketBroker. Neither one executes guest code or establishes memory confinement. No SocketBroker code or native sandbox claim is copied here. Normative contract [atto-spec #49](https://github.com/atto-lang/atto-spec/pull/49), conformance [#43](https://github.com/atto-lang/atto-platform-conformance/pull/43), parent [Forge issue #31](https://github.com/atto-lang/atto-source.forge/issues/31). Historical Forge TenantVM #296 is not independently proven hostile native SFI and should not be equated with this Wasm reference.
