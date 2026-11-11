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
