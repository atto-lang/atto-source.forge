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


## Internal stage-0 compiled guest table effects (2026-10-31)

This draft now includes a **distinct opt-in compiler/interpreter profile**:
`parse_shared_i64_stage0_source()`. It accepts ordinary typed scalar
`fnc main(...)` expressions with four reserved internal test intrinsics:
`shared_i64_lookup(key)`, `shared_i64_insert(key, value)`,
`shared_i64_insert_new(key, value)`, and
`shared_i64_update_counter(key, delta)`. It is not the canonical final
Oreslang shared-table API. In this restricted scalar profile, lookup throws
`TableErrorCode::Missing` rather than exposing the final Option/Result type;
insert returns its i64 value, insert_new returns 0/1, and update_counter
returns the checked updated value.

The frontend lowers those intrinsics into dedicated verified IR opcodes. A
`Program.shared_table_stage0` marker prevents unadmitted table opcodes from
passing IR verification. The ordinary `parse_source` and
`parse_runtime_source` profiles reject intrinsic calls, and the ordinary
IR interpreter and native assembler reject this program profile rather than
silently executing side effects. Shadowing stage-0 intrinsic names is rejected.

`MailboxBoundI64GuestDispatch<Capacity>` binds a compiled program to a
trusted host mailbox and an opaque delegated table capability. Each operation
obtains the current live mailbox incarnation; **guest numeric data never
supplies the caller identity**. Host operations continue to enforce private,
protected and public rights, generation fencing, bounded capacity, atomicity,
overflow and lifecycle retirement. This is still a host test harness rather
than the final scheduler-issued execution lease or general guest FFI/ABI.

`verification/guest_shared_i64_stage0.cpp` exercises direct compiled
insert/lookup/insert-new/counter operations, read-only and mismatched grants,
missing-key and overflow failures, actor restart and owner shutdown, forged
IR and unauthenticated source rejection, and 80,000 concurrent compiled guest
counter updates. One additional test compiles an empty `define singleton`
shell and a pure scalar initializer, shares its compiler-owned declaration
identity across same-domain actors, then uses that value to seed a shared
table updated by a *separate* compiled guest table program. The singleton
initializer itself cannot perform table effects.

The new test is registered in CTest and in
`scripts/test-singleton-shared-runtime.sh` for release, ASan/UBSan and
explicit leak probe profiles. **Authored tests are not test passes**.
No exact-head execution claim may advance without nonzero job steps and
provenance receipts. Ordinary `define singleton` source, deep immutability,
final Oreslang table syntax, scheduler execution leases, native table lowering,
cross-worker memory/broker mediation and all five-backend parity remain gated.

## Actor execution leases and exact-blob verification (2026-10-31)

The internal guest dispatch now obtains an `ActorExecutionLease` from the
trusted `Mailbox` for **each** shared-table operation. The lease pins the
specific live actor incarnation while checking its existing capability and
executing the table operation. A lease cannot be default-constructed, copied
or constructed from numeric guest arguments. The trusted mailbox is the only
factory. This is not yet a scheduler-issued lease in the production actor
interpreter, and the adapter must never be exposed directly to guest FFI.

`CapabilityToken::invalidate()` now waits for outstanding leases via an
execution shared/exclusive lock. It only fences the old authority and invokes
its actor-owned resource observers **after** the admitted operations have
quiesced. `Mailbox::close()` and `Mailbox::restart()` hold their admission
lock while invalidating the prior incarnation. The stage-0 dispatcher releases
its lease after each operation; no lease crosses await, select or guest
callbacks. These host locks do **not** establish hard-real-time progress or
bounded cancellation latency.

`verification/actor_execution_lease.cpp` contains construction/admission
negative tests, close/restart ordering with an outstanding lease, and 128
latch-coordinated admission/shutdown races. CTest and the direct
release/sanitized/sanitized-leaks script include the executable. This is
authored coverage, **not** a runtime test result.

