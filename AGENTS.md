# AttoLang source-first implementation guidance

These instructions apply to this repository's code changes, examples, fixtures, and AI coding agents. They supplement, and never override, repository security rules or review requirements.

## Source placement

- New AttoLang guest source, standard-library algorithms, sample programs, and guest-language regression fixtures use **`.atto`**. The `.ores` suffix is for legacy compatibility or explicit migration tests only; do not create new `.ores` features.
- Implement portable guest-visible behavior in AttoLang wherever it is expressible. Add only the smallest necessary host/compiler primitive when the language cannot safely implement that behavior itself. Do not disguise host-language emulation as actual AttoLang guest execution.
- Keep functions and methods in `snake_case`. Preserve actor ownership, sealed state, capability boundaries, deterministic errors, and full-AOT compatibility.
- If a necessary host-language implementation remains, document why guest code is not yet possible, which capability it supplies, and how it can be replaced. Do not mass-rename host compiler/runtime files to `.atto`.

## Evidence and security

- Separate source-only fixtures, host-model tests, compiler parse/typecheck, executable guest tests, and native AOT evidence. A zero-step workflow, synthetic receipt, or host-only test means **NOT_EXECUTED** for guest parity.
- Record exact fixture/compiler Git blobs and source heads when making parity claims; do not mark another backend as passing without its own executed result.
- Do not copy private Git objects to another organization simply because its runner has capacity. Respect source-side authorization, independent security review, approved private destination, and protected-branch requirements.

## Forge-specific responsibilities

- Keep Forge's compiler/JIT/AOT, backend IR, memory safety and native runtime in the host language where necessary; write portable guest APIs, algorithms and demos in `.atto`.
- Test `.atto` with Forge's actual compiler and executed interpreter/native binaries; stage-0 constant-folded scalars do not prove general code generation, actors, imports or backend parity.
- Name independent Forge and Cranelift evidence separately. Do not claim one backend's test results certify another.

## Strict guest AOT and zero-GC controlled-dynamism requirement

Review new code against [organization policy #38](https://github.com/ores-truffle-oreslang/.github/pull/38), [language-spec #18](https://github.com/ores-truffle-oreslang/attolang-language-spec.md/pull/18) and [this backend's policy](docs/AOT_ZERO_GC_DYNAMISM.md). **Sealed AOT rejects runtime eval, runtime class/method generation, unrestricted reflection, unknown proxy shapes and silent JIT/interpreter fallback before executable IR.** Statically generate proxies, virtual dispatch registries, generic/macro specializations, closures and async state machines. Require explicit ownership, `rt take`/`rt copy`/`rt borrow`/`rt share`, deterministic cleanup, bounded actor/task heaps, cycle handling, cancellation and generation fencing. No closure/function transport by channel/pub-sub.

Every PR must declare AOT profile and GC scope, justify host/compiler code vs `.atto`, record positive **and negative** `.atto` tests and exact-head executed guest receipts. Do not claim whole-process zero-GC from native source or a host-only test. Zero-step jobs are **NOT_EXECUTED**. Keep the no-Python/Rust-only tooling policy, security approvals and existing source-first rules intact.
