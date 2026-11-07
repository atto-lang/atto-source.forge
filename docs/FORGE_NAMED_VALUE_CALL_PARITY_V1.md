# Forge named-value scalar calls: bounded parity slice

This is a **stacked change on Forge #171**, not a claim of full parity with Java/GraalVM or LLVM. It adds exact-case, named value binding for *statically resolved, pure, signed-i64 functions* in Forge's **inlined source frontend**.

- Grammar: `pair(b: 2, a: 4)`, and a positional prefix followed by named values, e.g. `pair(4, b: 2)`. A positional value **after** a named value is rejected.
- Value arity still selects a function; names only bind the corresponding declared formal. No named-based overloads, reflection, defaults, spreads or dynamic calls.
- Source arguments lower to SSA instructions **in lexical order**, then results are bound to their formal slots. This preserves eager evaluation even when formal order is different.
- Unknown/duplicate/colliding labels, missing/extra actuals and invalid unused helper bodies are rejected before any output. Labels are case-sensitive.
- The normal `oresforge run` / `ir` / `emit-asm` pipeline is covered. The existing Linux native-assembly smoke runs these source cases through the real parser, emitter, linker and native executable. Its emitted `main` is **constant-folded**, so native compilation is narrower than general named-call runtime ABI support.
- `run-calls` and `emit-call-asm` use a **separate** stage-0 compiler; this PR does not add named arguments to that true native-call ABI. Nor does it implement named generics, methods, actors, imports, closures or suspension.

## Acceptance

`cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build --output-on-failure`

In particular: `oresforge_named_value_call_guest_42` (actual Forge parser+IR+interpreter) and `oresforge_native_asm_executable` (Linux supported arch, native assembly execution plus all positive/negative named cases). The negative harness requires nonzero status, **empty stdout**, and the expected diagnostic in each of `run`, `ir`, `emit-asm`.

**Evidence rule:** authored source and tests are not executed receipts. Require nonzero CI job steps, exact PR head/tree, native CMake/CTest and independently reviewed guest traces before changing parity status. Do not mirror this private source to a different organization without independent authorization.
