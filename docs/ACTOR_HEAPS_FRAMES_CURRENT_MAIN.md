# AttoLang Forge — current-main actor heap + frame integration

**Classification:** native host runtime implementation; **not** compiler-backed `.atto` actor execution; **not** physical/guarded actor stacks. The new integration branch starts from Forge `main` commit `7902eb2e3610babe1004a3e752b12079737992e6` and preserves its existing mailbox, compiler, tenant-isolate, source-admission, and CI paths.

## Exact source provenance

- Same-repository native heap and scratch arena C blobs are taken unmodified from draft Forge #101, including `ActorOwnedI64Heap.c` blob `008be708d79559c8afd7b7271f9385920f7a3aca`.
- The host-only logical frame storage and `ScalarActor::run_one` bridge come from pre-existing Forge #207; exact Git blobs are reused rather than introducing competing implementations.
- No Java or LLVM private source is mirrored into this branch. Portable semantics must be compared using reviewed shared fixture contracts and independent backends.

## Implemented runtime properties

| Feature | Forge in this integration | Claim limit |
| --- | --- | --- |
| Actor-owned heap | Dedicated bounded i64 heap and scalar field ABI per actor incarnation, preallocated FIFO, exclusive turn | Host callbacks only |
| Actor-owned stack | Dedicated bounded byte-frame arena, 64 frames / 64 KiB provisional default | Logical frame data only, not guarded CPU stack |
| Ownership | Per-arena identity and per-turn/physical-carrier/execution epoch; stale-frame denial | No source compiler allocation/borrow lowering |
| Suspension | Host arena retains bytes across suspend; resumes on another carrier | ScalarActor callbacks still synchronous; scheduler continuation bridge absent |
| Cancellation / STOP | Quiesced frame cancellation and retirement zeroize bytes; actor callback failure seals mailbox | No supervisor/compiler STOP lowering |
| Fault isolation | Host frame overflow poisons its actor and sibling can continue | Does not prove native stack-overflow signal containment |
| Conformance | Six native host CTests for arena, heap turns, heap safety, frames, callback bridge, plus a source-compiler negative gate | Exact-head hosted steps and compiled guest traces required |

## Parity gates against Java and LLVM

Java PR #2627 has a similar provisional actor-owned logical frame store and explicitly does **not** replace the JVM carrier call stack. LLVM PR #1350 includes a restricted compiler-generated scalar recursive stack budget, distinct from source-level actor stack isolation. LLVM actor-owned heap / sealed-state branches require independent compiled guest validation. Compare semantics rather than treating each repo's host substrate as parity.

Must-have source/compiler acceptance before claiming a production actor stack:

1. Canonical `.atto` `define actor`, spawn, state and handler compiler lowering, not host-only function callbacks.
2. Every guest call, function/method recursion, capture, exceptional edge, defer/cleanup and `suspends` continuation charges the actor's frame budget. No uncontrolled recursion on the OS carrier stack.
3. Actor A exceeds its bounded frame quota, terminates safely, releases frames after STOP; actor B runs **in the same OS process** and preserves its heap. Include stale frame access after actor restart/generation change, wrong task/carrier, and cross-actor authority denial.
4. Proper TCO where the effect/cleanup/ownership invariants permit, explicit rejection of `routine` recursion, and source-level cancellation and actor-turn nonreentrancy.
5. Guarded actor-native stacks or a proven stackless lowering strategy; compiled fault-injection mutation and supervisor restart assertions on GNU/Clang, Java/GraalVM, .NET, Wasm, and Cranelift/Forge.
6. Exact Git head, tree and fixture blob receipts; nonzero executed CI steps and independently reviewed conformance. Jobs with `steps=[]`/`null` or `runner_id=0` are runner admission failures, not passing tests.

## Local / hosted evidence

Existing Forge #207 records local GCC, Clang and ASan+UBSan execution of its standalone frame program at exact source blob identities. Its complete callback bridge and current-main CMake composition have **not** yet produced an exact-head linked native-heap test receipt. The expanded three-lane workflow builds and executes against the real `ActorOwnedI64Heap.c`; wait for admitted jobs and nonempty step records before assigning a verified status.

## Additional compiler admission hardening

