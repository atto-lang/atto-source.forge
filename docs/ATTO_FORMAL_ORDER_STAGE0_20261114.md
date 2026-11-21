# AttoLang name-first / type-first formal order (Forge stage-0)

**Status: authored native-source compiler change; actual build/test evidence missing.**

The independent Forge stage-0 source interpreter and native-call parser
now accept required scalar parameters as either `int x` or `x: int`.
Both produce the existing `int64` parameter slot and must execute
the same `.atto` programs when the real Forge compiler tests run.

`x?: int` is an omitted-argument ABI requirement, **not** a required Option.
`x: int?` means a **required** `Option<int>`, currently unsupported in the
scalar call ABI. Both spellings are rejected with separate compiler diagnostics
before IR interpretation or machine-code emission. Defaults without
name-level `?` are rejected; with a name-level `?`, omissions remain
unsupported. There is no fake null/zero default or implicit `Some` wrapping.

The new CTest invokes the real `oresforge` binary in both `run` and
`run-calls` modes for 6 scalar positive and 16 negative source cases.
Those are **authored tests, not confirmed stepful executions**. This does not
establish optional-argument or tagged Option guest support, Native AOT
certification, or Java/LLVM/.NET semantic parity.

Normative shared cases:
https://github.com/ores-truffle-oreslang/attolang-language-spec.md/pull/51
