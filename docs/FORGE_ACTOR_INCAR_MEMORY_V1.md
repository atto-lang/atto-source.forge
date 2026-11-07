# Forge actor-incarnation private heap and logical stack — v1 host substrate

Status: **authored native C++ host code; exact-head native CI NOT_EXECUTED**.
This is a first backend-local memory primitive, not compiled AttoLang actor lowering
or true per-actor OS stack-fault containment.

## Implemented

ActorIncarnationMemoryV1 binds one distinct native heap buffer and one
distinct native frame buffer to the live, opaque issuance identity of one
Mailbox incarnation. The two buffers are independently bounded (bootstrap
defaults: 1 MiB heap, 64 KiB logical frame stack). Storage is native C++
memory, not Java/CLR tracing GC.

Mailbox::authorizes checks current local capability, actor ID, generation
and closed state under its mutex. Memory methods check the issued actor
reference and each caller's reference. Closing/restarting a mailbox revokes
old-incarnation memory authority on subsequent calls. Retirement zeroes and
deallocates the native buffers; reincarnation needs a new ownership object.

Heap handles are opaque capabilities: bounded allocation, copy-only reads and
writes, stale/cross-actor denial, zero-on-drop, and bulk cursor reset when all
live allocations are dropped. A long-lived allocation can pin the simple bump
cursor: this is not a complete compacting or free-list production actor heap.

Frame handles are distinct opaque capabilities. Strict LIFO, byte/depth
bounds, zero-on-pop, terminal poisoning on invalid ordering/quota failure,
exact execution identity, and one-shot suspended continuation fencing are
implemented. Native frame bytes remain present across trusted host-directed
suspension/resumption on a different carrier.

The new CMake regression compiles against Forge's real authenticated mailbox.
The source-local workflow requests GCC/Clang optimized and ASan/UBSan builds
and actual CTest execution at the checked-out exact PR head. These checks
are authored, not yet observed to have executed.

## Limits and required review

- No real define-actor source syntax, actor HIR/IR, compiler-produced stack
  layout, emitted native machine-code frame, or .atto typed local is lowered
  to this host primitive.
- Mailbox creation does not automatically create an ActorIncarnationMemoryV1
  instance. A trusted supervisor adapter must construct, bind and retire it
  correctly on STOP, restart, panic, and shutdown.
- A mailbox capability does not constitute a physical turn or carrier lease.
  The host must enforce non-reentrant dispatch and trusted continuation
  ownership. Suspension here is a bounded byte-state transition only.
- Mailbox::lock_authorized now holds the live mailbox-generation mutex
  throughout each native heap/frame byte operation, so STOP and restart wait
  for that operation rather than invalidating its authority mid-copy. The
  lock is held only across trusted bounded memory operations; no guest
  callbacks run while it is held. This establishes byte-operation fencing,
  not full async scheduler, in-flight FFI or guest STOP linearization.
  Do not expose this primitive directly to untrusted guests.
- Only per-instance quotas are present. Process/tenant aggregate reservation,
  FFI lifetime pins, full allocation recycling, and deterministic asynchronous
  reclamation must be implemented before unrestricted actors.
- C++ calls still use the carrier's physical OS stack. A logical frame slab
  cannot prevent a process-fatal recursive native stack overflow.
- Forge and Cranelift need distinct compiler-backed receipts. A C++ host test
  cannot certify either compiler's guest actor execution.

## Release gate

Keep draft until real, nonzero-step CI runs with exact head/tree readback,
GCC/Clang sanitizer execution, semantic integration with actor arena drafts
#101/#117, independent review, and compiler-backed canonical .atto actor A
failure / sibling B survival / restart / cancellation / continuation tests.
Steps=[] or steps=null, runner_id=0 and source-only readback do not count as
guest parity. No external private-source mirroring without authorization.

Companion review targets: Java actor frames #2627, .NET actor frames #385,
LLVM actor ownership/runtime work, platform-conformance #562.