`verification/actor_frames_source_admission.py` checks 80 negative invocations (ten unsupported `.atto` actor forms across eight compiler CLI paths) and four positive scalar controls (including nested comments). Negative cases require exit 1, an error diagnostic and no published stdout, IR or assembly. These new checks are source-authored **and have not yet executed on hosted exact-head CI**. They deny false source actor parity until real compiler lowering exists.

## Iterative actor call lowering (stackless runtime foundation)

- `runtime/actor_stackless.hpp` contains a small bounded i64 instruction executor. Guest register and PC state is persisted in the *actor-owned frame arena*, using a fixed-size carrier-local array of opaque frame handles rather than recursive C++ calls. Every call is charged before execution; depth, instruction budget, bounds, checked arithmetic, malformed opcode/target, and unwinding are explicit. This is an internal lowering target, **not** a source-level AttoLang actor compiler.
- `verification/actor_stackless.cpp` tests 64-frame depth, actor-private overflow/poison, same-process sibling survival, thread migration, invalid bytecode, loop budget, integer underflow and full frame unwind; CMake/CI run this as `oresforge_actor_stackless_host`.
- **Evidence status:** hosted workflows are blocked at runner admission (zero executed steps), and the exact-head new test has not been executed. No `.atto` source actor is compiled or run; there is no general guest scheduler/continuation, physical machine stack switch or guest parity proof. The next compiler milestone is to emit validated instructions from typed `.atto` actor AST/HIR and execute actor A overflow alongside actor B in the same process.

## Additional actor VM verifier and negative hardening

- The iterative VM now explicitly carries a per-frame `child_ready` bit. `IncrementChild` and `ReturnChild` **poison the actor** if no child has actually returned; a new `Call` clears the previous result's readiness. This closes use-before-definition of actor-private return slots.
- Admission rejects unreachable invalid bytecode and noncanonical unused operands (including hidden branch literals). These are rejected before any frame is allocated.
- Native tests add repeated 64-depth reuse, swallowed quota errors, byte-cap exhaustion with free depth slots, malformed control-flow, null child reads, runaway budget and post-overflow sibling viability.
- Stage-0 actor source admission has ten feature classes spanning virtual actors, unreachable declarations, state, supervision, pub-sub and critical/suspends. All eight CLI emission/execute modes must deny without publishing code; scalar-only sources with actor-shaped comments must remain accepted.
- **No new exact-head executed result has yet been recorded.** The GitHub Actions runner-admission failure is separate from these authored tests.

## Concurrent actor stress and exact-source receipts

The actor VM host suite now executes two independent actors simultaneously on different carriers, each processing 64 iterative calls (128 total), while checking actor-specific heap field results, mailbox draining, separate frame retirement, and carrier-local callback context. The suite also denies a child-return increment at the i64 maximum and validates previously detected uninitialized-return and byte-budget errors.

The exact-head native test workflow pins and records the source blobs for the actor frame, stackless VM, heap C implementation, CMake composition, native test, and compiler-admission test. A second native negative-controls receipt marker must be produced by an actually executed test. Neither receipts nor the test labels imply compiled `.atto` actor parity; do not count workflow registration as an executed run.

## Reconciliation with the evolving Forge mainline (2026-11-07)

Forge main advanced 67 commits from this integration's original base, including `runtime/ActorIncarnationMemoryV1.hpp` (mailbox-incarnation-fenced, process-budgeted native heap and logical frames), `runtime/ActorStackDomain.hpp` (guarded Linux x86-64 `ucontext` host callback stack), and `runtime/actor_arena.hpp` (C++ scoped arena). These **already have their own host tests** and are preserved, not replaced by the new C scalar/stackless path.

The C arena test target was renamed to `oresforge_actor_c_arena_tests` to avoid colliding with the mainline `oresforge_actor_arena_tests`; the CTest names stay distinct. The exact-head workflow additionally builds/tests the mainline C++ actor arena and mailbox-incarnation-memory target, and executes the guarded-stack host test only on GCC/Clang non-sanitizer lanes.

**Architecture gap:** `ScalarActor` and `ActorStacklessI64` still use their own host-private frame and scalar heap domain rather than `ActorIncarnationMemoryV1`'s process/tenant budget ledger and mailbox-generation lock. Do not describe these as a unified memory/runtime ABI or substitute the `ucontext` stack (which cannot migrate) for stackless suspension. A typed compiler-produced actor adapter with one authoritative identity, budget and STOP/continuation fence is required before source guest parity.

## Restricted compiler-backed actor handler profile (Nov 7, 2026)

