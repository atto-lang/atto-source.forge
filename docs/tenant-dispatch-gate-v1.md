# Forge trusted-host TenantVM dispatch gate v1

**Status: HOST_ONLY; NOT_EXECUTED for hostile compiled Atto guests.**
Source: `include/atto/tenant_dispatch_gate_v1.hpp`; tests:
`tests/tenant_dispatch_gate_v1.cpp`. This gate is an admission and
receipt-queue primitive, **not** an executing carrier pool, guest compiler,
confining TenantVM, or process-isolated guest launcher.

## Enforced by this component

- Host-minted tenant credentials are issuer-bound and fenced by monotonic
  non-reused IDs. Destroying the gate invalidates weak issuer credentials.
  Closing a tenant discards undelivered queue receipts; no re-admission with
  the old credential succeeds.
- Ordinary DATA and authenticated supervisor CONTROL use distinct credential
  types. The runtime must retain the supervisor credential exclusively in
  trusted host code, not expose it in a guest import, address space, or mailbox.
- Per-tenant DATA and CONTROL lanes are bounded. The configured global
  CONTROL reserve must cover each admitted tenant's CONTROL reserve, and
  DATA cannot spend these reserved slots. CONTROL has a bounded burst before
  a pending DATA message is serviced.
- The trusted host must call `dequeue` for the matching tenant credential.
  The gate never steals a receipt across tenants, copies guest memory,
  holds a guest callback, or executes guest functions under its mutex.
- `untrusted_in_process` and `untrusted_process_fallback` admission both
  return `unsupported`; no unimplemented sandbox fallback is silently used.

## Threat boundaries NOT enforced

- A `TenantToken` is a **trusted-host capability**, not an unforgeable
  in-guest cryptographic token. The gateway must validate source identity,
  actor incarnation, message type, receipt uniqueness, and capability grants
  before calling this component. This gate does not attest those inputs.
- A hostile native instruction can still reach process memory/OS syscalls
  unless a *separate, independently verified* TenantVM confinement mechanism
  prevents it. A host-side C++ token, thread pool, Rust ownership checker,
  memory arena, `io_uring`/socket broker, or code signature alone is not
  a hostile-code sandbox.
- This gate does not schedule real actor frames or own worker threads.
  Close discards queue entries but does not cancel already-dequeued work,
  invalidate active borrowed pointers, close sockets, or quiesce suspended
  actor frames. The surrounding runtime must fence and quiesce before memory
  reclamation.
- Queue receipts are host metadata, **not** host callbacks, guest payloads,
  raw FDs, pointers, or executable permissions. No cross-tenant message
  transport is provided here.
- Process fallback requires a distinct implementation with per-tenant
  sandboxed processes. This v1 component refuses fallback rather than
  inventing an unsafe in-process substitute.

## Required integration/verification sequence

1. Integrate host-authenticated actor and supervisor registries with the gate;
   prove caller-origin and actor-incarnation binding rather than trusting
   caller-supplied integers.
2. Implement stackless, cage-local continuation frames with immutable
   `(tenant, generation, actor, incarnation, frame-slot, frame-generation)`
   ownership, bounded per-tenant fuel, strict wake/cancel fencing and
   quiescent retirement. Work stealing stays within the same TenantVM.
3. Supply independently verified in-process sandboxing (validated Wasm or
   reviewed native SFI/CFI/hardware enforcement), denial of raw guest syscalls
   and host pointers, bounded linear memory/stacks, and a capability-brokered
   network-only I/O gateway. Until then, reject hostile same-PID admission.
4. Run real compiled hostile `.atto` guests A and B concurrently in one
   measured PID. Adversarially test cage OOB, forged CONTROL/continuations,
   stale generations, unauthorized FD/host import access, quota/loop
   termination, and healthy-tenant survival. Provide exact source/compiler
   and artifact SHA-256 plus executed CI/guest receipts.

This module's local C++20 GCC/Clang ASan+UBSan tests are only **host unit
evidence**; they do not satisfy steps 1–4.
