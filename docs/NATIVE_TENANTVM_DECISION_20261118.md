# Forge native TenantVM decision (2026-11-18)

## Decision and status

Forge builds its **own native TenantVM** using the same security invariants as the LLVM backend, without a Wasmtime, WebAssembly, Cranelift or GraalVM execution dependency.

**Selected design, NOT implemented as a hostile-tenant sandbox.** The current `runtime/isolate.hpp` is a trusted C++ OresVM registry, not a protected address space. Native executable code can still dereference arbitrary process addresses. `TenantIsolationMode::ProtectedNative` therefore must **reject admission** until actual compiler/runtime enforcement is integrated and security-tested. The default remains `TrustedNative` for existing verified stage-0 host tests; no implicit upgrade is claimed.

The LLVM implementation and Forge are **independent codebases**: port security *contracts and negative tests*, not LLVM machine code, a shared unsafe C++ guest loader, or a second incompatible actor heap implementation.

## Proposed architecture

```text
Native OresVM root (trusted host)
  immutable compiler-verified executable code / versioned guest entry ABI
  tenant admission, capability broker, scheduler, aggregate quotas
    Forge TenantVM A: generation A, private globals, memory region, budget
      Actor A1 private bounded arena, sealed state, own mailbox + turn
      Actor A2 private bounded arena, sealed state, own mailbox + turn
    Forge TenantVM B: generation B, different memory region and grants
      Actor B1 private arena, mailbox + turn
```

### Memory confinement: must be enforced, not inferred

1. **Verified guest producer**: no arbitrary native machine code, dynamic guest libraries, unchecked inline assembly or unrestricted FFI in `ProtectedNative`. A compiler-known native ABI must limit entrypoints and host-call imports. If the backend cannot prove a safe guest code generation profile, reject compilation/admission.
2. **Checked 32-bit offsets** into a tenant-owned bounded region. Materialize native addresses only after verifying tenant authority, region generation, overflow-safe offset+length bounds, alignment, object/actor ownership and read/write permissions. Raw pointers must never appear as untrusted guest-visible capabilities.
3. **Compartmentalize writable memory**: tenant globals, actor regions, private object arenas, stacks and message buffers must not alias another tenant's writable pages. Share code read-only; immutable data only under an explicit cross-tenant sharing contract.
4. **No pointer escape**: prohibit casting guest-controlled integers or indirect-call tables into host addresses; avoid raw `void*`, C++ references or cross-tenant borrowed addresses in the guest ABI. All privileged services go through a checked broker and generation-fenced opaque handles.
5. **Fault/liveness isolation**: protected guest traps and bounds violations must end **only their tenant**, without a process-fatal SIGSEGV or SIGILL. Do not claim that guard pages alone achieve this: ordinary native faults can kill the entire host. Enforce guest checks at safe instruction boundaries and admit only verified code; process-level isolation remains defense in depth.
6. **Resource revocation**: enforce heap, stack, CPU/reduction, mailbox, callback and I/O budgets. A spinning native loop without instrumentation cannot be cancelled safely; reject such code. Quiesce admission first, drain physical calls and native resource pins, then reclaim arenas and invalidate handles.
7. **Scheduling**: actors have separate sealed state/arenas within their tenant. Allow different actor turns concurrently on distinct carrier threads when safe; do not enforce a tenant-global lock as a substitute for ownership. Only one mutable actor turn at a time.

Memory protection keys (MPK/PKU on supported CPUs), page protections and checked handle tables may be **additional** defenses after verifying compiler-generated access discipline; they are not the default guarantee. MPK has a limited number of protection keys, thread-scoped permission registers, and requires carefully validated trusted/native transitions. Support platforms without MPK via the same checked-offset guest profile, or fail closed when the requested protection strength cannot be met.

### API intent

`create_isolate(id, max_actors, TenantIsolationMode::TrustedNative)`: current host-only semantics, existing callers unaffected.

`create_isolate(id, max_actors, TenantIsolationMode::ProtectedNative)`: future opt-in; currently unconditionally fails closed. Unknown enum values fail closed. No silent downgrade, alias to trusted mode or Wasmtime fallback.

Do not rename existing versioned `IsolateHandle`, `ActorTicket`, `OresVM` symbols or wire artifacts without separate compatibility coverage; `TenantVM` is canonical architecture vocabulary.

## Implemented host-memory foundation (2026-11-18)

`runtime/tenant_memory_region.hpp` now implements a real, bounded native
`TenantMemoryRegion` (1 to 16 MiB, zero-initialized) with independent
memory per region. Host-issued read-only or writable `Lease`s are unforgeable
through the public C++ constructor API, bind to their original region and
generation, and only permit overflow-safe range-checked copies under a lock.
Retirement invalidates all leases, denies new operations and zeroes bytes.
The API deliberately exposes no guest-visible native `uint8_t*` or
unchecked memory reference. This is a **trusted host-side authority**:
untrusted native machine code is NOT sandboxed and protected mode remains
closed.

