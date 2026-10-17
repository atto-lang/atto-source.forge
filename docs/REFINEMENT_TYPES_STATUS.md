# Refinement types: Forge parity status

**Status (2026-10-17): native rejection parity only. No checked refinement parsing/lowering/runtime enforcement is claimed.**

Canonical contract: https://github.com/ores-truffle-oreslang/oreslang-common-source/blob/design/refinement-types-three-syntaxes-20261017/docs/REFINEMENT_TYPES.md

Forge's current compiler is a deliberately narrow scalar bootstrap. The real `oresforge` executable must successfully compile an ordinary scalar control and reject every proposed refinement surface before emitting partial IR:

- `@Size`/constraint annotations
- `with` refinements
- `where` predicates
- nominal `define newtype`
- invalid `with ... or ...` constructs

The CTest `refinement_admission` invokes the built compiler through `verification/refinement_admission_gate.py`. A passing rejection gate means only that unsupported constraints are not silently erased; it is **not** refinement implementation parity.

Future completion requires normalized constraint IR, checked construction, Unicode code-point length, structured errors, Zorde descriptors, actor/serialization/FFI boundary enforcement and native runtime evidence.

## Inclusive comparison spelling

Canonical textual inclusive bounds are `gte` and `lte`; symbolic `>=` and `<=` are equivalent source spellings. Legacy `ge` and `le` are deliberately invalid and must never be accepted as aliases. Until this backend implements checked refinement lowering, both canonical/symbolic valid refinements and legacy-invalid refinements remain fail-closed admission cases rather than erased base types.
