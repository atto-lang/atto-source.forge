# Singleton/shared-table host runtime slice

This slice implements tested C++ host primitives in Forge. It does not enable
`define singleton`, create a declaration registry, or expose a guest shared-table API.
The compiler's `ORES-SINGLETON-UNSUPPORTED` gate remains in place.
This is not five-backend conformance.

## Implemented behavior

`runtime/singleton_cell.hpp` provides `SingletonCell<T>`:
- One initializer executes; concurrent callers wait for complete publication.
- Initialization runs outside the mutex; failure is sticky and not retried.
- Published values use immutable reader leases. Closing denies new readers;
  existing leases retain their value. Closing during initialization prevents publication.
- Payload destruction occurs outside the mutex, including reentrant cleanup.
- An initializer may access an already published singleton. Access to an
  unpublished singleton from any synchronous initializer is rejected, including
  across different `T` types and across mutually dependent initializer threads.

The dependency guard deliberately rejects acyclic nested initialization too.
It does not solve arbitrary asynchronous wait graphs: initializers that spawn a
helper thread and wait for that helper to access their cell can still deadlock.
Initializers must be synchronous and must not launch/wait for dependent work.
A future compiler/scheduler adapter must enforce these effects before guest admission.
Cell destruction requires host quiescence; `close()` does not join initializer threads.
Cancellation and deadline-aware waits are not implemented.

`runtime/singleton_registry.hpp` provides a bounded application-domain registry:
- The registry is bound to one nonzero runtime domain and accepts only live,
  runtime-minted `ActorContext` callers from that domain.
- Singleton declarations are addressed by a two-part
  `SingletonDeclarationId { module, declaration }`. This is a trusted
  compiler/runtime identity, not a guest-supplied numeric key. Resolved import
  aliases must carry the exact same declaration ID.
- Concurrent actors resolving the same declaration converge on one
  `SingletonCell<T>` and therefore one published immutable reader identity.
  A restarted actor in the same domain can resolve the existing singleton;
  its stale pre-restart context is denied.
- Different declaration IDs never alias. Initialization failure remains sticky
  because the declaration slot is retained for the registry lifetime.
- Registry capacity is fixed at compile time. There is no hot-unload/reuse
  protocol yet, so every first-resolved declaration permanently consumes one
  slot until domain teardown.
- `close()` marks the registry closed before closing used cells outside the
  registry mutex, allowing reentrant payload cleanup without holding the
  registry lock. Existing immutable reader leases remain valid.


`runtime/shared_i64_table.hpp` provides `SharedI64Table<Capacity>`:
- Fixed slot storage, exact i64 keys and values, copy-based lookup.
- Private, protected (default), and explicitly writable public modes.
- Opaque capabilities bound to one table identity, domain, actor and incarnation.
  Copies preserve the same grantee and rights. Empty, foreign-table, foreign-domain,
  and restarted-actor handles fail authorization.
- Only the owner delegates. Protected tables cannot delegate non-owner writes;
  public tables still require an explicit write capability.
- Explicit `give_away(owner_cap, caller, next_owner)` transfers ownership to
  another **live, same-domain actor incarnation**. No guest-supplied actor
  identity or automatic heir is accepted. The new owner obtains its own owner
  capability; old owner grants carry an ownership generation and are **permanently
  stale** after transfer, even if the table is later transferred back.
  Existing independently delegated read capabilities remain valid. Public-mode
  explicit write grants retain public write semantics.
- Transfer first attaches a fresh actor-death observer *outside* the table lock
  to avoid the actor-resource/table-lock inversion. It then validates and
  atomically activates the new observer under the table lock. A dead recipient,
  concurrent handoff, capacity failure or caller shutdown leaves the original
  owner unchanged (unless its own shutdown retires the table). Obsolete or
  failed-handoff observers cannot retire a new owner's table.
