# Forge source exports v1 — reconciled with cooperation v1

This is the compiler half of the **combined** Forge contradiction-reconciliation PR.
The cooperation half is documented in TENANT_COOPERATION_BUDGET_V1.md.

## Resolved contradiction

Legacy Forge [#105](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/105)
made duplicate identical bindings idempotent; legacy
[#278](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/278)
rejected every duplicated name in direct IR. The authoritative
[common-source #156](https://github.com/ores-truffle-oreslang/oreslang-common-source/pull/156)
revision 'explicit-exports-v1.2026-10-26.5' makes exact duplicate bindings,
including redundant implicit pub, **idempotent**. Atto spec
[atto-spec #9](https://github.com/atto-lang/atto-spec/pull/9) specifies
order-independent export pub and package scopes; atto-spec #68 covers
the broader namespace/module future target.

## Stage-0 Forge compiler implementation

- Resolve exports **after** all local scalar function declarations, allowing
  forward/trailing, grouped, alias, typed and untyped export spelling.
- Supported local declaration family: fnc with i64-only signatures and no
  overloads. Supported scopes: file, pkg, public, including pub[file]
  and pub[pkg] local visibility declarations.
- Equivalent bindings collapse to a single binding; implicit pub takes
  precedence as canonical origin. Alias/name collision, scope conflict,
  explicit-private disclosure, unknown target, mismatched kind, malformed
  group and invalid export syntax fail closed with EXPORT_* diagnostics.
- No export can bypass source parser validation to change emitted scalar IR
  instructions or native x86-64/AArch64 assembly symbols. The internal
  Program::source_export_bindings array is *descriptive* source-interface
  metadata, never a security capability or a native loader export list.
- Directly constructed IR is checked for malformed and duplicated metadata.
  Real source declarations are still the authority for semantic resolution.

## Explicitly unsupported

Module/package import linking, multi-file namespaces, classes, actors,
Symbols, structural types, generics, routine overload families, recursive
type leakage, typed signatures beyond i64 and full semantic ABI hashing
remain unavailable in stage-0 Forge. Re-export syntax and implicit native
symbol export remain forbidden. A host-only metadata table is not a claim
of end-to-end package visibility or linker parity.

## Verification gate

export_semantics_v1_test compiles and exercises the repository's real
compiler/frontend.cpp, compiler/ir.cpp and codegen/native.cpp, not
a separate source-free parser model. It runs positive/negative tests and
compares IR plus native x86-64/AArch64 assembly with baseline to ensure
source export aliases never affect runtime code generation.

tenant_cooperation_budget_v1_test separately exercises the tenant-owned
host budget, phase persistence, short-dispatch evasion and replay defenses.

Both tests are registered in the root CMakeLists.txt for GCC/Clang
sanitizer builds. Record exact-head *executed* CTest and compiled guests
separately. Empty steps and no workflow runs are **NOT_EXECUTED**,
never a pass. Do not merge or claim full compiler/guest conformance until
all applicable material gates and independent reviews are satisfied.
