# Forge stage-0 native AOT dispatch benchmark

**Status:** new measurement harness for the stage-0 callable subset only.
This is **not** an AttoLang-wide performance claim, general devirtualization
proof, `STATIC_WITNESS` or `CLOSED_INDIRECT` implementation, or five-backend
performance comparison.

## Inputs and execution

The benchmark depends on [Forge static-dispatch hardening PR #252](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/252).
The separate benchmark PR is stacked on that branch to avoid changing its
already verified exact source head. It uses the existing real C++
`oresforge_static_dispatch_aot_verifier` with the canonical
`tests/fixtures/static-dispatch/direct-42.atto` source.

Run on an authorized, pinned Linux x86-64 runner:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-bench --target oresforge_static_dispatch_aot_verifier --parallel 2
bash benchmarks/static_dispatch/run.sh build-bench/oresforge_static_dispatch_aot_verifier
```

The runner first rechecks actual parsed `.atto` source and the verified IR,
requires emitted native assembly to contain both known static helper symbols,
rejects indirect branches in the closed fixture, and then links and executes
the real assembly together with the timing driver at GCC/Clang `-O0`/`-O2`.

Each artifact records the compiler/mode, compile time in milliseconds,
executable size in bytes and SHA-256. The driver checks every guest result is
42, warms each path, takes 7 monotonic-time samples of 800,000 calls each,
and reports median nanoseconds per call.

## Interpreting the numbers

- `direct_ns_per_call`: real emitted Forge stage-0 direct native entry path.
- `synthetic_host_indirect_ns_per_call`: the **same compiled Forge entrypoint
  called through a volatile C++ function-pointer slot**. It is a *synthetic
  host-side control*, **not** AttoLang dynamic method dispatch, generic trait
  lookup, actor routing, or a implemented `CLOSED_INDIRECT` backend path.
- The benchmark includes the guest method body plus caller/callee overhead.
  This is deliberately not a pure single-instruction indirect-branch test.
- The direct and synthetic paths have distinct measurement overhead.
  Repeated process runs, stable CPU frequency, isolated runner load,
  hardware details, optimized build configuration and confidence intervals
  are required before publishing comparisons as performance improvements.
- `compile_ms` measures the **assembly + C++ driver link step**, not the
  whole Forge compiler build or source compilation latency.
- Binary size measures the **standalone test executable with driver**, not
  the entire AttoLang application distribution.

## Evidence rules

Run must record exact source commit + tree, test input blob IDs, stepful runner
execution and output SHA-256. A zero-step failure is runner admission failure,
**NOT_EXECUTED**. Synthetic pointer/control measurements must never count as
implemented runtime virtual/trait dispatch evidence. Do not assert an expected
speedup or fabricate benchmark numbers; report measured values with environment
and caveats. A private file-blob mirror is not proof of full root-tree equality.
