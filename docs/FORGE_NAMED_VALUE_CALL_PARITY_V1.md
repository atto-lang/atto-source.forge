# Forge named-value scalar calls: bounded parity slice

This is a **stacked change on Forge #171**, not a claim of full parity with Java/GraalVM or LLVM. It adds exact-case, named value binding for *statically resolved, pure, signed-i64 functions* in Forge's **inlined source frontend**.

- Grammar: `pair(b: 2, a: 4)`, and a positional prefix followed by named values, e.g. `pair(4, b: 2)`. A positional value **after** a named value is rejected.
- Value arity still selects a function; names only bind the corresponding declared formal. No named-based overloads, reflection, defaults, spreads or dynamic calls.
- Source arguments lower to SSA instructions **in lexical order**, then results are bound to their formal slots. This preserves eager evaluation even when formal order is different.
- Unknown/duplicate/colliding labels, missing/extra actuals and invalid unused helper bodies are rejected before any output. Labels are case-sensitive.
- The normal `oresforge run` / `ir` / `emit-asm` pipeline is covered. The existing Linux native-assembly smoke runs these source cases through the real parser, emitter, linker and native executable. Its emitted `main` is **constant-folded**, so native compilation is narrower than general named-call runtime ABI support.
- `run-calls` and `emit-call-asm` now support **bounded signature-checked named i64 arguments**, including forward references, up to four SysV x86-64 register arguments, mixed positional prefix and exact-case labels. The native-call frontend and checked host interpreter keep lexical evaluation order. The native emitter maps source operands to formal register slots after evaluation; it does not constant-fold the entry. Neither compiler implements named generics, methods, actors, imports, closures or suspension.

## Acceptance

`cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build --output-on-failure`

In particular: `oresforge_named_value_call_guest_42` (scalar parser/IR/interpreter), `oresforge_native_asm_executable` (constant-folded scalar emitter) and `oresforge_real_native_recursive_calls` (Linux x86-64 actual native call/return and formal-register binding; 24 permutation programs plus regression cases). The negative harness requires nonzero status, **empty stdout**, and the expected diagnostic in each of `run`, `ir`, `emit-asm`.

**Evidence rule:** authored source and tests are not executed receipts. Require nonzero CI job steps, exact PR head/tree, native CMake/CTest and independently reviewed guest traces before changing parity status. Do not mirror this private source to a different organization without independent authorization.

## Reconciled integration chain (2026-11-07)

- Parent [Forge #211](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/211) preserves current main's fail-closed security/tenant gates while rebasing scalar PR #171.
- This follow-up carries scalar named-call PR #204 into the reconciled tree and extends the real native call ABI without mutating either original donor branch.
- Tests added to `verification/native_call_executable.py` require actual `emit-call-asm` output linked with a C11 driver and executed under Linux x86-64, not just source/IR acceptance.
- The mutated public-Module tests deliberately alter call slot metadata and require that independent `verify` and `emit_asm` reject it.

Do not equate passing source-readback or an empty-step Actions failure with successful guest code execution.

## Lexer hardening audit — November 7, 2026

Native-call source admission now accepts bounded nested block comments (maximum depth 128), consistent with the scalar lexer; declaration-like and removed-operator text inside comments must not be admitted as code. Unterminated blocks and depth 129 fail before any IR/assembly output. Removed `%`, `^` and `^^` operators now produce explicit canonical diagnostic substrings in `run-calls` and `emit-call-asm`. Regression cases execute through the real compiler path and source/IR mutation checks. This is **authored test coverage**, not stepful CI evidence.
