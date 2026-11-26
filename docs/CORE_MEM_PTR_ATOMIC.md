# `core/mem`, `core/ptr`, and `core/atomic` (Forge-owned Atto source)

These are Forge-local `.atto` candidate implementations, in
`stdlib/core/mem.atto`, `stdlib/core/ptr.atto`, and
`stdlib/core/atomic.atto`. They are independently maintained, not imported
from atto-common-source or a Java runtime.

- `core/mem`: power-of-two alignment, stride/size overflow preflights,
  checked array byte limits and byte span bounds.
- `core/ptr`: region-relative arithmetic, alignment and indexed bounds.
  It **does not** create or dereference a Pointer<T> or authorize memory access.
- `core/atomic`: relaxed/acquire/release/acq_rel/seq_cst memory-order
  admission with seq_cst default, including CAS failure-order validation.
  It **does not** implement Atomic<T> load/store, CAS, RMW or hardware fences.

No host interop, OS or allocator is required by the helpers. Actual region
provenance, lease/generation fencing, actor isolation, lifetime/drop correctness
and atomicity must be compiler/runtime-enforced with real guest evidence.
`node --test tests/core-memory-policy-source.test.mjs` is a restricted
source-only 63-case policy probe, not real Forge guest execution or native AOT.
All runtime/guest/parallelism/AOT claims remain NOT_EXECUTED.

Coordinate existing native work in
[LLVM #801](https://github.com/atto-lang/atto-source.llvm/pull/801),
[Rust #419](https://github.com/atto-lang/atto-source.rs/pull/419),
[atomic spec #175](https://github.com/atto-lang/atto-spec/pull/175), and
conflicting pointer specs [#18](https://github.com/atto-lang/atto-spec/pull/18)
and [#154](https://github.com/atto-lang/atto-spec/pull/154).
