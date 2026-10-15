# Elm-inspired composition: OresForge native source admission

Stage: **draft fail-closed contract**. OresForge's current Oreslang compiler
supports a small checked arithmetic `main` subset, not source-level
`Option`, `Result`, `Future`, `Stream`, `Observable`, channels,
closures, or actors.

The executable `elm_composition_admission.py` CTest checks a positive
Oreslang integer source control and six composition/transport source rejection
cases against the **real oresforge compiler**. A rejected source must exit
nonzero with an `oresforge:` diagnostic and zero IR. This is NOT an
implementation of a typed reactive runtime or evidence that a C++ helper has
source-language semantics.

All future Oreslang fnc and method names must be snake_case:
`flat_map`, `and_then`, `map_err`, `filter_map`, `on_success`.
Follow common-source PR #36: `Future`, `Stream`, `Observable` and
`Channel` are distinct and conversions explicitly carry backpressure,
subscription ownership, terminal states, cancellation and failure. A future
backend stage needs typed HIR/IR, exhaustiveness, owned sum layouts, no
double drops and verified callback effects before positive source admission.

No universal `Monad<F<_>>` or silent actor/Channel bridging.
