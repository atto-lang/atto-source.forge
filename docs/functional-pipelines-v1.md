# Forge functional-pipeline bootstrap compiler (bounded native subset)

**Implementation authored; executable guest parity requires exact-head execution evidence.**

The new `forge_functional` C++20 frontend compiles real Atto `.atto` source into deterministic C++ source, which GCC/Clang compile into native binaries. It is adjacent to the unchanged `forge_scalar` stage-0 compiler. It is **not** the production Forge JIT/AOT engine, an actor runtime or TenantVM sandbox.

Accepted syntax: signed int64-only named functions with up to eight args, `: int { return ...; }`, `: int -> expression;`, `-> int { return ...; }`, up to four immutable `val name = expr;` locals before the final explicit return, checked arithmetic `+`, `-` and `*`, named calls, and left-to-right `|>` plus right-associated `<|`. Arbitrarily mixed directions require parentheses. Calls are validated for symbol existence, argument count and cycles; local bindings reject future reads, duplicates and shadowing. Bounded source, token, recursion, function, argument, and output budgets apply. No guest expression bytes are injected into generated C++; only validated identifiers and typed integer literals are emitted.

Example compilation:

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DATTO_FORGE_SANITIZERS=ON
cmake --build build --parallel 2
build/forge_functional fixtures/functional-pipelines-v1/block-body.atto -o /tmp/functional-pipeline.cpp
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -I include /tmp/functional-pipeline.cpp -o /tmp/functional-pipeline
/tmp/functional-pipeline
# expected stdout: 83
```

Both exact user examples print `83\n`; nested right association and explicitly grouped directions print `42\n`. The Forge scalar CLI convention prints the result and returns OS status 0 (unlike the Rust/LLVM/.NET fixtures which may use OS exit status). Native overflow prints `E_OVERFLOW` to stderr and returns 70. Invalid sources return 2 without a generated C++ file.

The tests `tests/functional_pipelines.cmake` compile and execute all four generated native guest programs, check deterministic generated outputs and reject 14 cases without output artifacts. Four positive and four negative sources are copied **byte-for-byte** from atto-spec#121's canonical fixture blobs. Existing Forge scalar compiler tests remain intact.

## Verification and security boundaries

No performance, Native AOT, hard-real-time, full language, or multitenant security parity claim is made by this bounded path. In-process untrusted actors require a separate validated TenantVM memory/capability/CPU/I/O boundary, never the C++ emitter alone. Production integration and exact native guest execution logs on GCC/Clang with source/tree SHA remain required.

Related implementation: atto-source.rs#381, atto-source.llvm#54, atto-source.net#16, atto-source.java#51; contract atto-spec#121; evidence ledger atto-platform-conformance#112.
