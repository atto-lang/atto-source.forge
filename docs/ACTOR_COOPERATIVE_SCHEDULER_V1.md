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
- STOP immediately closes ingress and clears queued mailbox bytes, then
  cooperatively unwinds the suspended callback before stack/heap retirement.
  This differs from PR #101's drain-on-stop scalar actor; production parity
  must select and verify one canonical STOP protocol **before** integrating
  these two runtimes. This bounded trusted-host profile requires strictly
  increasing positive numeric actor IDs within a scheduler lifetime. IDs
  are never reused; a constant-size high-water mark replaces the previous
  unbounded retired-ID set. Out-of-order IDs fail closed, including after
  UINT64_MAX; failed admission does not consume the next ID. This is a
  deliberate API restriction, not generation-based logical identity reuse.
  The live registry remains an ordered map with O(log n) lookup. Supervisor
  settlement and compiler-integrated slot reuse remain separate work.
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
   binaries passed. These are **standalone local native tests**, not
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