- Mutex-linearized lookup, insert, insert-if-absent, CAS, erase and checked counter
  update. Capacity/overflow/missing-key errors leave contents unchanged.
- Each table weakly registers as an actor-owned runtime resource in a fixed
  256-slot per-incarnation registry. Registration does not dynamically grow;
  exhaustion fails table construction with `Capacity`, and expired slots are
  reusable. Mailbox close, restart, and destruction automatically retire every
  table owned by that exact incarnation under the same mutex as operations.
  The explicit trusted
  `owner_stopped()` hook remains available for scheduler adapters, but ordinary
  lifecycle correctness no longer depends on callers remembering to invoke it.
  Old handles cannot authorize a replacement table, including after destruction.

A shared-pointer control-block identity is used for this host slice. It is not a
wire handle or a guest ABI. Capabilities do not keep a retired table alive.
No raw slot pointer or mutable reference is returned; operation locks provide
quiescence, so this slice has no externally borrowed slot memory to reclaim.

## Trust boundary and limits

`ActorContext`, owner-capability minting, resource registration, and the explicit
`owner_stopped()` compatibility hook are TRUSTED HOST APIs. `ActorContext` is
runtime-minted from a live mailbox incarnation and cannot be numerically
constructed. Mailbox close/restart/destruction fences that incarnation and
automatically retires its registered shared tables before the lifecycle operation
returns. A guest must never manufacture caller identity, invoke owner-capability
factories, register arbitrary host resources, or submit lifecycle notifications.
A future guest adapter must derive caller identity from the admitted execution
lease, validate guest capabilities and dispatch to the matching host table.

Keeping C++ fields private does not create an FFI security boundary. Do not
serialize/export these host structs or permit native memory manipulation.

Each table allocates its fixed slot storage once, then uses bounded linear scans
(no hash collision variability). Actor-resource registration is a fixed 256-slot
scan and does not grow a container. Successful operations create no dynamic slot
allocation, call no guest code and expose no borrowed storage. Exception delivery
and host locks/reference-counting still use host runtime machinery. Per-application
domain-wide table count/byte budgets beyond the per-actor 256-resource ceiling,
compiler Error/Result mapping, automatic heir handoff, fine-grained capability revocation,
and guest permission checks remain pending.

A `shared_ptr<const T>` does not prove deep immutability of reachable objects.
Compiler checking must reject mutable/actor-private aliases and initializer
receiver escape before admitting singleton declarations.

No naming registry, cross-process/isolate/worker singleton sharing, general key
types, snapshots, iterators, zero-copy borrowed values, hot loading or restart
protocol is implemented. There is no hard-real-time or zero-host-allocation claim.
Retirement logically clears slots; secure physical zeroization is not claimed.

## Tests and reproducibility

From repository root:
```sh
bash scripts/test-singleton-shared-runtime.sh release
bash scripts/test-singleton-shared-runtime.sh sanitized
bash scripts/test-singleton-shared-runtime.sh sanitized-leaks
```

The script records the compiler version and SHA-256 of each test executable,
builds directly without Python, and applies 30-second execution timeouts.
The release profile uses O2; sanitized uses O1 with ASan/UBSan. The
`sanitized-leaks` profile uses the same instrumentation but first runs an
intentional leak capability probe and requires LeakSanitizer to report it with
the configured nonzero exit code before executing the real tests with
`detect_leaks=1`. Leak detection has not yet produced runner-executed hosted
evidence. The current hosted workflow attempts release, sanitizer, and explicit
leak profiles, but recent Actions runs failed before runner assignment and
executed zero steps; those runs are not test evidence. CMake registers `oresforge_singleton_shared_runtime` and the existing
admission test.

Runtime tests exercise:
- Authorization, capability identity/incarnation, delegation and private/protected modes.
- Capacity rollback, replacement at capacity, exact key access and erased-slot reuse.
- CAS mismatch, missing key, overflow/underflow and INT64_MIN delta.
- Eight concurrent writers and 80,000 atomic increments with exact final value.
- Exactly one insert-if-absent winner and one CAS winner.
- Automatic owner retirement on restart, close and mailbox destruction, including
  retirement racing reads, stale replacement handles and expired capabilities.
