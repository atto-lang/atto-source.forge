# Forge M3: sealed iterative actor frames, fail-closed faults and evidence

**Status:** stacked draft on Forge [PR #214](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/214), which depends on logical frames #207 and private heaps #101. Keep #117 STOP/arena reconciliation separate until semantic conflicts are reviewed.

## What this actually implements

`runtime/actor_frame_vm.hpp` introduces a **host-only**, sealed, typed i64 interpreter that runs a finite instruction vocabulary on the existing `ActorFrameStack`. A call is a 32-byte actor-owned logical frame with argument, accumulator, temporary and program counter. The evaluator is an **iterative loop**: neither direct nor mutual recursion adds a native C++ call frame. `Call` stores the parent PC and pushes a new actor-private frame; `Return` restores the saved parent accumulator and zeroizes the popped frame.

This is intentionally **not** a compiled `.atto` general method, self-hosted compiler, Forge/Cranelift native machine-code lowering, stackful coroutine, physical stack switch, FFI sandbox or production untrusted-tenant isolation. The source-to-C++ actor emitter remains the only compiler-backed `.atto` slice; the VM is the next runtime substrate for future compiler lowering.

The VM construction verifier rejects empty/oversized programs, unknown opcodes, noncanonical unused operands, out-of-bounds function targets, missing returns, backwards/unbounded same-frame branches and malformed fallthroughs, even for unreachable instructions. Program data is copied into an immutable vector during admission. The VM provides only typed i64 registers, checked signed addition/multiplication and explicit call/return semantics. No raw pointers, function closures, implicit conversions, operator overloading or nested `Option`/`Future` flattening can enter this instruction set.

## Actor-local safety contracts

- **Frame quota:** 64 logical frames per actor, 32 bytes each for this VM, using the existing 64 KiB max private frame storage. Recursion depth 65 is rejected and poisons the offending frame arena; it cannot grow the shared carrier's machine stack.
- **Work quota:** the slow-path budget check occurs only every 128 executed scalar opcodes, with a maximum of 16,384 opcodes per invocation. Exhaustion irreversibly retires the actor frame arena and throws an actor-local `std::length_error`. This is a bounded interpreter budget, not a general CPU sandbox.
- **Arithmetic:** signed overflow is checked before addition/multiplication, with a recoverable exception and terminal frame retirement. No signed-overflow undefined behavior is knowingly admitted.
- **Swallowed faults:** `ScalarActor::run_one` now rejects **both** poisoned and retired frame arenas after invoking a callback. An external callback cannot make a quota or overflow fault appear to commit by catching it and returning a value. The actor's mailbox is cleared and its heap turn is left; siblings with their own identities/heaps remain active.
- **Carrier migration:** a new turn on a different physical carrier obtains a fresh actor-local token; stale prior-turn tokens remain denied. This substrate does not support suspension inside a VM invocation yet.
- **Ownership:** all actual guest frame reads/writes pass through checked frame handles; host-level `std::array` handles are not exposed to guest code. A caller must quiesce external references and loans before destroying a `ScalarActor`.

## Tests added or tightened

1. `oresforge_actor_frame_vm_host` — factorial, Fibonacci, mutual recursion and exact results across multiple invocations; property-based signed addition and multiplication sweep (2,050 assertions); recursion quota and work quota; malformed/unreachable bytecode; stale carrier token; real `ScalarActor` native heap integration; same-numeric-ID actor authority denial; overflow STOP and sibling survival; caught-quota rejection.
2. `oresforge_actor_source_admission` — 674 deterministic accepted emitter cases + 366 negative source cases, including reserved language identifiers, bad signed limits, CRLF comment handling, duplicate definitions, unsupported operators/imports/`await`/classes and injection attempts. It also verifies deterministic output and that source identifiers do not enter generated C++.
3. `oresforge_compiled_atto_scalar_actor_native` — strengthened source-to-native tests now require mutation controls to exit **successfully with a wrong observed scalar result**; generic crashes are explicitly **not** mutation evidence. The test records native binary SHA-256 and checks that eight legacy command modes still reject opt-in actor syntax.
4. Exact-head GitHub Actions GCC/Clang × optimized/sanitized matrix now compiles and invokes all three focused CTests. Generated native binaries are instrumented in sanitizer mode.

## Local evidence — tested against exact GitHub source blobs

The local test workspace uses individually fetched Forge sources, and `git hash-object` reproduced the exact GitHub blob SHA for `ActorFrameStack`, `ActorScalarFrameVM`, `ScalarActor.{hpp,cpp}`, `ActorOwnedI64Heap.{h,c}`, `actor_source.{hpp,cpp}`, the source admission test and all four `.atto` fixtures. No source was mirrored to another repository or organization.

| Local execution | GCC optimized | Clang optimized | GCC ASan/UBSan | Clang ASan/UBSan |
|---|---|---|---|---|
| Actor source admission 674 accepted + 366 denied | PASS | PASS | PASS | PASS |
| VM arithmetic/fault property smoke (2,058 assertions) | PASS | PASS | PASS | PASS |
| Native `ScalarActor` + actor heap + frame VM integration | PASS | PASS | PASS | PASS |
| Four `.atto` emitted-to-C++ native runtime fixtures | 4/4 | 4/4 | 4/4 | 4/4 |

Two standalone arithmetic mutations were also compiled and **exited cleanly**, producing `5 → -5` and `15 → 25` as expected wrong-result witnesses. These were local executable smoke tests, not source-org-hosted Actions receipts. The `.atto` emission test uses the authentic `actor_source.cpp` behind a small local CLI wrapper, **not** the complete Forge compiler executable.

**Not yet executed:** exact full repository CMake/CTest and source-org GitHub Actions on this PR head. Prior Forge Actions jobs reported `runner_id=null` with `steps=null`; zero-step jobs remain **NOT_EXECUTED** and cannot authorize a merge. The dedicated workflow is configured but should not be labeled PASS until a real runner executes it.

## Next compiler parity gates

1. Fix hosted GitHub Actions runner admission and produce nonzero executed steps on the exact PR HEAD, with an independent review.
2. Integrate the typed VM instruction structure with verified `.atto` actor HIR/MIR (functions/methods and call sites) instead of building VM instances manually in host tests.
3. Extend frame descriptions with static function ID, stable generation fence, locals, typed operand/root maps, ownership/borrow metadata and scheduled continuation state.
4. Implement `suspends`/`await`, cancellation and restart fences, explicit borrow-root pin accounting and quiescent hot-upgrade proof. Ensure all affected backends execute identical source fixtures independently.
5. Treat separate guarded physical native stacks as an independent alternative to this stackless evaluator. A guard page by itself does not make a wild native stack overflow recoverable in-process.

**Promotion policy:** keep draft until the dependency chain #101 → #207 → #214 and this hardening PR have independent approval, exact-head nonzero native test receipts and security review; reconcile #117 semantically without removing STOP, ownership or generation checks.
