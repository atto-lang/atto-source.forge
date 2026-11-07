# AttoLang Forge — current-main actor heap + frame integration

**Classification:** native host runtime implementation; **not** compiler-backed `.atto` actor execution; **not** physical/guarded actor stacks. The new integration branch starts from Forge `main` commit `7902eb2e3610babe1004a3e752b12079737992e6` and preserves its existing mailbox, compiler, tenant-isolate, source-admission, and CI paths.

## Exact source provenance

- Same-repository native heap and scratch arena C blobs are taken unmodified from draft Forge #101, including `ActorOwnedI64Heap.c` blob `008be708d79559c8afd7b7271f9385920f7a3aca`.
- The host-only logical frame arena and `ScalarActor::run_one` integration come from draft Forge #208.
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
| Conformance | Four CTests for actor arena, actor heap turns, scalar heap safety, frames + sanitizer lane | Exact-head hosted steps and compiled guest traces required |

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

The separate #208 host-frame implementation was compiled on local GNU and Clang with warnings-as-errors against a *minimal heap ABI stub*, emitting `ATTO_FORGE_ACTOR_FRAME_HOST_EXECUTED|cases=20|guest=NOT_EXECUTED`. **The current-main composition has not yet produced a linked actual native heap test receipt**. The added three-lane workflow builds and executes against the real `ActorOwnedI64Heap.c`; wait for admitted jobs and nonempty step records before assigning a verified status.