An alternate private test carrier was prepared from source SHA
`a879eba78f3c0ba9f4829945d47944b25fa09353` and root tree
`5fc72a829484502cb5974531a65ee571abb1c6d6`.
Its `forge-singleton-proof` directory contains 24 source blobs checked
against their original Git blob SHAs; this is a **verified source slice**,
not an identical full-repository tree. Carrier commit
`659727ecfd0ff535ab276a7357f64a4a669dc83f` has a
manifest and a token-free, local-checkout test workflow:
https://github.com/voxletra/voxletra-e2e/actions/runs/41230900580 .
Release, sanitized and sanitized-leaks jobs all ended with
`runner_id=0` and `steps=null`; no compiler or test executed.
Do not interpret this as passing sanitizer evidence.

Future promotion requires a nonzero-step runner for the *current source
head*, blob parity on all compiled inputs, compilation under GCC and Clang,
CTest, release/ASan/UBSan/leak result receipts, and the production scheduler's
own execution admission semantics. No backend is currently promoted.

## Two-phase actor shutdown and reentrancy audit (2026-10-31)

The stage-0 Forge mailbox now distinguishes **requesting** actor shutdown
from **waiting for completion**. `Mailbox::request_close()` marks the
mailbox closed and rejects new actor admissions without waiting for its
currently running lease. On the last lease release,
`CapabilityToken` invalidates authority and retires actor-owned tables.
`Mailbox::close()` still waits for external callers; synchronous
`close()` or `restart()` from the actor's own lease throws a classified
runtime error instead of self-joining. A supervisor must perform a
synchronous restart after quiescence; the actor's request method does not
create a new incarnation.

Lifecycle joins no longer hold the mailbox mutex. An external restart
marks an in-progress transition, releases the mailbox lock to wait for
old-actor quiescence, then publishes a fresh generation only if no
`request_close()` or `close()` arrived meanwhile. Shutdown during the
transition cancels the restart without exposing a fresh incarnation.

Every admitted lease is tracked under a bounded 4,096-slot per-actor
admission budget. Following the stop request, a previously obtained
`ActorContext` is valid only on a thread holding an admitted lease for
that same authority. External unleased contexts are denied even while the
last operation drains. The internal lease is **thread-affine**, not a
network handle, and the production guest scheduler still needs its own
non-forgeable execution context and cooperative cancellation integration.

Retirement callbacks must not synchronously call `close()` or
`restart()` on the same actor. These paths now fail fast inside a
retirement callback, while `request_close()` remains idempotent and
nonblocking. Failure in host retirement synchronization causes a
fail-stop condition rather than a false success receipt; no hard-real-time
progress or general callback safety is established.

Added unexecuted tests in `verification/actor_execution_lease.cpp` cover
self-close and self-restart rejection, deferred final-lease retirement,
cross-thread stale context denial, nested leases, budget exhaustion,
external close versus self-close requests, racing supervisor restart
cancellation, reentrant retirement callbacks and 128 admission/close
races. CTest and the release/ASan/UBSan/leak test script already include
this executable.

**Verification limitation:** A source-free private runner canary on
`honeypot-r-us-test/hnpt-admin-api-server.rs` was attempted at
https://github.com/honeypot-r-us-test/hnpt-admin-api-server.rs/actions/runs/41233454560.
It failed before runner assignment (`runner_id=0`, `steps=null`).
The formerly successful run on this private carrier was from October 30;
its historical runner assignment is not proof of available capacity now.
No private Forge source was copied to this carrier following the failed
canary. The older SHA-verified Voxletra source slice is **not** current
head, and its own execution attempt also failed before assigning a runner.
No current-head release, CTest, sanitizer or guest-conformance pass is claimed.

Remaining gaps: a production scheduler-issued execution lease; normal
`define singleton` frontend admission; final typed table APIs, owner
heir/transfer notices and cancellation policy; actual worker/isolate
sharing; five-backend conformance receipts.
