# Forge actor continuation / cooperative scheduler v1 (host only)

**NOT CERTIFIED AS LANGUAGE EXECUTION.** This proposal layers a bounded
single-carrier native callback scheduler on PR #206's guarded stacks and
Forge `main`'s existing authenticated `runtime/actor.hpp` byte mailboxes.
It does **not** lower `define actor`, implement source guest `await`/`select`,
actor group join/leave, ownership borrowing, native tenant process isolation,
self-host a compiler, or implement a multi-worker preemptive scheduler.

## Implementation

- Linux x86-64 only: one mapped guarded physical stack per actor. A suspended
  `ActorStackDomain` retains its full native continuation with stack locals;
  the one-shot `run_turn()` API remains available and prohibits `yield()`.
- A continuation starts with `start_task`, yields within its actor stack, and
  resumes with `resume_task` **on the original owning thread**. Nesting a
  different actor inside an active actor turn is rejected. Unsupported
  migration, unscheduled execution, reuse after STOP and direct cross-actor
  heap-handle access fail closed.
- Cancelling a *suspended* continuation sets a flag; its next `yield()` return
  throws `Cancelled` inside the actor, allowing C++ scope unwinding. STOP only
  physically scrubs/unmaps a finished task. Catch-all callbacks can suppress
  cancellation; callers must require terminal quiescence before destructor.
- `ActorCooperativeScheduler` admits bounded actor registrations, invokes one
  mailbox message per ready turn, and preserves FIFO per actor. It reuses
  Forge's opaque local ActorRef authority; the `send` path authenticates with
  `Mailbox::send` before allocating a wakeup. Wakeup-ring storage is allocated
  once at scheduler creation. It does **not** eliminate allocation by
  mailbox send itself, and it is **not** a hard real-time allocator.
- The lifecycle now has two explicit transitions. `close_actor(id)` seals
  ingress but drains all accepted DATA in FIFO order before retirement;
  `stop_actor(id)` is immediate STOP and zeroizes/abandons pending DATA while
  cancelling a suspended continuation. STOP wins over graceful close. These
  separate entrypoints give PR #101's drain-on-stop behavior an explicit
  graceful counterpart without silently changing urgent STOP behavior.
  Cross-PR wiring and source-actor semantics remain a separate integration gate.
  Actor ids are not reused in a scheduler lifetime.
- `ActorStackDomain` charges its mapped stack (including guard pages) and
  bounded heap against `ActorMemoryAggregateBudgetV1::process()` on current
  `main`. Failed constructors, normal returns, STOP and destruction refund
  exactly once; the separate `ActorIncarnationMemoryV1` logical frame model is
  not confused with a physical stack. This is not a per-tenant isolation budget.
- A retired callback purges stale queued wakeups, including self-close during
  a handler, so a stale entry cannot occupy a bounded ready-ring slot.
- The current domain heap is a bounded, non-recycling byte bump allocator.
  PR #101 (actor-owned scalar heap) and #117 (bounded arena / STOP fencing)
  remain separate draft integration candidates. This work must not silently
  replace their generations, loans, or destructor fences.

## Local executed gate

1. The original `forge_actor_stack_domain_host_v1` CTest, including
   a forked guard-page overflow negative on unsanitized GCC/Clang.
2. The new `forge_cooperative_actor_scheduler_host_v1` CTest includes two
   actors on one carrier, resumed stack-local frame state, private heap and
   cross-actor handle denial, FIFO/one-message scheduling, nested scheduling
   denial, wrong carrier, stale/non-recycled actor ids, cancellation unwind,
   mailbox bounds, stop-before-first-turn and guest callback fault cleanup.
3. GCC/Clang C++20 O0/O2 native builds and GCC/Clang O1 ASan+UBSan local
   binaries passed, including graceful drain, STOP-after-drain abort,
   self-close wakeup cleanup, and aggregate quota exhaustion/refunds. A
   scoped 2-test CMake/CTest harness passed. These are **standalone local
   native tests** using the same public aggregate-ledger ABI, not
   exact-PR-head full-repository CI or source-compiled guest actor tests.

For source tree compilation on a Linux x86-64 carrier:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build -R 'forge_(actor_stack_domain|cooperative_actor_scheduler)_host_v1' --output-on-failure
```

### ASan caveat and test policy

AddressSanitizer warns that `makecontext`/`swapcontext` are not fully
supported. ASan can substitute fake stack addresses, so physical stack
address-range assertions are compiled only in **non-ASan** native builds.
The sanitizer profile still runs authority, lifetime, callback error and
heap bounds tests. No source guest behavior, physical isolation from hostile
native pointers, or fiber correctness is certified by a sanitizer pass.

## Blocking follow-ups (not implemented here)

- Reconcile arena/heap authority and STOP semantics of #101/#117/#206.
- Replace `ucontext` with an audited arch-specific continuation mechanism
  or proven supported fiber ABI before claims of portability/production use.
- Integrate the source compiler `define actor`/`spawn`/`receive` lowering,
  source actor state and ownership verification, and executable differential
  tests against Java/LLVM (identical fixture and actual native output).
- Add wakeups from external worker carriers without an unsafe shared
  scheduler queue; prove quiescence, shutdown, generation fences and quotas.
- Acquire full exact-head source-owned CI job steps and independent review;
  any private external carrier needs separately approved source export policy.

## Current-main integration and certification

The integration branch starts from Forge `main` `336d5b9f63e01681375c5e02a9dc0d66ae47d9b8` rather than reusing the stale #215 base. It preserves the existing `ActorIncarnationMemoryV1` files, generated-frame and actor arena tests, and all unrelated `CMakeLists.txt` registrations; only the cooperative scheduler test target is appended.

The native local fixture uses a test copy of the public aggregate-ledger interface. **Full current-main CMake/CTest and exact GitHub CI are not certified.** Job steps have been reported absent on Forge source-org runs. A workflow with zero steps is NOT_EXECUTED, never PASS. No private source is mirrored without approved carrier authorization.
