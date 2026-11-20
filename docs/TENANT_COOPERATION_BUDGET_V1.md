# Forge tenant-owned cooperation budget v1

This reconciles the legacy Forge proposals
[oreslang-source.forge #222](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/222),
[#223](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/223), and
[#224](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/224)
in the canonical Atto Forge repository without copying an inconsistent legacy
meter into production guest execution.

## Normative host contract

- The **polling phase belongs to the task**, remains in [0, 127], and
  persists across dispatch, suspension, resume and sibling task execution.
  Charge at most 128 units per bounded operation. Every crossing of a 128-unit
  boundary produces a cold-poll result, even if dispatches are 127 units long.
- **512 committed units per dispatch** is a hard admission limit, independent
  of polling phase. A charge that would exceed it is rejected atomically.
  A charge landing exactly at 512 returns Yield and cannot admit more work
  on that dispatch lease.
- **Lifetime fuel** is reserved on task admission from the tenant's bounded
  allowance, and charged only for committed work. A short/failed charge or
  redispatch cannot refill fuel. Retiring a task does not refund the tenant's
  lifetime allotment, preventing repeated task creation from bypassing limits.
- Each tenant, task and dispatch carries a host-issued opaque ticket tied to
  an issuer incarnation. Retired tasks/tenants, stale dispatch serials,
  cross-tenant reuse and cross-registry identity replay are denied.
- At most one dispatch lease per task may be active. The trusted host must
  close the lease after each scheduler turn. The budget serializes concurrent
  charges and never invokes arbitrary callbacks while holding its mutex.
- Admission limits are 1024 tenants per registry, 1024 tasks per tenant,
  at most 1,000,000,000 lifetime fuel units per tenant, and 1..128 units
  per charge. Invalid input and quota exhaustion fail closed.

## Trust boundary

**Host-only implementation, not a guest sandbox.** Opaque C++ constructors and
a mutex are not isolation from malicious native instructions. This header does
not inject compiler checkpoint instructions, suspend arbitrary native/FFI
calls, provide a resumable guest continuation, enforce wall-clock deadlines,
revoke already-running uninstrumented code, or authorize untrusted TenantVM
admission. The existing native trusted-scalar path remains trusted-only.

The compiler must prove coverage of loops, recursion, dynamic calls and
post-optimization basic blocks, reject unmetered code, and check 128-unit
maximum work weights. The scheduler must force a poll/Yield at appropriate
safepoints and cannot skip checks by issuing fresh dispatch tickets. TenantVM
memory, I/O capability and control-flow confinement must be independently
verified before any untrusted same-process guest admission.

## Native regression tests

CTest target: tenant_cooperation_budget_v1_test

The test exercises short-dispatch poll evasion, exact 128/512 boundaries,
weighted-charge atomicity, fuel depletion, dispatch replay, active-lease
reset denial, cross-tenant/issuer theft, retirement and generation replacement,
parallel sibling tenants, concurrent charges, and invalid quota inputs.

Source CI only counts as executed when real checkout, build and CTest steps
ran on the exact PR head. A failed GitHub Actions allocation with zero job
steps is **NOT_EXECUTED**, not a test failure or pass. Locally compiled
host-header regression results never imply compiled Atto guest parity.
