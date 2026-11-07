# Forge actor heaps + actor-private frame storage (2026-11-07)

## Implementation and exact scope

This change **stacks on Forge draft #101** (native scalar actor with an owned
`ActorOwnedI64Heap` and nonreentrant callbacks). It does not overwrite #101
or Forge #117, the independent general actor-arena/mailbox integration.

Each trusted `ScalarActor` incarnation now owns a separately allocated,
bounded `ActorFrameStack`, provisioned before actor callbacks run:

- 64 logical frame slots and 64 KiB of actor-private frame bytes by default.
- A distinct unforgeable, in-process authority per stack; numeric actor ID
  collisions do not grant access to another actor's frames.
- Turn epoch + physical-carrier checks before every frame access.
- LIFO push/pop, bounded writes/reads, monotonically stamped slot handles.
- Frame bytes survive turn exit, so a later admitted turn on another carrier
  can resume from retained actor-owned storage. The old turn token is invalid.
- Pop zeroizes its frame bytes; termination revokes new operations before
  scrubbing on quiescent turn exit. Overflow and out-of-order pop poison that
  actor's frame stack. A poisoned callback cannot commit the actor turn even
  if it catches the original exception.
- `ScalarActor::run_one` enters both the existing private scalar heap turn
  and the actor frame turn; `stop()` retires frames only after accepted work
  drains. No guest callback holds the actor's mailbox/lifecycle mutex.
- A frame handle is never a guest pointer. Callback-local access requires a
  host-minted current-turn token. The host object must still be quiesced before
  destruction. These APIs are **not** network actor capabilities.

The implementation retains Forge's stage-0 `ActorOwnedI64Heap` and
`ScalarActor` callbacks unchanged at the ABI boundary. Frame storage is a
separate **logical stack arena**, not a switched execution stack.

## What this does NOT prove

- **No per-actor physical OS stack or guarded native stack switch yet.**
  C++ callback invocations still run on their current carrier's OS stack.
  Consequently, a hardware stack overflow or native memory fault may still
  crash the entire process. This PR cannot claim OS-process crash isolation.
- No compiler-lowered `.atto` actor, typed scalar call-frame descriptor,
  guest `await` / `suspends`, actor bytecode/iterative dispatcher, stack-map
  roots, borrow checker, native frame-pointer switching or guard-page proof.
- No authenticated restart/supervisor durable continuation restore, external
  FFI/I/O pin quiescence, cross-tenant isolation, hot-upgrade frame migration,
  untrusted guest code, portable JIT or end-to-end self-hosted compiler.
- No whole-process memory budget: each actor has a bounded local allocation;
  supervisor-wide aggregate admission is future work.
- CMake/CTest host tests are not compiler-backed `.atto` guest execution.
  Zero-step Actions runs mean NOT_EXECUTED.

## Evidence gates

```sh
# Run on exact PR head, with actual allocated runners and nonzero steps.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure \
  -R 'oresforge_actor_private_frames_host|oresforge_actor_heap_frame_bridge_host|oresforge_scalar_actor_heap_and_turns'
# Existing optimized/sanitized source-local native memory workflow:
bash scripts/verify_native_actor_memory.sh optimized
bash scripts/verify_native_actor_memory.sh sanitized
```

New host regressions: invalid constructor quotas, two actors with the same
numeric ID, cross-actor handle denial, concurrent reentrancy rejection, stale
turn/slot rejection, nested LIFO order, 16,384 zeroized reuse iterations,
suspension/resumption on another carrier, overflow poisoning, retirement in an
active turn, real scalar actor heap/frame callback integration, swallowed
overflow detection, and survival of a sibling actor.

The standalone frame executable was locally compiled/run with GCC C++20
`-O2 -DNDEBUG -Werror` and Clang C++20 ASan/UBSan `-Werror`.
This is local host-only evidence; the hosted/complete Forge CTests and new
callback-bridge test still require an actual exact-head run.

## Cross-backend dependency register

| Family | Related implementation | Promotion gate |
| --- | --- | --- |
| Java/GraalVM | Java #2627 actor-owned frame bytes; #2631 SourceTask await/resume; #2633/#2640 iterative typed scalar frame VM | Compiler-backed typed guest calls, generation-fenced await and exact-head CI |
| LLVM | LLVM #1215 compiled sealed actor heap slice; #1456 compiler-backed actor heap ownership test candidate | Executed AOT guest lifecycle, native actor-owned stack frames and bounded recoverable errors |
| Forge | #101 private scalar heap; this stacked frame branch; independent #117 generic arena/STOP work; #202 scalar `.atto` executable subset | Merge/reconcile parent dependencies, then wire actual typed `.atto` actor lowering into both allocations |
| .NET / Wasm | Independent source backend implementations | Neither backend is certified by Forge host tests |

## Next implementation steps

1. Design an immutable typed frame ABI `{actor_incarnation,frame_generation,
   function_id,pc,locals,operands,live_roots}` for compiler-owned `.atto`
   lowering. Verify scalar calls, non-tail recursion limits, exception edges
   and root/borrow lifetime; retain same frame through `await`, resume and
   cancel with generation fences.
2. For true per-actor **physical** stacks, select a guarded, fixed-reservation
   stackful coroutine/context-switch backend (including sanitizer switch
   hooks), or explicitly specify stackless execution as the supported actor
   stack model. A reserved buffer with no context switch is **not** proof.
   A recoverable stack overflow requires supervised fault containment and
   must never rely on a plain SIGSEGV in the shared process.
3. Enforce supervisor aggregate frame+heap quotas; pin all callback, FFI, I/O,
   async continuation and borrow owners before reclamation. Prove STOP and
   restart races, sibling survival, reused-address ABA and hot-upgrade
   generation fencing under sanitizers.
4. Obtain nonzero exact-head source-owned CTests, independent approval, then
   compiled `.atto` actor execution receipts in each backend separately.

Do not promote the feature to self-hosted/actor guest parity based on
source-only, standalone host, synthetic or zero-step evidence.
