# Forge signed remainder: representable `INT64_MIN rem -1 = 0`

**Status:** draft correctness/parity change. The native C++ primitive implementation is verified locally; the new 15-guest source compiler pipeline needs stepful exact-head CI.

## Contradiction and resolution

The previous Forge `checked_rem` treated the pair `(INT64_MIN, -1)` like checked *division*: both returned `std::nullopt`. But the signed truncating **remainder** is exactly **0**, even though the mathematical quotient `2^63` is not representable as `int64`. Executing native C++ `INT64_MIN % -1` is undefined behavior; return zero *before* evaluating the operator. A zero divisor still rejects. Checked division `INT64_MIN / -1` continues to reject.

This aligns Forge with [atto-spec candidate #144](https://github.com/atto-lang/atto-spec/pull/144), [Java checked-i64 #66](https://github.com/atto-lang/atto-source.java/pull/66), [JavaScript executable test #69](https://github.com/atto-lang/atto-source.js/pull/69), and the [central 11-repo corpus #235](https://github.com/atto-lang/atto-platform-conformance/pull/235). The semantics are a **proposed standard**, pending normative review.

## Changed checks

- Production: `include/atto/checked_i64.hpp` now returns `0` on the exact MIN/-1 remainder pair, without division/overflow UB; all other cases unchanged.
- Existing `tests/checked_i64.cpp` checks the new rule at both C++20 `static_assert` and native runtime levels.
- Existing independent `tests/checked_i64_oracle.cpp` verifies overflow division and representable signed remainder **separately**, without itself evaluating unsafe `a % b` at MIN/-1; covers 9×9 boundaries and 100,000 high-entropy signed pairs plus near-zero divisors.
- Existing native `tests/scalar_conformance.cmake` changes MIN/-1 from rejected `E_OVERFLOW` to a real generated binary printing `0`.
- A new `tests/shared_checked_i64_guests.cmake` runs all fifteen cases from the **exact unchanged** Java/JS corpus, Git blob `71309fac2ab6b2191a0056c84d78987bd342dce0`, SHA-256 `e56dfccea11757ff18fc72a81ba5d643380e69184ab309eedf45e1e7b940e133`. It translates the reviewed named-call expressions into Forge's supported single-`main` scalar syntax (explicitly **not** unchanged multi-function-source parity). Every case must pass through `forge_scalar` source compilation, generated C++ compilation, actual native execution, and exact stdout/stderr/exit checks. CTest registers the adapter under `shared_checked_i64_native_guests`.

## Local tests run on exact copied Git blobs

The revised checked-in Forge C++ header blob `856e554994591f73ee4d74d10385c1e39d9d42f8`, `checked_i64.cpp` blob `3d80a2554530c1dd42f46f4ea90335bdaf38f5a3` and `checked_i64_oracle.cpp` blob `837bd8d97bb3ef2a529bae7c149748f29fc3c4e1` were reconstructed in a local Linux sandbox and verified against `git hash-object`. Executed:

- GCC and Clang at `-O0` and `-O2`: both existing exact production-code native test binaries compiled and passed, **8/8 runs**.
- Clang `-O1` ASan+UBSan on the real independent production-code oracle: **1/1 pass**.
- A new boundary executable with static assertions and runtime edge checks: GCC/Clang O0/O2 **4/4 pass**, Clang ASan+UBSan **1/1 pass**.
- Negative mutation: restoring the **old** `checked_rem` behavior only in a temporary copy makes the new boundary static assertion fail at compilation (GCC exit 1), proving this test detects the original contradiction.

**Total local host C++ binary runs: 14/14 PASS.** These are native primitive tests, **not** a run of the actual `forge_scalar` source-to-generated-native-guest compilation workflow and **not** a whole-program `.atto` conformance certificate.

## Remaining gates

Execute the complete CMake/CTest tree on real, exact-head source CI under GCC and Clang with sanitizers, including both the changed existing `scalar_guest_conformance` and new `shared_checked_i64_native_guests`. Record nonzero runner steps, compiler and binary hashes, source/tree SHA, diagnostics and independent review. The grammar still supports only Forge's current restricted scalar source; do not claim actors, imports, JIT/AOT engine parity, TenantVM security or hard real-time guarantees.

Existing [Forge numeric issue #75](https://github.com/atto-lang/atto-source.forge/issues/75) remains responsible for extending checked arithmetic throughout the compiled numeric backend and [#76](https://github.com/atto-lang/atto-source.forge/issues/76) for strict conversions/precision beyond i64.
