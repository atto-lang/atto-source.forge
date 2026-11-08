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
