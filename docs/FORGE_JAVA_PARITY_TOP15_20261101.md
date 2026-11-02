# Forge → Java/Graal AttoLang parity: first 15 actionable gaps
Date: 2026-11-01. Baseline Forge main `a98a5e0abec222862278a52e7aaadbb2c674ec97` (tree `d1edaacf4ef7d613bcbdc257488399fa2823a0e4`).

**Evidence conventions.** A draft PR is not a merged feature. Compiler admission
(fail closed), host-runtime behavior, real compiled guest execution, and
cross-OS native AOT receipts are four distinct proof categories. An Actions
job with `steps=[]`/`null` is `NOT_EXECUTED`, never a test failure or pass.
Forge is an independent C++20 compiler, **not** the Cranelift toolchain.
This document ranks *missing executable semantics* rather than counting
diagnostics as feature parity.

| # | Priority gap relative to Java/Graal direction | Forge main baseline / existing work | Smallest next executable milestone |
|---|---|---|---|
| 1 | Immutable-by-default typed locals | Before this patch, only parameter references and a return expression; this PR adds bounded `int`, `const int`, and inferred-i64 `const name` SSA locals in scalar/runtime entry, not general typed locals. Java #2449 targets int/bool/string guest semantics. | Execute C++ parser + interpreter, native constant-entry and runtime-argument cases at exact head; then add `bool`/`string` only with real typed values. |
| 2 | Nominal classes, methods, receivers and dispatch | Scalar frontend rejects class; draft Forge #157 implements opt-in multi-instance scalar IR only. | Port single-class scalar method source fixture with real x86-64 native method calls; reject duplicate identity and stale receiver. |
| 3 | Source actors with private heaps and exclusive turns | Host actor mailbox on main; draft #101 builds bounded scalar host actors. | Compile one `.ores` actor spawn→send→receive→retire fixture to real native code and fence stale generations. |
| 4 | Actor groups, pub/sub and backpressure | No source `Topic<T>`, subscription or group member lowering; Java #2440 defines contract. | Two subscribers across independently joined groups, bounded FIFO and unsubscribe-vs-publish guest fixture. |
| 5 | `critical {}` / `suspends` separation | Draft Forge #168 reserves source tokens fail closed; no scheduler-aware critical gate. Java #2433 also reserves unsafe lowering. | Positive uncontended lock + FIFO queued waiter, reject await inside critical body, verify lease release on cancellation. |
| 6 | Future/await, structured concurrency and cancellation | No coroutine/continuation lowering in bootstrap. | Single owned `suspends` task with await, cancel and exactly-once outcome, no carrier thread blocked. |
| 7 | Borrow checking, actor-owned regions and isoactor reclamation | Host actor arenas and private-heap prototypes; provenance draft #148 is not source lowering. | Compile exclusive loan and double-borrow rejection; retirement fence and bounded memory reclamation. |
| 8 | Project-root imports, package-owned namespaces and init barriers | Main rejects all imports; draft #141 hardens denial. | Link two sources in same package namespace, reject cross-package alias spoof and qualified `init()`. |
| 9 | Option/Result, records, structs and tuple ownership | Native scalar i64 only; structural admission matrices exist as drafts. | Real tagged `Option<int>` representation with match exhaustiveness and no implicit nested flattening. |
| 10 | Hygienic AST macros and compile-time generics (AOT) | No macro/generic compiler lowering. | Deterministic hygienic expansion and two monomorphized instantiations in an AOT artifact; reject capture and recursive expansion. |
| 11 | Numeric types, checked error ABI, and pure shared Math stdlib | Checked i64 supported; draft #153/154 covers Math and numeric error slot, not floating/BigInt/Decimal guest parity. | Bit-identical Math source + overflow typed receipt, then binary64/decimal negative precision corpus. |
| 12 | Native Oreslang testing harness | C++/Python host tests exist, no portable `stdlib/testing.ores` guest execution in Forge. | Compile tests from the shared native `.ores` stdlib harness and emit machine-readable pass/fail records. |
| 13 | Async networking, QUIC/TCP, DNS, nonblocking ownership | Host transport and nonblocking fixes drafted #142/#161, no async guest socket API. | Actor-owned nonblocking loopback I/O with backpressure, cancellation and bounded DNS. |
| 14 | Annotations, injected state, refinements/Zorde and capability effects | Draft #133 fail-closed annotations; #170 reserves state-container heads. No actual general lowering. | Typed private scalar state injection with generated ownership checks; adversarial alias and Zorde runtime-bound checks. |
| 15 | Cross-platform executable AOT, optional JIT and self-hosting | Experimental native scalar asm x86-64/ARM64; native callable x86-64 only; `jit/` policy not executable tiering. | Linux/macOS/Windows binary receipts at same source contract, then optional JIT behind separately tested capability gate. |

## Order and shared acceptance
1. **This branch:** get #1 as real bounded positive Forge scalar semantics, without
   claiming bool/string or general lexical scope. Run `cmake -S . -B build`,
   `cmake --build build -j`, and `ctest --test-dir build --output-on-failure`
   on the **exact PR head**. Linux x86-64/ARM64 native CTests are separately
   required for generated machine-code claims; record compiler digest.
2. Reconcile #157 then #101 before attempting #3/#4; inspect overlapping
   backend branch heads and keep host substrates distinct from guest lowering.
3. Land shared contract-first negative admissions for #5/#7/#8 while implementing
   smallest genuine positive #9/#11 slices. Do not call diagnostic alignment parity.
4. Integrate #12 into the same compiler guest harness, then extend #13–#15
   with ASan/UBSan, concurrency races, native binaries and independent review.

Promotion requires **positive and negative exact-head compiled guest receipts**,
no zero-step substitutions, immutable fixture IDs/source digests, independent
review and protected-main branch policy. No private source export to outside
organizations is authorized by this matrix.