`verification/tenant_memory_region_test.cpp` covers denied cross-region
leases, stale-after-retire use, read-only enforcement, uint32 overflow,
concurrent disjoint host writes, tenant recreation with fresh zeroes and
neighbor survival. CMake registers
`oresforge_tenant_memory_region_host_only` with strict warnings.

**Local exact-blob evidence, not GH Actions:** the two committed file blobs
were SHA-verified against a separately compiled local copy:
- header `2ac5d3201d463da3b0f3231b1adfe2b98a3488b0`;
- test `96d408646f661407b148b103beb6a6db43b45243`
(updated 8,192-iteration deterministic foreign-lease/overflow-offset sweep).

GCC C++20 O1 and O2 passed; Clang C++20 O2 passed; Clang
ASan+UBSan O1 passed. This was a standalone exact-source test, not a full
Forge CMake/CTest or compiler-backed untrusted guest execution test.

### OresVM actor-memory lifecycle integration (2026-11-18)

The `OresVM` trusted host now allocates one independent **4 KiB** bounded
`TenantMemoryRegion` on actor registration, keyed by the existing
`IsolateHandle` and generation-fenced `ActorTicket`. Each physical
registration debits a process-root memory budget (default 16 MiB), with
atomic rollback on registration allocation failures. Checked host access
(`read_actor_memory` and `write_actor_memory`) validates both tenant
and actor authority, denies shutdown/restart use, and holds the root
registry lock through the bounded memory copy to prevent retirement races.
`retire_actor` zeroes and releases the actor's physical memory and returns
its budget. No extra actor-memory owner or second independent actor registry
is introduced.

The existing `oresforge_tenant_isolate_host_admission` CTest has tests for
wrong tenant/actor tickets, overrun, quiescence, restart and aggregate
allocation caps. The existing **real source-to-native scalar guest** CTest
(`oresforge_compiled_tenant_isolate_native`) also now exercises the same
host actor-memory lifecycle beside compiler-generated native scalar calls.
This is not direct guest-language load/store lowering: **the emitted Forge
machine code still does not receive a memory pointer or call the checked
host memory API**, and can never be admitted as untrusted code on this basis.

**Verification (exact committed source):** The checked-in `OresVM`
header and `isolate_host_test.cpp` were materialized locally with exact Git
blob identity: `399d8245b3ef324f8a9137e708b7045071e03bc4` and
`4a33df4c5b33274cec98429bc9c1ad45e7df0478`.
They compiled and ran successfully under GCC 14.2 O2, Clang 17 O2, and
Clang 17 O1 ASan+UBSan (`-Wall -Wextra -Wpedantic -Werror -UNDEBUG`).
The original `TenantMemoryRegion` header/test also passed those profiles
at blobs `2ac5d3201d463da3b0f3231b1adfe2b98a3488b0` and
`4331f4c5c403e99a18695bd9689184faf1448d1c`.
This proves exactly these **standalone host tests**, not the entire CMake
suite or real compiler-to-native guest isolation. The new
`native_isolate_scalar_guest.cpp` compiled-guest CTest is still NOT_EXECUTED
on the exact branch, as are Linux x86-64/ARM64 package/compiler parity gates.
Source-org GitHub Actions still fail before executing steps.

### Remaining physical enforcement gap

The host-only `TenantMemoryRegion` is not yet connected to compiler-emitted
Forge loads/stores or `OresVM` actor runtime allocation ownership. Before
enabling `ProtectedNative`, all guest memory references, stack spills,
indirect calls and native service transitions must be checked in generated
code against the tenant cage and capability manifest. An arbitrary native
pointer can bypass this C++ API. True independently terminated tenant faults
and a real two-source-guest survivor test remain mandatory.

## Implementation stages and release gates

1. Design/pin guest ABI: guest entry status+result, pointer/offset authority, allocated region ID, generation, ownership permissions and compiler-verified safe indirect call targets.
2. Lower verified Forge IR to explicit checked memory operations and quota-aware safepoints; reject unsafe/native features and uninstrumentable paths.
3. Implement per-tenant cage allocator and actor arena integration, versioned handles, immutable AOT sharing and fresh globals; preserve the existing mailbox/actor ownership mechanisms.
4. Prove two **compiled Forge source** tenants in one OS process, with one malicious bounds guest and one healthy neighbor running concurrently. Verify no cross-tenant read/write or raw pointer extraction.
5. Test forged/stale offset handles, integer overflow, use-after-destroy, host I/O capabilities, tenant restart, actor mailbox drain, CPU starvation, stack/heap exhaustion and guest fault termination.
6. Run x86-64/ARM64 native executable and sanitizer suites under GCC/Clang O1/O2, with exact Git source/artifact provenance; require nonempty CI steps, independent security review and no host crash.
7. Only after (1)-(6) turn on `ProtectedNative` and update the conformance registry from `NOT_EXECUTED` to actual evidence. Never infer full AttoLang guest actor parity from host-only scalar tests.

