# Elm-inspired composition: OresForge native source admission

Stage: **draft fail-closed contract**. OresForge's current Oreslang compiler
supports a small checked arithmetic `main` subset, not source-level
`Option`, `Result`, `Future`, `Stream`, `Observable`, channels,
closures, or actors.

The executable `elm_composition_admission.py` CTest checks a positive
Oreslang integer source control and sixteen composition/transport/failure source rejection
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

## Postfix question mark and typed exceptional control (2026-10-15 hardening)

The source admission corpus now covers sixteen cases including postfix
`Result?`, postfix `Option?`, disallowed implicit `Result?`-to-throw conversion inside
`try/catch`, `raise` bypassing local catch, and `panic` escalation.
They remain **negative acceptance gates**: a diagnostic/no output is not
executable evidence of return, throw, recover or supervisor correctness.

See common-source PR #36 (postfix `?` routing contract) together with
common-source #35 (callable `throws`/`raises`/`panics` proposal).
`Result<T,E>?` in a `Result<U,E>` callable returns `Err`, even when
`throws E` is also declared. In a non-Result-returning callable, `Result<T,E>?` is rejected even
when `throws E` is declared: postfix `?` never converts `Err` to `throw`.
Any such conversion must use a separate, explicitly checked operation
(e.g. proposed `or_throw()`), not `?`. `Option<T>?` can return `None` only from an
`Option<U>` callable; it cannot fabricate an exception. Ordinary
`try/catch` handles real `throw` only, not `Err` returns.
`raise`, `panic`, cancellation, and fatal controls are not caught by
`?` or converted to stream completion.

Release requires native guest-source lowering, typed effect and ownership
verification, exactly-once LIFO cleanup across early return and exceptional
unwind, cancellation/continuation and supervisor integration, plus verified
matching traces against Java/Graal. Do not promote the current negative tests
to positive execution tests without those proofs.
