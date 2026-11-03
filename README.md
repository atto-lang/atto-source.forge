# AttoLang Forge (legacy OresForge)

An **independent, experimental C++20 compiler and execution stack** for **AttoLang** (formerly Oreslang), distinct from GraalVM/Truffle and LLVM. This is a working **bootstrap prototype**, not a production AttoLang compiler.

Stage-0 extension admission is not import parity: `.atto` names pass through the existing bounded single-file parser, while imports, cross-backend guest parity, production AOT, and zero-GC guarantees remain unproven. See [Atto source admission](docs/ATTO_SOURCE_ADMISSION.md).

**AttoLang source files use the canonical `.atto` extension.** Legacy `oresforge` CLI/ABI names and `oreslang-*` repository names are retained temporarily for tooling compatibility. Existing `.ores` fixture archives are legacy conformance artifacts, not the format for new examples.

## Getting started

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/oresforge run examples/hello.atto       # 14
./build/oresforge ir examples/hello.atto
./build/oresforge emit-asm x86_64 examples/hello.atto
./build/oresforge emit-asm arm64 examples/hello.atto
./build/oresforge run examples/hello.atto  # expected: 14
~~~

Requires CMake >= 3.20, a C++20-capable GCC/Clang, and a POSIX toolchain for native smoke testing. CI builds this experimental bootstrap and executes its CTest suite. The former `verification/smoke.sh` reference was removed because that file does not exist.

## Self-hosting and native execution evidence

**OresForge is not yet self-hosting.** Its current compiler is C++20, not
Oreslang source compiled by Oreslang. See [the stage-by-stage self-hosting
acceptance plan](docs/SELF_HOSTING.md). The Linux x86-64/ARM64 CTest
`oresforge_native_asm_executable` now assembles, links and **runs** the
compiler's constant-return assembly and compares results with the checked
source interpreter, including fail-closed negative cases. This is native
execution of a narrow scalar program, **not** general native code generation
or stage-1 compiler bootstrap.

## Opt-in runtime-argument experiment (new)

This experimental stage-0 profile accepts a `main` with **zero to four typed
`int` parameters**, while helper functions are still statically inlined.
The standard `run`, `ir`, and `emit-asm` commands retain their stricter
zero-argument, compile-time checked behavior. New commands:

~~~sh
# Source example: fnc main(x:int,y:int):int{return x*3+y;}
./build/oresforge run-runtime examples/runtime-args.atto 7 8       # 29
./build/oresforge ir-runtime examples/runtime-args.atto             # param.i64 instructions
./build/oresforge emit-runtime-asm x86_64 examples/runtime-args.atto > entry.s
~~~

`emit-runtime-asm` generates **actual native integer operations on runtime
ABI arguments** (System V x86-64 or AAPCS64 ARM64) rather than folding them
to one constant return. Checked add/sub/multiply overflow emits a trapping
instruction; the checked interpreter instead reports an exception. The
native frame and IR are explicitly bounded. Source `/` still requires
float lowering and remains rejected, as do recursion, mutable locals,
conditionals, loops, native inter-function call frames, and self-hosting.
This is not a generic cross-platform ABI or a C-callable error-return API.

The Linux CTest `oresforge_runtime_argument_native_executable` links
emitted assembly to an independently compiled C driver, calls it with
different **runtime inputs** and compares results with the checked
interpreter. Separate negative tests require native overflow to terminate
via SIGILL/SIGTRAP, not silently wrap or return a bogus value.

## Opt-in real native function calls and lazy `if`

The **experimental** `run-calls` and `emit-call-asm` commands compile
a separate, verified callable source subset. Unlike `run-runtime`, helper
functions are **not statically inlined**: x86-64 code emits actual
`call` and `ret` instructions, with real stack frames.

~~~atto
fnc factorial(n:int):int {
  return if n <= 1 then 1 else n * factorial(n-1);
}
fnc main(n:int):int { return factorial(n); }
~~~

~~~sh
./build/oresforge run-calls examples/factorial.atto 10         # 3628800
./build/oresforge emit-call-asm x86_64 examples/factorial.atto > factorial.s
~~~

Supported here: signed 64-bit integer literals and parameters; arithmetic
`+`, `-`, `*`; comparisons `<`, `<=`, `>`, `>=`, `==`, `!=`;
lazy `if condition then expr else expr`; typed function calls, forward
references, direct and mutual recursion with up to four `int` parameters.
All calls use System V x86-64 integer registers with an internal hidden
recursion-depth argument, limited to 128 frames. Bounded per-function ASTs
also cap stack-frame sizes, and the checked interpreter has an evaluation
step budget. Arithmetic overflow and excessive recursion trap in generated
native code and raise explicit errors in the interpreter.