**Non-goals:** no Wasmtime in Forge, no DLL-based tenant sandbox, no assumption that `pthread` separation protects process memory, no Fork/OS-process requirement for basic TenantVM execution, and no claim that disabling guest FFI alone prevents accidental native access across tenants.

## Cross-backend policy

- `.llvm`: retain existing custom native TenantVM; Forge learns from its checked native memory architecture but remains independent.
- `.forge`: native code with enforceable checked guest memory confinement (this decision).
- `.net`: Wasmtime sandbox for validated untrusted Wasm; preserve strict .NET Native AOT host.
- `.wasm`: produce validated guests; provide a canonical guest ABI and conformance fixtures.

References: `oreslang-source.llvm/docs/ORESVM_VS_ISOLATEVM.md`; `oreslang-source.llvm/docs/TENANT_VM_NAMING_V1.md`; `docs/VM_ISOLATE_CONTRACT.md`.

## Follow-up: deterministic tenant memory erasure and monotonic lease identities

The tenant memory region's previous destructor used ordinary `std::fill`,
which an optimizing compiler may remove after the buffer's last observed use.
`runtime/secure_erase.hpp` now performs volatile byte stores with compiler
signal fences; `TenantMemoryRegion::retire()` and `State::~State()` both
invoke it. This is defense in depth for host buffer reclamation, not
guest sandboxing or guaranteed erasure of all caches, allocator metadata
or copies retained elsewhere.

The previous `fetch_add` lease-generation allocator could wrap after
`UINT64_MAX`, eventually reissuing an old generation. The current
compare-exchange allocator refuses issuance at exhaustion *without
modifying the counter*. This preserves monotonic non-reused generations
for the lifetime of the host process even on allocation failure.

**Executed local exact-blob tests:** the standalone secure-erasure helper
and test match Git blobs
`f1c28781e3ffc42c0fbc833fbb6317c6aa82c087` and
`df544ea743a8ab2478dc8ac407e25e3814639847`.
The updated tenant memory-region header matches
`856bf5ee987e25b91df5c5245792db0554064674`; its regression test is
`4331f4c5c403e99a18695bd9689184faf1448d1c`.
Both targeted test executables passed GCC/Clang C++20 at -O1 and -O2
with strict warnings, and Clang ASan+UBSan at -O1.
These are **targeted host tests only**, not full CMake/CTest, untrusted
native compiler guest execution, or independent CI attestation.

## Audit follow-up: native write-source alias denial (2026-11-18)

The checked host `TenantMemoryRegion::write` API previously copied an
external `std::span` into its private bytes via `std::copy` after validating
the *destination* bounds, but without checking that the source span overlaps
the same private memory region. That is undefined behavior for a copy
into an overlapping range, and an internal pointer leak/caller bug could
silently corrupt a tenant's private bytes. **It is not itself evidence
that untrusted compiled guest code can access the private region.**

Forge now checks address interval overflow and overlap against **the entire
private byte region**, rejecting source aliasing before the copy.
The helper deliberately treats native pointer interval overflow or a null
pointer with nonzero size as unsafe. The check is for trusted native
x86-64/ARM64 host code using a usable `uintptr_t`; it cannot validate a
generally invalid external pointer or turn a C++ process into a sandbox.
The earlier private-region owner/lease/offset checks remain intact.

- New checked interval helper:
  `runtime/checked_memory_overlap.hpp`, Git blob
  `8730b44d41797774b72319887ee2d1ae5938c833`.
- New boundary/overlap/wrap negative cases:
  `verification/checked_memory_overlap_test.cpp`, Git blob
  `59494247ac74a27f8654083dbfbe83f9b12df86b`.
- Updated native arena:
  `runtime/tenant_memory_region.hpp`, Git blob
  `6577d894f8bbca56cec7531486046e5255370c8c`.
- Existing 8,192-case adversarial native arena test:
  `verification/tenant_memory_region_test.cpp`, Git blob
  `96d408646f661407b148b103beb6a6db43b45243`.

The **exact matching committed source** was locally compiled and executed
with GCC and Clang C++20 at O1/O2 and Clang O1 ASan+UBSan; both targeted
test programs passed all five configurations. A separate
targeted CMake/CTest harness also passed **2/2** tests for GCC and **2/2**
for Clang. These are scoped host tests; the complete Forge source
CMake/CTest, compiler-emitted guest isolation, and source-org Actions were
**not** executed successfully. All source-org Actions checks inspected
continue to have zero executed job steps; the private-source alternative
CI approval remains pending. `ProtectedNative` remains disabled.
