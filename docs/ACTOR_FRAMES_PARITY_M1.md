# Forge actor-owned frame arena — M1 host/runtime slice

Stacked on Forge [#101](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/101) so the actor-owned **heap** and its turn contract are reused, not duplicated.

- **Storage:** one fixed, preallocated, independently zeroizable byte-frame arena per `ScalarActor` incarnation; provisional 64 frames / 64 KiB. Frames are logical owned data, **not C++ carrier call stacks**, operating-system guard pages, fibers, or a physical actor stack.
- **Authority:** private arena domains, turn epochs, logical-execution identity, physical carrier thread, sealed frame/suspension tokens, LIFO discipline, bounds checks, nonreentrant current dispatch. No guest pointer API is exposed.
- **Lifecycle:** clean synchronous callbacks must pop all their frames. Suspended frames retain their charge across carrier migration in the standalone host arena. A regular scalar callback cannot suspend yet; a callback attempting to return with live frames fails closed. After callback exceptions or swallowed quota errors the actor's queue is sealed and frame memory zeroed.
- **Fault isolation:** frame exhaustion raises a typed terminal exception without consuming more frame slots, a sibling actor can continue. Stale frames/continuations are denied after cancel/retire. Heap retirement remains quiescence-fenced by the existing scalar actor lifecycle.
- **Evidence:** GCC/Clang local host-model tests and the linked scalar callback bridge are not compiler-backed `.atto` guest proof. The standalone host heap in parent #101 is not present in Forge main. Physical actor stack isolation, source actor lowering, scheduler continuations, `suspends`, supervision, hot upgrade, native guards, and five-backend exact-head parity remain **unproven**.

## Release gate

Run CMake/CTest with GNU and Clang, ASan+UBSan, negative fault injection, exact-head/tree receipts. Count `steps=[]`, `steps=null`, or `runner_id=0` as runner-admission failures, not tests passed. Require compiled `.atto` actor A/B proofs with deep recursion and sibling survival before claiming per-actor **physical** stack isolation.

## Migration plan

1. Reconcile #101 with current Forge main, preserving mailbox/tenant/CI and compiler behavior. Retain this PR stacked until that merge.
2. Lower source `define actor`/spawn/handlers/state into the checked runtime ABI and explicit frame push/pop/unwind.
3. Implement stackless continuation lowering and ownership-aware suspension/resume, replacing any unbounded host recursion with generated state machines.
4. Add per-actor guarded physical native stacks/fibers (or prove no guest recursive use of the carrier stack); test SIGSEGV/guard behavior in subprocesses and ensure actor B survives.
5. Differential test Java/LLVM/Forge with common `.atto` fixtures, receipts, generation fencing, STOP/cancel and memory quotas; gate merge on independent review.
