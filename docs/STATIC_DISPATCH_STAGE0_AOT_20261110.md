# Forge stage-0 direct dispatch / strict-AOT boundary

Status: bounded real compiler/assembly gate, **not** language-wide dispatch parity.

## Implemented source subset

Forge's `compiler/call_native.cpp` parses up to 64 statically named
functions with at most four `int` parameters. Every `Kind::Call` binds to
one deterministic function index before publication. The compiler accepts
forward calls and recursion after verifying argument arity and target/name
identity. Native x86-64 lowering emits `call .Lores_fn_N`, never a method-name
registry or function pointer for this source subset.

The parser already rejects unknown/duplicate functions and wrong call arity.
The separate IR verifier must also reject **forged compiler-generated IR**.
This audit makes it reject noncanonical/invalid function identifiers and
duplicate or malformed parameter names, rather than relying only on the
source parser having run. The direct-call ABI is not changed.

## Executable evidence required

1. Pin the exact PR source head and Git tree; a zero-step Actions run is
   runner admission failure and provides **no** compiler execution evidence.
2. Compile and run `oresforge_static_dispatch_aot_source_and_ir` against the
   real compiler implementation and canonical
   `tests/fixtures/static-dispatch/direct-42.atto`.
3. Reject wrong callee index, wrong callee/name identity, forged function name,
   forged parameter, duplicate parameter, unresolved function, bad arity,
   and duplicate function.
4. Emit native x86-64 assembly from the **same parsed .atto source**; require
   direct labels for `inc` and `twice`; prohibit indirect call/jump in the
   bounded fixture.
5. Assemble/link actual executables at O0 and O2 and execute them, requiring
   `oresforge_entry() == 42`. Record binary SHA256, compiler version and head.
6. Do not count the Forge interpreter's 42 result alone as full native AOT.

`cmake -S . -B build -DBUILD_TESTING=ON` and
`cmake --build build --target oresforge_static_dispatch_aot_verifier` followed
by `ctest --test-dir build -R oresforge_static_dispatch_aot_source_and_ir`
run the source-level/IR component. The dedicated exact-head workflow also
links and runs the actual machine-code artifact.

## Out of scope / follow-up

- General classes, inheritance, virtual dispatch, traits/generic witnesses,
  hygienic macro-generated specializations and actors are **unsupported**
  in this stage-0 callable compiler. A successful stage-0 gate is **not**
  evidence that those features compile to Forge native code.
- A future unified compiler IR should map this stage-0 call to `DIRECT` in
  [the shared dispatch contract](https://github.com/ores-truffle-oreslang/oreslang-common-source/pull/265),
  preserving exact target identity and borrow/effect/capability admission.
- Full strict-AOT parity requires an independent `.atto` compiler-backed
  test for each supported feature, executable identity, negative tests,
  target architecture evidence and an independent review.
- Measure O0/O2 guest execution, compile time, executable size and inlining
  separately; never label host-only C++ microbenchmarks as AttoLang gains.

## Expanded source-local native guest acceptance (November 20, 2026)

The existing draft-stage and ready-stage work remains in its original PR, not a new competing implementation. Run both named CTests against **the actual current PR commit** with GCC and Clang on Linux x86-64:

```sh
cmake -S . -B build-forge-aot-gcc -DBUILD_TESTING=ON -DCMAKE_CXX_COMPILER=g++
cmake --build build-forge-aot-gcc --target oresforge_static_dispatch_aot_verifier -j2
ctest --test-dir build-forge-aot-gcc --output-on-failure -V -R '^oresforge_static_dispatch_aot_(source_and_ir|compiled_native_guests)$'

cmake -S . -B build-forge-aot-clang -DBUILD_TESTING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-forge-aot-clang --target oresforge_static_dispatch_aot_verifier -j2
ctest --test-dir build-forge-aot-clang --output-on-failure -V -R '^oresforge_static_dispatch_aot_(source_and_ir|compiled_native_guests)$'
```

The `verification/static_dispatch_native_guests.cmake` test calls the **actual**
`oresforge_static_dispatch_aot_verifier` twice per source for deterministic
assembly. For positive cases it also independently interprets the verified
source (`--check-42`), then assembles/links native code and executes it at
`-O0` and `-O2`. Negative cases require a genuine x86 `ud2` / `SIGILL`
exit (Bash status 132); an unexpected return or unrelated failure is rejected.

- Positive fixtures: `direct-42`, `direct-four-args`,
  `direct-nested-calls`, `direct-lazy-branch`,
  `direct-recursion-boundary`, and `direct-i64-boundary`.
- Negative traps: `trap-overflow` and `trap-depth-limit`.
- **Per C++ compiler:** 12 successful native guest executions plus
  four intentional native trap executions (16 total). With both compilers,
  32 actual guest executions are required for this narrow native AOT gate.
- IR hardening additionally rejects lexically valid but source-reserved
  function/parameter names and noncanonical numeric payloads injected into
  dispatch nodes. The source parser and external IR verifier share the
  same identifier rules.

**Evidence boundary:** The CMake harness itself was exercised in the
isolated development container against a **synthetic** small emitter with GCC
and Clang. That checks script parsing, native assembly/link, SIGILL handling,
and negative rejection of wrong results/indirect calls; it does **not** test
the real Forge source compiler or certify these eight real fixtures.
Source-org runner admission and exact-head executable GCC/Clang evidence
remain mandatory. No private source is approved for cross-organization export.

As before, this is stage-0 static, scalar, SysV x86-64 callable AOT—not
general nominal/actor dispatch, TenantVM guest isolation, portable ARM64
codegen, or Rust/Wasm backend equivalence.
