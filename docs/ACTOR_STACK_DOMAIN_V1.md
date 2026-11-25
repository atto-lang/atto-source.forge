# Forge native actor stack domain v1 (host substrate)

**Evidence class:** host callback execution on a real separate native stack;
**NOT** compiled AttoLang/Oreslang `define actor` execution, a guest scheduler,
security sandbox, self-hosted compiler, hard-real-time certification, or
Java/LLVM semantic parity.

## Implemented in this draft

- Linux x86-64 only: each `ActorStackDomain` owns a distinct `mmap` region with
  an independent writable stack between two `PROT_NONE` guard pages. It uses
  `getcontext` / `makecontext` / `swapcontext` to execute a C++ callback on that
  actor's stack, rather than merely allocating an unused stack buffer.
- Real bounded, zero-initialized per-actor byte heap. Opaque local handles
  include a domain incarnation; loads, writes, allocations and retirements
  require the current actor's live turn. Foreign, retired, out-of-bounds, and
  outside-turn operations fail closed. No raw heap pointers escape the API.
- Calls are exclusive on each physical carrier (TLS). Nested cross-actor calls
  are denied rather than silently executing inside another actor's turn.
  Contexts are thread-affine and cannot migrate across worker threads.
- Callback exceptions unwind inside their own actor stack and are delivered
  back to the trusted caller; the callback may run again in a later turn.
- Explicit STOP rejects subsequent turns and wipes mapped stack bytes and
  actor heap bytes before release. Destruction while executing is prohibited.
- The allocator is bounded bump allocation: released slots are zeroed and
  invalidated, **but their bytes are not reused until actor retirement**.
  This avoids unsafe aliasing before a general actor-owned drop/loan protocol
  is implemented. Do not advertise O(1) slot reuse or indefinite allocation.

## Tests and limitations

**Negative containment baseline:** overflowing a real native actor stack
currently hits a guard page and triggers SIGSEGV, terminating the whole
process if run in-process. The guard regression therefore forks a child
process and checks SIGSEGV from the parent. Even with a separate healthy
actor present in that child, this is **NOT actor-only containment**.
The parent-process survival test demonstrates only OS process isolation.
A normal C++ callback throw is recoverable after unwinding and leaves an
independent actor's private heap intact, but that is distinct from stack
overflow. Compiled guest recursion must use an actor-owned checked depth/frame
budget and raise a typed, terminal actor PANIC before any guard-page fault.
Its parent supervisor CONTROL notice must remain independent of any local
`on_panic` observer. Neither `on_raise` nor lexical `recover` may consume
physical process faults.

`verification/actor_stack_domain_test.cpp` covers actual callback stack
addresses, distinct actor stacks, per-actor persisted heap state, foreign
handle and nested dispatch rejection, out-of-bounds access, stale handle
retirement, callback exception recovery, wrong-carrier admission, STOP,
invalid budgets, and a child-process guard-page SIGSEGV negative test.

Run the source CTest on Linux x86-64:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target oresforge_actor_stack_domain --parallel
ctest --test-dir build -R '^forge_actor_stack_domain_host_v1$' --output-on-failure
```

Optional standalone sanitizer command, with the known ASan `ucontext`
limitation and fake-stack address substitution disabled for this test:

```sh
c++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
  verification/actor_stack_domain_test.cpp -pthread -o /tmp/actor_stack_domain_test
ASAN_OPTIONS=detect_stack_use_after_return=0 /tmp/actor_stack_domain_test
```

ASan itself warns that `makecontext`/`swapcontext` are not fully supported.
Therefore sanitizer passes do **not** certify fiber correctness. The normal
O0/O2 GCC/Clang and guard-fault negative tests are independent evidence.
The API is a trusted-host prototype, not a malicious-native-code boundary.
On other OS/architectures this header intentionally refuses compilation;
CMake does not register this test there.

## Required parity integration gates (still NOT_EXECUTED for this draft)

1. Integrate the existing Forge actor-owned arena work (PR #117) and bounded
   scalar actor lifecycle work (PR #101), preserving their incarnation and
   STOP/loan semantics rather than replacing them with this minimal heap.
2. Bind source `define actor` parsing, type checks and native lowering to a
   stable Actor ABI. Compiled source guest callbacks—not just C++ callbacks—
   must execute on actor stacks, with constructor/receive/teardown evidence.
3. Add true suspend/resume, frame pinning across `await` / `select`, lifetime
   and cancellation semantics; this first revision runs each turn to return.
4. Prove `rt borrow`, `rt take`, `rt share`, sealed `critical-read` and
   `critical-write`, generational mailbox tickets, supervisor CONTROL notices,
   ActorGroup capabilities, tenant quotas, actor pipelines, and supervised
   shutdown against independently executed Java and LLVM guest fixtures.
5. Enforce tenant security at process/Wasm/OS boundaries; guard pages alone
   do not prevent hostile native code from reading another actor's memory.
6. Do not claim source-actor, self-hosting, cross-backend or release parity
   without exact-head native guest runs, negative controls, artifact hashes,
   workflows/jobs with nonzero executed steps, and independent review.

**Runner/privacy:** this PR does not copy private source to another GitHub
organization or authorize carrier mirroring. Source-owned testing is required;
carrier execution additionally requires independent approved export policy.
