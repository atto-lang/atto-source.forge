# OresForge self-hosting: stages, evidence, and release gates

**Status (2026-10-20): not self-hosted.** The compiler on `main` is written in
C++20. Its Oreslang source frontend accepts only a zero-argument `main(): int`
whose return is a checked i64 arithmetic expression on the main branch.\nOn the proposed Forge typed-function branch, pure helper functions, typed i64\nparameters and calls are statically inlined into that expression; there are\nstill **no true native function calls, stack frames, runtime inputs, loops,\nstrings, modules/import resolution, collections, ownership, error handling,\nor async support**. A multi-function source returning a constant after static\ninlining is not a general-purpose guest compiler.
The assembly emitter currently compiles **constant-evaluated** entrypoints,
not arbitrary executable IR.

The `verification/native_asm_executable.py` CTest runs the real compiler's
`run` and `emit-asm` commands on identical source, assembles/links the
output with a local C host driver, and executes it. It compares exact signed
i64 stdout for positive cases, including the typed-helper call inliner\nonce that feature branch is merged, and requires interpreter/IR/assembly commands
to reject unsupported or overflowing source without emitting stdout. Its
JSON receipt declares `self_hosted: false` and explicitly limits native
codegen to a constant entrypoint. This is a **stage-0 native assembly smoke**,
not a compiler bootstrap or guest-language runtime demonstration.

## Planned self-hosting chain (not implemented)

| Stage | Compiler implementation | Required executable proof |
| --- | --- | --- |
| 0 | Existing trusted C++/other approved reference compiler | Build and run source-to-IR/native tests using pinned compiler/toolchain revisions; verify no unsupported feature is silently admitted |
| 1 | Compiler implemented in Oreslang; compiled by stage 0 | Compile actual Oreslang compiler source, using a real module graph, function calls, data types, input/output, and target backend; execute resulting stage-1 compiler on positive and negative fixtures |
| 2 | Same compiler source compiled by stage 1 | Execute stage-2 compiler and require observable agreement with stage 1 on accepted/rejected source, diagnostics policy, canonical IR/ABI, and native guest behavior |
| 3 | Repeat compilation by stage 2 | Require fixed point of canonical compiler outputs under pinned reproducible toolchains, or document/hash precisely accounted nondeterministic fields; verify rebuilt compiler can compile new Oreslang programs |

**No stage-1, stage-2, or stage-3 compiler artifact is present on main.**
A successful CMake build, interpreter run, CTest suite, source-free policy,
or GitHub Actions workflow which never executed a test step cannot advance
the self-hosting evidence level.

## Promotion requirements

1. **Real language implementation.** Source parser/AST and semantic analysis,
   module graph, typed function calls/recursion, user-defined types,
   ownership, error paths, control flow, I/O and compilation APIs must
   execute sufficiently to compile the Oreslang compiler itself. Failure
   should be explicit; dummy interpreters, precomputed compiler outputs and
   staged C++ scripts cannot count as a guest compiler.
2. **Single immutable input graph.** Pin compiler `.ores` source file hashes,
   imports, stdlib/core versions, target profile, target ABI, code-generation
   settings and all build tools. No ambient resolver search path, network
   import, undeclared dependency, dynamic reflection or host fallback.
3. **Execution and negative controls.** Execute stage-1 and stage-2
   compiler binaries. Reject malformed input, unsupported syntax, untrusted
   import paths, invalid UTF-8 and type/borrow errors at each stage. Compare
   diagnostics by stable code/semantic category, not platform path strings.
   Run positive program outputs, recursion limits and deterministic failures.
4. **Independent differential oracle.** Compare against independently built
   Java/Graal and LLVM compilers on their **intersection of implemented
   features**. Never promote a backend just because another backend passed.
   Count native execution only when native artifacts actually ran.
5. **Reproducibility and supply chain.** Keep source tree SHA, toolchain
   digest, build manifest, stage artifact SHA-256, fixture hashes, raw logs,
   exit codes and actual executed workflow/job/step identity. Stage-1 versus
   stage-2 equivalence alone is not proof of correctness (a common compiler
   miscompile can repeat); use independent oracles and mutation negatives.
6. **CI privacy.** This source repository is private. A cross-organization
   runner's `GITHUB_TOKEN` does not automatically read it. Before any
   mirroring, require approved *private* source and destination,
   explicit destination authorization, commit/tree/blob integrity checks,
   minimal granted permissions and no credential or log leakage. Do not move
   source to public test repositories or confuse matching Git trees with
   permission to disclose private code.

## Immediate next engineering milestones

- Add typed multiprocedure IR and calls (including recursion budgets) with
  bytecode/IR verifier coverage; stop treating a single constant-return
  function as general native codegen.
- Implement lexical environments, assignment/control flow and aggregate
  layout with explicit i64 semantics; exercise native code that computes from
  runtime inputs rather than constants.
- Implement deterministic module resolution and full import graph, using
  exact file identities and preserving core/alloc/stdlib boundaries.
- Port an Oreslang compiler frontend/semantic pass into `.ores` source and
  prove it can compile a deliberately small, executable guest program.
- Only after the Oreslang-written compiler can compile its own supported
  source graph, add the stage-1/2/3 executable bootstrap gate and enable it
  as mandatory on any release profile labeled `self-hosted`.

**Never name a release self-hosted without the full chain and exact-head
executed evidence.**
