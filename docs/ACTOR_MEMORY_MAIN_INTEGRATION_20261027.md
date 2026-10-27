# Current-main Forge actor-memory integration
Draft; host runtime only. Guest actor lowering, protected untrusted execution,
aggregate process quotas and external continuation/FFI lifetime pins remain open.

Base main: e0513d495f0b0b30bd6999f402b4dec2f2f1f4b4.
Arena/header and unit-test origin: integration PR #58 at
ebf9e76c34f2ee367749388abc1f5351de5311cc. Only the arena and focused tests
are ported; the older compiler/routing/CMake tree is not substituted for main.

## Implemented

* Fixed-capacity per-actor persistent and scratch arrays, opaque issued handles,
  nonrecycling domains and carrier identities, generation and epoch checks.
* Carrier identity issuance happens before active-turn publication.
* retire() seals new turns and byte authority immediately, even with an active
  turn. Only the admitted physical owner can end that turn. Final scrubbing
  waits for that leave; destruction while active remains fail-stop.
* Existing current-main authenticated mailbox is exercised with the arena:
  deep-copy admission, credits, independent actor values, foreign handles,
  STOP/restart, stale contexts and zeroed new incarnation.
* Added normal CTest registration without removing/replacing any existing
  current-main declarations. ORESFORGE_ACTOR_MEMORY_HOST_ONLY builds only the
  actual runtime tests and current mailbox implementation.

## Executed local evidence

GCC C++20, strict warnings-as-errors: arena unit suite and mailbox-memory
integration both pass at O1 and O2. Both pass with ASan+UBSan
(detect_leaks=0): arena O1, integration O2. Unit suite includes 8192 slot-reuse
iterations, 512 sequential carrier lifetimes, domain exhaustion/concurrency,
foreign carrier denial and destructor retirement guards.
The new integration test against the unchanged #58 header fails with
"active actor turn blocks retirement"; patched header passes.

No local cmake or clang exists, so CMake/CTest and Clang remain NOT_EXECUTED.
Source-local exact-head workflow requires GCC/Clang x O1/O2, optimized and
sanitized CTests. Zero-step hosted jobs do not certify any of these gates.
This does not create a source actor scheduler or an OS thread per actor.

Java/Graal #2157, LLVM #1086 and WASM #153 remain companion unmerged fixes.
