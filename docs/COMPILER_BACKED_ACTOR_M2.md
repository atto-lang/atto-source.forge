# Forge AttoLang actor M2: closed-world source-to-C++ native execution

**Status:** experimental opt-in compiler slice, draft and not certified. This is
a compiler-backed `.atto` actor **via generated C++20** that uses Forge's
native `ScalarActor`, `ActorOwnedI64Heap`, and actor-owned logical frame arena.
It is not Cranelift lowering, native Forge assembly, guest runtime self-hosting,
untrusted tenant isolation, or full actor language parity.

## Admitted source profile

Example: `examples/actor-counter.atto`

```atto
define actor Counter as
  let int total: 0
  receive(int message) -> int ::
    total = total + message
    return total
  end
end
```

The opt-in `oresforge emit-actor-cxx path.atto` parses and verifies the
**entire** file before emitting C++ source. It accepts exactly one actor with
one explicitly mutable signed-i64 field and one synchronous typed
`receive(int message) -> int` that performs a single `+` or `-` update,
then returns that field. Identifiers are source-local and are not interpolated
into generated C++. Initial signed bounds include `INT64_MIN` and `INT64_MAX`.

Unsupported source is rejected without a partial generated artifact:
imports, multiple actors, class/enum/model declarations, methods, calls,
closure/capture, malformed or extra tokens, Unicode identifiers, ownership
features, `await`/`suspends`, channels, actor groups, async, virtual actors,
unsafe native callbacks, integer operators outside this subset, untyped state,
reentrant actor turns and arbitrary handwritten host code.

This opt-in command **does not** expand admission to the ordinary Forge
`run`/`ir`/`emit-asm` modes. It emits C++ that must be compiled using an
approved native C++20 toolchain, and fails if parsing or output fails.

## Runtime behavior actually exercised by the new CTest

The compiled actor callbacks use a new **framed callback overload** that
preserves the older native scalar callback ABI. For each independently
admitted physical carrier turn, the emitted handler:

1. Mints an internal current-actor frame token and pushes a bounded
   16-byte frame (one i64 argument and one i64 local);
2. Writes and reads typed values through the actor-owned frame storage,
   rather than borrowing raw addresses from the carrier stack;
3. Reads and writes the per-actor mutable field using the existing C
   `ActorOwnedI64Heap` ABI;
4. Uses checked signed addition/subtraction with a recoverable C++
   exception on overflow instead of an `ud2` or host-process trap;
5. Pops and zeroizes the frame before return or failure. On a callback
   exception, the existing `ScalarActor` seals the affected actor, retires
   its frame arena, rejects further ingress and leaves sibling actors live.

The test script compiles *actual tracked `.atto` input* through the real
`oresforge` executable, links the generated C++ against the production
Forge actor runtime, then runs independent actor instances with the same
numeric id and different private heaps. It exercises inter-actor denial,
FIFO-sensitive results, carrier migration, STOP, overflow and underflow
sibling survival, signed extremes, source admission negatives and native
arithmetic mutation controls.

The compiler owns the generated expression semantics. The C++ toolchain
still performs the final native lowering, so this is **not** a source-to-Forge
machine-code IR or a general compiled AttoLang actor executor.

## Reproducible verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --target oresforge --parallel
ctest --test-dir build --output-on-failure -V \
  -R '^oresforge_compiled_atto_scalar_actor_native$'
```

A dedicated workflow additionally checks out the **exact PR HEAD**, logs
its Git tree, and runs GCC/Clang x optimized/ASan+UBSan. The CTest detects
sanitizer flags and applies them to the *emitted actor executable* as well as
the source-owning compiler. No CTest or source-org CI is a pass when the
hosted job has no runner and `steps=[]` or `steps=null`.

Only a standalone local compiler frontend + emitted C++ smoke with a stub
heap/frame host has executed in the authoring environment. The actual Forge
ScalarActor runtime integration and full source-org exact-head CTest remain
**NOT_EXECUTED** pending runner admission. Do not promote this work based
on the local stub-host check.

## Dependencies, safety gaps, and next implementation

- Base Forge actor-private heap PR #101 and logical frame PR #207 must be
  independently reviewed and reconciled with Forge arena/STOP draft #117.
- Replace this source-to-C++ bootstrap with compiler-owned typed MIR and
  native Forge/Cranelift emission for genuine `.atto` actor methods.
- Replace this scalar 16-byte handler frame with a verified descriptor
  `{actor_generation,function_id,pc,params,locals,operands,live_roots}`.
  Implement iterative recursion (no growth of the shared carrier stack),
  explicit tail transfer, bounded frame stack fault admission and lexical
  ownership/borrow roots.
- Add `suspends` source lowerings with committed suspension/continuation
  state, carrier migration, cancellation/STOP, supervisor restart and stale
  generation/ABA denial before supporting async guest code.
- Add aggregate supervisor heap/frame quotas and all external FFI/I/O/borrow
  pins before physical reclamation. Native stack overflow and arbitrary
  SIGSEGV still are **not isolated** by logical actor frames.
- Establish separate exact-head compiler-backed actor execution receipts
  for Java/GraalVM, LLVM, Forge, .NET NativeAOT and Wasm. Never import one
  backend's test result as another backend's certified parity.