Forge now has an explicitly opt-in scalar actor source compiler: actor-ir reads a .atto actor and emits a checked descriptive handler IR; run-actor constructs a private ScalarActor and sends supplied i64 messages through native actor turn admission. The supported grammar is deliberately limited to a single mutable i64 state field with a checked-add on_message handler, or a stateless on_message forwarding to the exact recursive countdown shape. Unsupported actor forms still fail closed in default run/ir/native-asm modes.

The generated countdown uses the actor-owned stackless VM frame/PC/argument state, with 64-frame quotas and checked overflow. Actor identifiers, state names, parameter names and method symbols come from validated source, not a fixture filename. Host message-injection is not source-language spawn, general user-defined actor functions, suspension, native AOT machine code, or a unified mailbox-authorized ActorExecutionContext.

Added .atto fixtures actor_counter.atto and actor_countdown.atto; the native CTest checks two independent compiled source actor instances, carrier migration between turns, concurrent sibling execution, 65th-frame overflow, same-process healthy sibling survival, STOP and stale-identity restart, and checked i64 overflow. The separate CLI test checks four positive invocations and 46 negative invocations, including strict legacy mode denial, malformed source, incorrect operators, invalid messages and no partial stdout after a fault. The host bridge rejects overlapping synchronous receives before enqueue.

All new source files, fixture blobs and both test markers are pinned in the exact-head GCC/Clang/ASan workflow. They are authored, not observed passing, until runner admission produces nonzero executed steps and actual receipts. Follow-up is a full source-level spawn/handler ABI and one mailbox-incarnation/process-budget authority.

## Source-level spawning and reply profile — draft milestone

The restricted compiler now additionally accepts a **source-level** `pub fnc main(): int` containing 2–8 checked `const name = spawn ActorName();` bindings, up to 256 synchronous `name.on_message(signed_i64);` calls, and a checked-add result combining two final `name.on_message(signed_i64)` replies. The parser binds each call to a declared actor instance; duplicate bindings, unknown actor types/targets, numeric overflow, unsanctioned expressions, and extra declarations reject before any source execution.

`execute_main` creates one independently authoritative `ActorExecutionContext` per source `spawn`, supplies native private actor heap and bounded logical frame arena, and executes send/handler/reply through the existing native mailbox turn bridge. The command `oresforge run-actor-main <file.atto>` executes that restricted profile, buffering output until all turns succeed. This is a genuine compiler-checked source spawn/send/reply **synchronous bootstrap**, not a general asynchronous actor send, actor future, suspension, `select`, supervisor, actor group, JIT, or native AOT actor code generation.

The source fixtures `verification/fixtures/actor_main_two_instances.atto` and `actor_main_recursion.atto` assert outputs 42 and 5. The dedicated CLI suite now adds 19 source-main mutation cases, including a compile-valid 65th-frame source fault, and 47 new fail-closed invocations; it retains the previous 46 CLI negatives and 80 default actor denial cases separately. The existing mainline mailbox-incarnation budget and `ActorIncarnationMemoryV1` remain **unintegrated** with the scalar compiler bridge: cross-path generation, aggregate quotas and STOP semantics must be unified before production parity.

**Evidence:** source and CTest definitions have been pushed to the draft branch; claims of native compilation, passing compiled actors, isolation at runtime, or optimizer/sanitizer conformance are contingent on actually executed exact-head CI jobs. Zero-step runner failures are NOT_EXECUTED.

## Shared process-budget admission for source-spawned actors

Each source-compiled `ActorExecutionContext` now reserves 128 KiB from the same `ActorMemoryAggregateBudgetV1::process()` ledger used by mainline actor-incarnation storage **before** constructing its private `ScalarActor` heap/frame arena. The reservation is released by RAII only after the owned actor destructor runs; an exception during construction also refunds it. This is a conservative process-budget reservation for the restricted 1-field/8-message-queue/64-frame scalar profile, **not** measured per-allocation accounting or a unified `ActorIncarnationMemoryV1` object. No tenant-level ledger or mailbox-generation authority is shared yet.

The expanded native regression checks a one-actor ledger increment/decrement, failed-constructor rollback, exact eight-spawn/256-send acceptance, ninth actor and 257th send denial, independent actor heap values, and successful source-produced reply sums. These CTest cases remain authored/unverified until real compiler and runtime steps execute on the exact pull-request head.
