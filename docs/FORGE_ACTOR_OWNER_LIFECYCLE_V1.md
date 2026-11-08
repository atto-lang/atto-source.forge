# Forge trusted actor-incarnation owner — host lifecycle v1

Status: SOURCE-AUTHORED / NOT_EXECUTED until real exact-head GCC/Clang CTests
run with nonzero executed CI steps. This is a host adapter, NOT `.atto`
actor lowering, Cranelift guest execution, native process isolation, or
complete cooperative scheduling.

## Invariants

`ActorIncarnationOwnerV1` owns, in one scoped supervisor-managed object,
an authenticated `Mailbox`, an `ActorArena` physical-turn admission gate,
and an `ActorIncarnationMemoryV1` private heap and logical frame slab.
All three identities use one exact incarnation. No separately retained raw
actor memory object or native pointer is exported from the host API.

- `send` deep-copies the bounded mailbox payload. `dispatch_one` admits
  a maximum of one queued message and one turn at a time.
- Turn memory methods are checked under the owner's lifecycle mutex,
  against original carrier thread, physical turn epoch/domain, current
  mailbox issued capability, actor ID and generation. A later message,
  foreign thread or restarted actor cannot borrow an earlier turn's memory.
- Frame APIs are synchronous begin/push/read/write/pop/finish. The host
  adapter deliberately exposes no `suspend` or `resume` until scheduler
  continuation and nonreentrancy ownership are integrated.
- After the trusted handler returns, the owner verifies exact frame-stack
  completion. Even a caught frame quota error permanently closes that actor
  on handler exit; sibling actor runtimes stay independently usable.
- STOP closes the mailbox before reclaiming memory, revoking all new sends
  and native turn memory operations. If a handler is active, STOP marks it
  pending and **defers native zero/reclaim and aggregate quota refund** until
  the physical carrier has returned and ended its turn. STOP does not block
  on arbitrary guest code while holding the owner mutex.
- Restart requires quiescence, retires prior memory, increments the mailbox
  generation and constructs a new `ActorArena` and private heap/frame pair.
  If construction fails, the successor mailbox remains closed; no partially
  admitted actor exists.
- Failed handler dispatch zeroes the dequeued mailbox payload, closes this
  incarnation, retires its own storage and rethrows the host exception.
  Another actor's scheduler or heap is not torn down by this owner.

## Source regression

`verification/actor_incarnation_owner_v1_test.cpp` verifies independent
actor-native memory reservations, payload deep-copy, cross-carrier and
reentrant-turn rejection, actor A STOP racing a handler, deferred refund,
generation rejection after restart, zeroed successor heap, swallowed
terminal frame quota fault and actor B replying with 42 afterwards.
`CMakeLists.txt` registers an independent CTest. The source-local workflow
requests exact immutable head readback and a 2×2 GCC/Clang optimized and
ASan/UBSan matrix.

## Remaining gaps / release holds

The physical `ActorArena` is a trusted fixed-sized host gate, not a fully
charged actor heap or an asynchronous scheduler. The new owner does not
atomically install compiler-generated typed frame layouts, enforce deadlines
for runaway native callbacks, execute guest `.atto` handlers, wire
`ActorStackDomain` guard-paged native stacks, provide FFI lifetime pins,
or prove native stack fault containment. As with other host callbacks, the
supervisor must join the executing carrier before destroying this owner.
The scoped quota ledger must outlive all owners charged against it.

Do not call a source-only commit or `steps=null` failure a test pass.
Independent review, exact-head stepful host/sanitizer runs and compiled
actor A stack exhaustion / actor B survival fixtures across Java/LLVM/.NET/
Forge/Wasm remain required before promoting to merge-ready.

## Compatibility and combined-tree evidence

This source PR contains the **actor lifecycle owner**, its focused CTest,
workflow and documentation. It does not itself contain the separate
`ActorIncarnationMemoryV1` hardening diff from PR #226.

The combined candidate [Forge #230](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/230)
joins the byte-identical actor-memory implementation and regression suite
from #226 with this owner. Its workflow builds and runs both native CTests
against one source tree under GCC/Clang, optimized and ASan+UBSan modes.
Even there, actual execution remains **NOT_EXECUTED** while the source-org
GitHub Actions runner cannot admit jobs (`steps=null`).

Owner-specific safety guarantees carried into this source branch:
- Dequeued message failures at physical-turn admission close that
  incarnation instead of silently dropping one message.
- Native `ActorArena` quiescence is verified before native heap/frame
  scrub and quota refund.
- Failure to end a physical turn quarantines native storage and forbids
  new dispatch/restart rather than causing a possible use-after-free.
- Quota-denied supervisor restart remains closed and re-admits only with
  a new generation after reservation capacity returns.
- A throwing trusted handler closes only its own incarnation and, when
  quiescent, refunds its native reservation.

Neither this source PR nor #230 establishes compiled `.atto` guest actors,
native stack fault recovery or correct asynchronous continuation ownership.
