# OresForge

An **independent, experimental C++20 compiler and execution stack** for Oreslang, distinct from GraalVM/Truffle and LLVM. This is a working **bootstrap prototype**, not a production Oreslang compiler.

## Allocation optimization and JIT CLI admission (experimental)

The public flag names are `--opt-escape=off|safe|verify|speculative`,
`--jit=off|on`, and `--opt-escape-report`. A `.cli-flags.toml` maps
these through **flags-2-env** to `ORES_OPT_ESCAPE`, `ORES_JIT`, and
`ORES_OPT_ESCAPE_REPORT`. This bootstrap also accepts direct spellings:

~~~sh
./build/oresforge --opt-escape=off --jit=off run examples/hello.ores
./build/oresforge run examples/hello.ores --opt-escape=off
~~~

**General Forge compilation still supports `off` only.** `safe` is
accepted exclusively for the new opt-in `emit-alloc-asm x86_64` closed-world
source profile described below. `verify`, `speculative`, `--jit=on`, and
enabled allocation reports remain rejected *before* source or IR publication. This is intentional: Forge's tiered
JIT module currently requests tiers but always interprets, and the scalar
compiler has no guest heap-object allocation IR to optimize. The flags are
not proof of escape analysis, native JIT, stack promotion, or backend parity.
The parser rejects invalid/duplicate flags and values; explicit direct
CLI values override the corresponding environment mapping.

The `oresforge_optimization_cli_admission` CTest executes the real compiler
and verifies supported controls, environment mapping, unsupported modes,
negative diagnostics, and zero output for rejected configurations.

## Typed allocation MIR candidate analysis (experimental)

`compiler/escape_analysis.hpp` introduces a separate **typed model**, not
source compilation or an optimization pass in the shipping scalar compiler.
An allocation carries its nominal value kind, field count, bounded layout,
observable allocator effects, custom drop, and dynamic/atomic restrictions.
Every use is modeled explicitly; the model is fail-closed until a verified MIR
builder certifies a *complete* effect graph.

Candidate classification is `scalar-candidate`, `stack-candidate`, or
`preserve-allocation`. Only verified local borrows qualify for a bounded
stack candidate; returning, capturing, actor/channel sends, FFI, unknown
calls, suspension, identity checks, global writes, unsafe pointers, and
ownership transfer preserve the original allocation. Size/alignment budgets,
unknown types and custom drops also prevent promotion.

The CTest `oresforge_escape_candidate_analysis` runs positive and negative
typed-MIR checks. **No source-to-MIR lowering or machine-code allocation
elimination is implemented.** `--opt-escape=safe` remains explicitly
unsupported. A separate ownership/loan verifier, exception-edge coverage,
MIR lowering and executable optimized/off equivalence tests are prerequisites.

## First executable scalar-replacement experiment

The strict x86-64 `Point` fixture has two `int` fields, two runtime integer
inputs, one initialized local, and a checked addition of field reads. The
special parser consumes **every token** and rejects additional statements,
captures, actor messages, calls, returns of references, `await`, imports,
unsafe pointers, and unknown effects. Its typed MIR is constructed only by
this closed grammar; the producer's completeness condition is not supplied by
user source or accepted as an arbitrary generic MIR certificate.

~~~oreslang
define struct Point as
    int x
    int y
end
define pub fnc main(a:int,b:int):int {
    let p = Point { x: a, y: b };
    return p.x + p.y;
}
~~~

~~~sh
./build/oresforge ir-alloc point.ores
./build/oresforge --opt-escape=off emit-alloc-asm x86_64 point.ores > heap.s
./build/oresforge --opt-escape=safe emit-alloc-asm x86_64 point.ores > scalar.s
~~~

The baseline generates real `malloc(16)` / `free` calls around the owned
temporary, while the safe version applies the existing scalar-candidate
analyzer and emits native arithmetic without either call. Both use checked
overflow traps. `verification/native_alloc_executable.py` assembles and
**executes both versions** under a Linux x86-64 C driver that instruments
`malloc` and `free`, compares multiple runtime inputs, and checks overflow
signals. This is a limited stage-0 executable experiment, **not** complete
Oreslang object lowering, generic stack promotion, allocation-failure
equivalence under externally hooked allocators, or `safe` support for normal
`run`, `ir`, `run-calls`, or `emit-asm`.

There is no `--opt-escape=verify` evidence provider or speculative JIT.
Production enablement still requires general MIR ownership/alias/exception
proofs and cross-backend executed tests.

## Getting started

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/oresforge run examples/hello.ores       # 14
./build/oresforge ir examples/hello.ores
./build/oresforge emit-asm x86_64 examples/hello.ores
./build/oresforge emit-asm arm64 examples/hello.ores
./build/oresforge run examples/hello.ores  # expected: 14
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
./build/oresforge run-runtime example.ores 7 8       # 29
./build/oresforge ir-runtime example.ores             # param.i64 instructions
./build/oresforge emit-runtime-asm x86_64 example.ores > entry.s
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

~~~oreslang
fnc factorial(n:int):int {
  return if n <= 1 then 1 else n * factorial(n-1);
}
fnc main(n:int):int { return factorial(n); }
~~~

~~~sh
./build/oresforge run-calls factorial.ores 10         # 3628800
./build/oresforge emit-call-asm x86_64 factorial.ores > factorial.s
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

## Directories

| Directory | Responsibility | State |
| --- | --- | --- |
| compiler/ | Parser, SSA-like IR, verifier, checked interpreter, CLI | Small runnable arithmetic subset |
| codegen/ | x86-64 and AArch64 assembly | Constant-return programs only |
| runtime/ | OresVM-facing infrastructure | Thread-safe bounded **byte mailbox prototype** only |
| jit/ | Tiered execution | Policy only; interpreter is always used |
| verification/ | Tests, fuzzing, cross-backend and proof plan | Bootstrap tests; formal work planned |

### Current supported grammar

A single zero-argument \`fnc main() : int { return EXPR; }\`, with optional \`define\` and \`pub\` modifiers. Expressions include i64 decimal literals, parentheses, unary +/- and binary + - *. Source division is rejected because reference Oreslang `int / int` returns `float`; float lowering is not implemented. Internal checked-i64 IR division remains independently tested. Arithmetic is checked for overflow. The IR is single-block and every operand must refer to an earlier definition. There are still no runtime function-call instructions, stack frames, conditional branches, or loops. Tokenization is deterministically ASCII for the admitted grammar regardless of the host locale. Valid UTF-8 in comments is accepted, but non-ASCII identifiers or whitespace are rejected. The native CTest now executes assembled results for multi-function constant-folded sources and rejects invalid helper programs consistently across CLI modes. All emitted machine-code functions simply return the **compile-time computed constant** in \`oresforge_entry()\`; general instruction selection is not implemented.

Actors as Oreslang language constructs, ownership/borrows, concurrent scheduling, async/await, hot reload, true native JIT, native object generation, and OresVM integration **are not implemented**. Actor references now carry an **opaque, unforgeable-from-numeric-fields local
incarnation authority**, rotating on restart. This only prevents one local
mailbox from accepting a different mailbox's numerically matching handle;
it is **not** a cryptographically authenticated or network-serializable
capability, and it does not implement guest actor execution.

See [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), and [verification](verification/README.md).

License is not yet selected; maintainers should add an explicit license before external contributions.

See [value-boundary audit and admission tests](docs/VALUE_BOUNDARIES.md) for
aggregate, call, string, conditional and async feature limits.