This is an intentionally separate **stage-0 C++ compiler path**; it does not
yet provide the general Oreslang ABI, mutable local bindings, statements,
loops, closures, floating-point division, actors, native ARM64 call lowering,
or compiler self-hosting. Only Linux x86-64 has executed native call and
branch evidence. Do not treat this syntax subset as complete Oreslang syntax.

## Immutable-by-default i64 bindings (stage-0 slice)

The default scalar and opt-in runtime-argument frontends now accept
initialized, type-first `int name = expression;`, explicit
`const int name = expression;`, and inferred-i64 `const name = expression;`
before the single terminal `return`.
Both lower to immutable SSA values; a later declaration cannot shadow a
parameter or another local, and self/forward references are rejected.
The initializer may call a pure helper or depend on an earlier binding.
The bounds are 32 locals per function and the existing 128-level AST limit.
No mutable `let`, aggregate/heap values, `bool`/`string` locals, blocks,
loops, general lexical scopes, or implicit conversions are admitted.
The opt-in `run-calls` / `emit-call-asm` compiler is extended by this PR as a separate bounded, independently verified i64 implementation; it does not imply general typed-local parity.

The C++ unit suite exercises the actual parser and verified interpreter,
including the 32-binding budget and eager failure of unused initializers.
The separate `run-calls` / `emit-call-asm` x86-64 compiler also admits bounded
immutable checked-i64 locals (typed and inferred `const`) across real helper
calls, parameter-dependent expressions and lazy branches. Its owned postfix
AST now verifies lexical scope independently before interpreting or emitting
stack-slot loads. This is an opt-in native-call scalar subset, not full
AttoLang class/actor or general typed-local parity. The source-org CI job
admission remains unverified until executed compiler/CTest receipts exist.

The separate native-call lexer now treats CR and CRLF as end-of-line for
`//` comments so subsequent declarations cannot silently disappear.

Existing native-assembly and runtime-argument CTests also cover
compiled i64 programs with these bindings. That is a small stage-0
source-level capability, **not** Java/Graal language parity.

## Directories

| Directory | Responsibility | State |
| --- | --- | --- |
| compiler/ | Parser, SSA-like IR, verifier, checked interpreter, CLI | Small runnable arithmetic subset |
| codegen/ | x86-64 and AArch64 assembly | Constant-return programs only |
| runtime/ | OresVM-facing infrastructure | Thread-safe bounded **byte mailbox prototype** only |
| jit/ | Tiered execution | Policy only; interpreter is always used |
| verification/ | Tests, fuzzing, cross-backend and proof plan | Bootstrap tests; formal work planned |

### Current supported grammar

A zero-argument \`fnc main() : int { [up to 32 immutable int bindings] return EXPR; }\`, with optional \`define\` and \`pub\` modifiers. Expressions include i64 decimal literals, parentheses, unary +/- and binary + - *. Source division is rejected because reference Oreslang `int / int` returns `float`; float lowering is not implemented. Internal checked-i64 IR division remains independently tested. Arithmetic is checked for overflow. The IR is single-block and every operand must refer to an earlier definition. There are still no runtime function-call instructions, stack frames, conditional branches, or loops. Tokenization is deterministically ASCII for the admitted grammar regardless of the host locale. Valid UTF-8 in comments is accepted, but non-ASCII identifiers or whitespace are rejected. The native CTest now executes assembled results for multi-function constant-folded sources and rejects invalid helper programs consistently across CLI modes. All emitted machine-code functions simply return the **compile-time computed constant** in \`oresforge_entry()\`; general instruction selection is not implemented.

Actors as Oreslang language constructs, ownership/borrows, concurrent scheduling, async/await, hot reload, true native JIT, native object generation, and OresVM integration **are not implemented**. Actor references now carry an **opaque, unforgeable-from-numeric-fields local
incarnation authority**, rotating on restart. This only prevents one local
mailbox from accepting a different mailbox's numerically matching handle;
it is **not** a cryptographically authenticated or network-serializable
capability, and it does not implement guest actor execution.

See [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), and [verification](verification/README.md).

License is not yet selected; maintainers should add an explicit license before external contributions.

See [value-boundary audit and admission tests](docs/VALUE_BOUNDARIES.md) for
aggregate, call, string, conditional and async feature limits.
