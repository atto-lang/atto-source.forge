# OresVM / TenantVM contract v0 (legacy IsolateVM backend admission)

**Canonical naming (2026-11-11): TenantVM** is the tenant execution unit beneath OresVM. `IsolateVM` and the existing host type spellings remain historical/compatible; the Forge JIT does not gain untrusted isolation by renaming. See [TenantVM naming](TENANT_VM_NAMING_V1.md).

**Status: experimental host substrate.** Not full actor VM execution,
complete cross-backend parity, per-tenant physical resource enforcement or
a security sandbox. No general guest-code tenant isolation is claimed.

**2026-11-18 decision:** Forge builds an independent *native* protected TenantVM
analogous to LLVM, without Wasmtime. The new `ProtectedNative` admission mode
fails closed until verified compiler-instrumented memory confinement exists.
See [Forge native TenantVM decision](NATIVE_TENANTVM_DECISION_20261118.md).

## Common vocabulary

- **OresVM:** root host, code registry, permission broker, global admission
  and tenant supervisor. It must not own tenant-writable guest module globals.
- **IsolateVM:** an OresVM-owned execution domain for one tenant incarnation;
  owns private mutable globals, heap/resource limits, actor registry and
  quiescence accounting. Reusing a tenant ID creates a new generation.
- **Actor:** individually scheduled concurrent unit inside exactly one
  tenant's IsolateVM, with sealed state, owned arena and mailbox.
- **Isolate execution context:** a versioned *borrowed entry capability*;
  never a raw writable pointer to the OresVM root or another tenant.

## Required lifecycle invariants

1. Created -> Running -> Quiescing -> Stopped -> destroyed.
2. Reject duplicate tenant IDs, unbounded admission, invalid quotas,
   stale/foreign/forged handles and actor ID replay after reincarnation.
3. Closing admission during quiescence must be atomic with new guest,
   actor, mailbox, callback and I/O registration accounting.
4. Complete shutdown only after guest calls and all actor,
   continuation, callback, mailbox-loan and I/O retirement have drained.
   Accounting is insufficient without physical OS/worker retirement.
5. Cross-isolate messages must be versioned and serialized or explicitly
   transferred by checked ownership. No borrowed raw pointers cross tenants.
6. A shared immutable AOT module may have different *tenant-owned*
   writable globals. Neither mutable module state nor user symbol identities
   silently become process global.
7. Include a distinct status/result ABI, including legitimate signed
   negative values. Check native artifact entry ABI **before** function
   pointer casts; ABI version mismatch must fail closed.
8. An in-process isolate is **not an OS security sandbox**. Native/FFI
   arbitrary pointers require process-level separation for hostile tenants.

## V8 lessons and crucial differences

V8 separates an isolate's engine/heap from its Context globals and supplies
host platform scheduling and explicit entry/handle scopes. Oreslang should
borrow explicit context ownership, generation-fenced handles, snapshots
for immutable initialization and fast tenant-specific state, **not**
the assumption that a V8 Isolate is equivalent to a renderer-process
security boundary. Oreslang must eventually schedule independently sealed
actors within one tenant concurrently, rather than imposing one
entering thread for the whole tenant. Pointer compression changes memory
representation, not authorization. In-process host admission is insufficient
for arbitrary untrusted native machine code.

Architecture reference: LLVM draft PR #1158,
`docs/ORESVM_VS_ISOLATEVM.md` in `oreslang-source.llvm`.

## Forge C++20 implementation

`runtime/isolate.hpp` provides a non-copyable OresVM host authority with
generation-fenced tenant/actor handles and explicit host-owned operation
pins. Distinct tenants have independent registries and actors. Per-actor
pins reject actor retirement until the native owner signals completion.
No compiler emitted machine-code isolate-context ABI yet; Forge's native
JIT/AOT machine code is **not** a security sandbox. The class deliberately
does not share any code or runtime dependency with Cranelift.
CTest: `oresforge_tenant_isolate_host_admission`.
The exact header/test passed local GCC C++20 and Clang ASan/UBSan; full
Forge repository CMake/CTest CI is still required.


## Executable source-to-native IsolateVM slice (follow-up)

The host registry now exposes a synchronous **trusted native scalar**
entrypoint `invoke_trusted_scalar(handle, entry, delta)`. This retrieves
the current tenant-local i64 value, invokes the Forge-compiled
`oresforge_entry(old,delta)` machine-code function and commits the returned
value to **only that tenant**. The native code receives only scalar
parameters, not a mutable OresVM/IsolateVM pointer. A per-tenant mutex
protects read/modify/write while a host call pin fences shutdown, including
queued invocations. Nested native entries are rejected.

The new `oresforge_compiled_tenant_isolate_native` CTest runs the **real
source compiler** on `verification/tenant_scalar_entry.ores`, emits
x86-64 System V assembly, assembles/links it with an independent C++ host
driver, and executes two tenants concurrently for 1,000 increments each.
It validates separate signed values, shutdown admission, neighbor progress
and fresh state after tenant reincarnation. This is an opt-in stage-0
profile, not general actor lowering.

The narrow old+delta native guest is guarded by an explicit checked
host preflight under the per-tenant turn lock. It rejects integer overflow
before entering machine code that would otherwise trap with ud2. It also
rejects malformed tenant IDs before registration. This is NOT a general
replacement for a status/result guest ABI: arbitrary trusted Forge native
guest code may still trap or access host memory.

Local evidence: the exact committed hardening blobs for
`runtime/isolate.hpp` (`c7ee3d9e04a0f6e49782f9269ca2e078430626e9`),
`verification/isolate_host_test.cpp`
(`b590d07ca9363f3f48d1adf24b7ccae62ee52f65`) and
`verification/native_isolate_scalar_guest.cpp`
(`468fdbbdbbd0c607994c554a2cf673e995daff23`) passed GCC C++20
and Clang AddressSanitizer/UBSan local checks. The native guest in these
local checks is **manually assembled**, not Forge compiler output.
The full Forge compiler/assembler CTest remains unverified until
executed by a GitHub Actions runner at the exact source SHA.
