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
- test `4331f4c5c403e99a18695bd9689184faf1448d1c`.

GCC C++20 O1 and O2 passed; Clang C++20 O2 passed; Clang
ASan+UBSan O1 passed. This was a standalone exact-source test, not a full
Forge CMake/CTest or compiler-backed untrusted guest execution test.

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
