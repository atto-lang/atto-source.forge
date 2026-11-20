# Forge restricted scalar grammar: caret/XOR and begin/end

Status: scoped source compiler support (not full Java actor/method parity). Canonical operator decision: [atto-spec #143](https://github.com/atto-lang/atto-spec/pull/143). Java return-first model: [legacy Java #3181](https://github.com/ores-truffle-oreslang/oreslang-source.java/pull/3181); Forge legacy design: [#295](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/295).

- **Statement-leading `^`** is exactly `return` for the one-result, pure, zero-argument integer `main` supported here. Single `^` is **never** infix XOR or exponentiation.
- **Contiguous `^^`** is signed-i64 bitwise XOR. It retains the prior XOR precedence tier, between `|` and `&`. A space/comment between two carets does **not** form XOR; `^^^` rejects.
- Braced bodies (`{ ^ 42; }`) and **standalone next-line** `begin` / `end`, `end.`, `end;` are supported for restricted scalar entrypoints. Return-first `int <- main()` and `int• main()` (optional `fnc`) reuse the scalar AST. The exact U+2022 callable marker is the only admitted non-ASCII source syntax in this lexer.
- Malformed UTF-8, unapproved Unicode, misplaced `^`, unsupported declarations, booleans, non-scalar calls, and multiple return statements reject without code generation. `Math.pow`, actor/method execution, Boolean XOR, imports, continuations, and async tasks are unsupported.
- Compiled native guest tests: `tests/scalar_guest.cmake` and `tests/scalar_conformance.cmake`. Parser, lexer, diagnostics/no-clobber: `tests/scalar_parser.cpp`, `tests/scalar_lexer.cpp`, and `tests/scalar_cli_diagnostics.cmake`.
- Before a parity claim require exact-head GCC/Clang Debug/Release CMake+CTest, ASan/UBSan, emitted compiled guest execution, source/tree hashes, nonzero runner steps and independent review. Authored tests alone are NOT_EXECUTED evidence.

The legacy `oresforge` bootstrap frontend and general hostile TenantVM remain separate; this increment is not a general native compiler certificate.
