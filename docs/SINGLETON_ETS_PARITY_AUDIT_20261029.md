# Singleton and ETS sharing audit — 2026-10-29

Status: proposed cross-backend semantics; singleton/ETS guest execution remains unverified.
The PR includes Forge declaration admission and a C++ host runtime slice; guest singleton/table execution remains unsupported. See SINGLETON_SHARED_RUNTIME_SLICE.md for implemented behavior and remaining integration gates.

## Audit finding

A singleton gives one instance in a defined scope. It does not by itself make mutable fields safe to share between actors.
ETS is a runtime-owned table facility with access controls, atomic operations and process-linked lifetime. An ordinary singleton object is not an ETS equivalent.

Pinned source revisions inspected:
| Backend | Source revision | Evidence inspected | Finding |
| --- | --- | --- | --- |
| java | `9f4f4a3b75dd4b124e8509b65a88a98213c11ab5` | Parser.java, Ast.java, shared arena contracts | No singleton declaration implementation found in inspected frontend evidence |
| llvm | `6ca6a6911a6eeedc51e6865f87e8b73a5d604a3e` | frontend/Parser.cpp, repository file inventory | No singleton declaration implementation found in inspected frontend evidence |
| net | `f84d61e4211e6accd2f205462d430259a1e697ba` | StructuredSource.cs, Syntax.cs | No singleton declaration implementation found in inspected frontend evidence |
| wasm | `4c38695f27bda6d36d4e1a3259b487d75e96ced6` | docs/REAL_COMPILER_GATE.md, repository file inventory | Real guest compilation depends on LLVM; singleton worker sharing not established |
| forge | `60ca345cf1b532a9c371de329721402e92be334f` | compiler/frontend.cpp, compiler/call_native.cpp | Restricted scalar/runtime/call profiles; explicit unsupported admission added in this PR |

This is a scoped source audit, not proof of absence from every branch or historical commit.
No backend is counted as passing singleton conformance. Shared-arena documentation and host tests do not prove compiled singleton behavior.
The five requested backends include Forge; the existing general parity manifest's JavaScript target does not substitute for Forge.

## Erlang baseline

Primary references:
- https://www.erlang.org/doc/apps/stdlib/ets.html
- https://www.erlang.org/doc/apps/erts/persistent_term.html

ETS tables are node-local. A creating process owns the table; owner death normally destroys it unless ownership is transferred (including an heir). Access can be private, protected or public. Object operations have documented atomicity guarantees; arbitrary sequences of operations do not become transactions. Insert/lookup currently copy objects. Ordinary concurrent traversal is not a consistent whole-table snapshot.

persistent_term fits rarely changed, frequently read configuration. Complex replacement/deletion can trigger global garbage collection. That update mechanism is unsuitable as an Oreslang hard-real-time design.

## Proposed singleton contract

Retain the requested `define singleton` declaration; avoid new sharing keywords.
This section is a proposal, not a claim that the syntax or API is implemented.

1. Identity is keyed by resolved declaration identity plus application sharing-domain incarnation. Imports, path aliases and duplicate module loading must not create extra instances. Separate applications remain separate; this is not a distributed singleton.
2. Exactly one initializer runs per incarnation. Publish only after successful complete initialization. Waiters see either the completed value or a stable failure; no partial receiver escapes.
3. Initialization failure is sticky for that incarnation. Explicit supervisor-controlled restart creates a new incarnation and invalidates old authority; incidental accesses never silently retry side effects.
4. Detect recursive and cross-singleton initialization cycles, including cross-task wait cycles. Diagnose instead of hanging. Cancellation of a waiter must not destroy a successfully publishing instance.
5. Immutable fields follow existing implicit-`val` rules. Field immutability alone is shallow: reachable mutable values cannot be shared as ordinary aliases.
6. Frozen, deeply immutable, shareable values may be read across actors in the same admitted domain. Mutable singleton state requires compiler-checked synchronized access. No direct mutable field escape or actor-private reference can be published.
7. A singleton holding an ActorRef conveys mailbox access, not direct actor state access. Existing sealed-state and borrow rules remain in force.
8. Initializers and locks must obey effects and permissions. A singleton must not amplify an actor's authority or bypass an untrusted-isolate boundary.
9. Hot loading preserves a compatible live incarnation or performs an explicit supervised transition; never publish two live instances under one identity.
10. Shutdown denies new admissions, drains active leases and destroys once. Initialization failure, shutdown, and replacement must have deterministic races and bounded bookkeeping.

## Proposed ETS-style table contract

Use an explicit typed shared-table library/runtime abstraction. A singleton can expose a table capability; it does not expose table backing memory.

