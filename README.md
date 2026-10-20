# OresForge

An **independent, experimental C++20 compiler and execution stack** for Oreslang, distinct from GraalVM/Truffle and LLVM. This is a working **bootstrap prototype**, not a production Oreslang compiler.

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
