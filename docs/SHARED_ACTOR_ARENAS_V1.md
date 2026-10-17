# Forge actor arena prototype (host-only)

`runtime/actor_arena.hpp` is a fixed-capacity actor-owned byte arena for SHARED/PRIVATE actor incarnations. It uses stack/inline fixed persistent slots and turn-local scratch, domain and generation checked handles, a bounded copy-only API, one physical carrier per admitted turn, and explicit zeroizing retirement. A later turn may migrate to a different carrier. Its hot path performs no heap allocations, but this **does not** certify the full compiler/runtime as allocation-free.

**Not yet wired:** `runtime/actor.cpp` currently only provides a bounded mailbox, not source actor allocation lowering. This header is host-only and has no shared-domain region, callback lease or deterministic nontrivial value destructors. Do not expose the native handles as guest actor refs or cross-thread data access. End-of-turn must be wired into every exceptional/suspension/cancellation exit before source semantics can use scratch. See `oreslang-common-source/docs/SHARED_ACTOR_ARENAS_V1.md` and matching conformance vectors.

## Explicit CONTROL retirement (native host substrate)

The owning CONTROL plane must call `retire()` after the last actor turn has
ended and before releasing the native arena object. An active turn blocks
retirement. `retire()` is idempotent after success, and the destructor fails
closed (`std::terminate`) if the arena is still active, the mutex cannot be
acquired immediately, **or explicit retirement has not completed**. The
destructor is not a substitute for a guaranteed CONTROL retirement path.
Every exceptional, cancellation and shutdown exit must quiesce the carrier,
retire the arena, and only then destroy it. The Linux/macOS CTest regression
exercises expected destructor termination in subprocesses to avoid aborting
the test runner.

Domain IDs use saturating atomic issuance and are never recycled on
exhaustion. These are **host-only** invariants; source `.ores` allocation
lowering and guest actor lifetime parity remain unsupported.