- Explicit same-domain ownership transfer, old-owner cleanup fencing, new-owner
  retirement, denied foreign/stopped candidates, capacity rollback, and 128
  old-owner teardown vs handoff races, and ABA-style handoff-back stale grant
  checks. These are authored regression cases,
  not current-head execution results.
- Filling the 256-resource lifecycle registry, deterministic capacity failure,
  expired-slot reuse, and full-set retirement on restart.
- Concurrent singleton publication with identical reader identities and complete values.
- Domain registry resolution from multiple actors, import-alias identity,
  foreign/stale caller rejection, declaration isolation, sticky registry
  failure, bounded declaration capacity and registry close behavior.
- Sticky failure, recursion, cross-thread initializer cycles and cross-type nesting.
- Allowed published dependencies, close before/during/after initialization,
  retained reader leases and reentrant initializer/destructor cleanup.

The admission test still requires 24 classified singleton rejections, nine lexical
controls and three interpreter executions. Passing these host tests does not prove
compiled guest singleton/table semantics or parity with Erlang.

## Stage-0 compiler/runtime bridge

`compiler/singleton_stage0.{hpp,cpp}` now provides an explicit internal bridge
for the already-established empty declaration shell:

```ores
define singleton Settings as end
fnc main() -> int { return 42; }
```

This bridge is intentionally **not** the general Forge frontend and is not a
parity promotion. The normal scalar/runtime/callable frontends still reject
`define singleton` with `ORES-SINGLETON-UNSUPPORTED`.

The stage-0 compiler:

- accepts exactly one explicit `define [pub] singleton Name as end` declaration
  before the existing scalar program;
- receives resolved module identity as trusted compiler input, not guest data,
  and derives an opaque bootstrap `SingletonDeclarationId`;
- removes only that empty declaration shell and compiles the remaining scalar
  source with the real Forge scalar frontend;
- uses the scalar program's `main()` result only as a temporary stage-0
  initializer value, avoiding invention of final singleton field/member syntax;
- classifies a receiver-name reference in the unsupported singleton body as
  `ORES-SINGLETON-PREPUBLICATION-ESCAPE` before the generic stage-0 body
  rejection; and
- rejects every other non-empty singleton body with
  `ORES-SINGLETON-STAGE0-BODY-UNSUPPORTED`.

`verification/singleton_guest_stage0.cpp` binds the compiled declaration ID to
the bounded application-domain registry and drives concurrent first access from
two same-domain actor contexts. It requires exactly one execution of the
compiled scalar initializer, complete value 42 publication and one shared
reader identity across eight contenders. It also proves the normal frontend
remains fail-closed and comments do not count as receiver escape.

The bootstrap ID mapping currently uses deterministic FNV-1a components. It is
not a security capability or final cross-artifact/wire identity and must be
replaced by the full compiler's collision-safe resolved declaration identity
before any parity promotion. The stage-0 bridge also does not establish final
singleton member syntax, deep immutability, cancellation, scheduler dispatch,
worker/isolate behavior, shared-table guest API semantics or hard real-time
properties.

## Next compiler integration gate

1. Bind compiler-resolved module/declaration identity to the existing bounded
   domain registry; guest code must never manufacture declaration IDs.
2. Prove complete initialization, no receiver escape, deep immutable sharing and effects.
3. Bind guest table dispatch to scheduler-derived identity and map typed errors;
   host owner lifecycle is already automatically wired to mailbox incarnations.
4. Compile identical guest programs with positive and negative semantic oracles.
5. Reuse the native substrate where appropriate for LLVM, and implement matching
   .NET, Java/Graal and Wasm transports with exact-source execution receipts.

Keep unsupported admission in place until these gates pass.