| Topic | Required rule |
| --- | --- |
| Default access | Protected: owner writes; explicitly admitted actors read. Private is owner-only. Public writes require an explicit choice and independently granted write authority. |
| Authority | Runtime derives caller identity from its execution context. Guest-supplied actor IDs, table names, offsets or serialized handles cannot grant authority. |
| Capability transfer | Explicit capability delegation may attenuate read/write rights. Domain and permissions checks still apply. A name lookup does not mint authority. |
| Contents | Copy values by default; reject actor-private references. Zero-copy requires deeply frozen data in an admitted shared arena and a lifetime lease. |
| Atomicity | Define linearization points for lookup, insert, delete, insert-if-absent, compare-and-swap and checked update-counter. A read followed by write is not an atomic increment. |
| Callbacks | No arbitrary guest callbacks under internal locks. Callback-based update requires a separate specified retry/effect contract. |
| Borrowing | No mutable reference escapes an operation. Read borrows require a lease and cannot outlive it or cross await/select without proven pinning. |
| Lifetime | One explicit owner; owner death deletes or atomically transfers to an eligible heir. No transfer means retirement, not accidental immortality via a singleton reference. |
| Stale handles | Validate domain, table identity and generation each operation. Deleting and recreating a named table cannot revive old handles (ABA). |
| Reclamation | Stop admission first; defer free until readers, writers, callbacks and external pins quiesce. An expired owner cannot issue new operations. |
| Iteration | Bounded cursors; no implied snapshot during writes. Snapshot mode, if added, needs explicit consistency, pin and memory budgets. |
| Keys | Specify typed equality, hashing, numeric distinctions and symbol identity identically across backends. Begin with exact typed keys; do not silently inherit host equality. |
| Errors | Typed Error subclasses / existing Result discipline; distinguish permission denied, stale authority, capacity exhaustion, overflow and closed table. Failure leaves data unchanged. |
| Limits | Bound table count, bytes, entries, key size, collision work, operation work and pins. Over-budget operations fail without partial publication. |

A table reference must not cross machine/network boundaries as raw authority.
Same-domain actors can access an admitted native table directly. Separate JVM/Graal isolates and Wasm workers need a validated shared-memory implementation or broker protocol; a per-worker global is not one shared singleton. Brokered operations must document their scheduling and ordering; actor messaging may be used as an initial implementation with disclosed performance differences.

## Implementation order and acceptance gates

1. Compiler declaration/type identity, once initialization, complete initialization and escape checking.
2. Bounded table substrate: protected/private modes, typed scalar keys/values, atomic operations, generational capabilities and owner-death retirement.
3. Guest lowering in Java and LLVM; then .NET Native AOT, Forge native and LLVM-to-Wasm/worker transport using identical source cases.
4. Explicit delegation, heir transfer, immutable zero-copy leases, hot load and bounded iteration.
5. Capacity and worst-case operation measurements on pinned builds; hard-RT acceptance is separate from semantic parity.

Required deterministic cases:
- Concurrent first access executes initializer exactly once and all readers observe fully initialized state.
- Import aliases resolve to the same identity; separate domains do not.
- Direct and cross-task initialization cycles fail; initializer failure cannot leak partial state.
- Pre-publication self escape, mutable alias publication and actor-private state export fail checking.
- Cancellation, hot load and shutdown cannot publish a duplicate incarnation.
- Private/protected/public operations enforce rights using runtime caller identity.
- Copied, forged, attenuated and cross-domain capabilities cannot gain write authority.
- Many actors increment one counter with exact final value and a checked-overflow no-change case.
- Competing insert-if-absent/CAS have exactly the specified winners.
- Owner death, heir death, simultaneous deletion, in-flight read and name reuse preserve lifetime and reject stale handles.
- Input and lookup copies do not alias; frozen zero-copy data outlives all admitted readers only.
- Cursors, table capacity, bytes and pin limits fail deterministically; concurrent traversal never claims snapshot consistency.
- Symbols versus strings and typed numeric keys have identical behavior on every backend.
- Worker/isolate tests prove cross-actor identity instead of testing globals on one worker.

Use barriers and recorded histories rather than sleeps for races.
Positive cases require genuine compiler checking and execution with pinned source, compiler executable hash and identical guest bytes. Negative cases require semantic diagnostic classification; unsupported syntax rejection is not a safety-check pass.
Zero-step, skipped, unexecuted or infrastructure-failed CI is never conformance evidence.
No runtime/zero-GC/hard-RT parity claim is permitted from contract equality or host-only tests.

## Implemented Forge admission hardening

All three profiles reject top-level singleton declarations after optional define/pub modifiers with `ORES-SINGLETON-UNSUPPORTED` and the byte offset, before returning IR. The guard is at declaration position, so comments and ordinary identifiers containing or equal to singleton are not blanket banned.
Unsupported status remains a parity gap.

The standalone test covers 24 classified rejections (visibility/order and declarations before/after main), nine lexical controls and three real scalar/runtime/call interpreter executions returning 42.
CMake registers it as `oresforge_singleton_admission`.

Local validation: GCC 13.3, C++20, O1 with ASan/UBSan and O2 both pass. LeakSanitizer cannot inspect processes in this environment; O1 used ASAN_OPTIONS=detect_leaks=0. Leak detection is unverified.
The baseline test fails with an unclassified diagnostic, demonstrating the classification regression.
An existing Native aggregate missing-field warning requires -Wno-missing-field-initializers with -Werror; no unrelated warning fix is bundled.
Full CMake/CTest, hosted CI, other backend compilers and guest singleton/table execution were not run.

Reproduce from repository root:
```sh
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -Wno-missing-field-initializers -I. compiler/frontend.cpp compiler/ir.cpp compiler/call_native.cpp verification/singleton_admission.cpp -o /tmp/ores-singleton-admission
ASAN_OPTIONS=detect_leaks=0 /tmp/ores-singleton-admission
```

