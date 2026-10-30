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

`runtime/shared_i64_table.hpp` provides `SharedI64Table<Capacity>`:
- Fixed slot storage, exact i64 keys and values, copy-based lookup.
- Private, protected (default), and explicitly writable public modes.
- Opaque capabilities bound to one table identity, domain, actor and incarnation.
  Copies preserve the same grantee and rights. Empty, foreign-table, foreign-domain,
  and restarted-actor handles fail authorization.
- Only the owner delegates. Protected tables cannot delegate non-owner writes;
  public tables still require an explicit write capability.
- Mutex-linearized lookup, insert, insert-if-absent, CAS, erase and checked counter
  update. Capacity/overflow/missing-key errors leave contents unchanged.
- Close and the trusted owner-death hook retire under the same mutex as operations.
  Old handles cannot authorize a replacement table, including after destruction.

A shared-pointer control-block identity is used for this host slice. It is not a
wire handle or a guest ABI. Capabilities do not keep a retired table alive.
No raw slot pointer or mutable reference is returned; operation locks provide
quiescence, so this slice has no externally borrowed slot memory to reclaim.

## Trust boundary and limits

`ActorContext` and `owner_stopped()` are TRUSTED HOST INPUTS. There is no scheduler
binding in this change. A guest must never construct a caller context, invoke the
owner-capability factory or submit an owner-death notification. A guest adapter
must derive caller identity from the admitted execution lease, validate guest
capabilities and dispatch to the matching host table.

Keeping C++ fields private does not create an FFI security boundary. Do not
serialize/export these host structs or permit native memory manipulation.

Each table allocates its fixed slot storage once, then uses bounded linear scans
(no hash collision variability). Successful operations create no dynamic slot
allocation, call no guest code and expose no borrowed storage. Exception delivery
and host locks/reference-counting still use host runtime machinery. Per-application
table count, total byte budgets, compiler Error/Result mapping, owner-death wiring,
heir transfer, capability revocation, and guest permission checks remain pending.

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
ASAN_OPTIONS=detect_leaks=0 bash scripts/test-singleton-shared-runtime.sh sanitized
```

The script records the compiler version and SHA-256 of each test executable,
builds directly without Python, and applies 30-second execution timeouts.
The release profile uses O2; sanitized uses O1 with ASan/UBSan.
Leak detection is disabled only for the local restricted process environment;
the hosted workflow uses the sanitizer defaults.
CMake registers `oresforge_singleton_shared_runtime` and the existing admission test.
The dedicated read-only workflow checks out the exact PR head and runs both profiles.

Runtime tests exercise:
- Authorization, capability identity/incarnation, delegation and private/protected modes.
- Capacity rollback, replacement at capacity, exact key access and erased-slot reuse.
- CAS mismatch, missing key, overflow/underflow and INT64_MIN delta.
- Eight concurrent writers and 80,000 atomic increments with exact final value.
- Exactly one insert-if-absent winner and one CAS winner.
- Owner retirement racing reads, stale replacement handles and expired capabilities.
- Concurrent singleton publication with identical reader identities and complete values.
- Sticky failure, recursion, cross-thread initializer cycles and cross-type nesting.
- Allowed published dependencies, close before/during/after initialization,
  retained reader leases and reentrant initializer/destructor cleanup.

The admission test still requires 24 classified singleton rejections, nine lexical
controls and three interpreter executions. Passing these host tests does not prove
compiled guest singleton/table semantics or parity with Erlang.

## Next compiler integration gate

1. Add declaration identity and a domain-owned cell registry, with budgets and shutdown.
2. Prove complete initialization, no receiver escape, deep immutable sharing and effects.
3. Bind table dispatch and lifecycle to scheduler-derived identity; map typed errors.
4. Compile identical guest programs with positive and negative semantic oracles.
5. Reuse the native substrate where appropriate for LLVM, and implement matching
   .NET, Java/Graal and Wasm transports with exact-source execution receipts.

Keep unsupported admission in place until these gates pass.

