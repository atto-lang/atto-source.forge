# Forge stage-1 compiler-owned Core identity linking

New opt-in `oresforge run-core` and `oresforge emit-core-asm x86_64`
modes compile a canonical `identity.atto` module embedded at build
time. The stage-1 callable parser first parses the module source,
then imports `identity.passthrough` from a second source unit and
lowers it into the existing verified native direct-call graph.

Only canonical `atto.core/identity` and historical
`ores.core/identity` suffix aliases are accepted. No ambient source
roots, user CWD, filesystem fallbacks, Alloc, Std or actor capability
imports are introduced. This is not the full `numeric.atto`
contract; if/do/fi and broader type semantics are still unsupported.
The source-owned native CI runner remains unallocated; authored code
must not be misrepresented as completed compiler-backed parity.

## Stage-1 executed native test plan

The new CTest `oresforge_atto_stage1_core_identity` invokes the real
Forge binary and emits two actual x86-64 native-call assembly files.
The generated executable outputs must be 42 and 43, both PIE and
non-executable stack. Its test-local `atto.core/identity.atto` file
returns 99 and must never override the compiler-embedded module.
A five-case negative import matrix rejects Core typos, Alloc/Std/actor
and local-path substitutions with no emitted assembly. The script
records a native ELF SHA-256 when it actually executes. This does
not prove full `numeric.atto`, portable backend parity or source-owned
CI until runners allocate.

## Hosted binary evidence scope

The stage-1 native script links the generated guest assembly with a
C++ test harness using the normal hosted runtime. PIE and non-executable
stack flags do **not** imply zero ELF/libc imports or freestanding Core.
Its emitted scope explicitly says `native_harness=HOSTED` and
`guest_host_imports=NOT_VERIFIED`. The source's lack of imported
Std/actor declarations is not a substitute for a compiled ELF import
inventory audit. The native backend here is Forge's x86-64 compiler,
not a separately proven Cranelift implementation.

## Exact-head CI receipt integration

The native Forge CI bootstrap now reruns the compiler-owned Core
identity script under both GCC and Clang, binds the source commit and
tree and the exact two committed `.atto` file Git blobs, records the
compiled Forge executable's SHA-256 and the native guest ELF digest,
and uploads the log/receipt. The script consumes a checked-in example
`examples/atto-core-identity.atto` and tests its byte-equivalent
scratch copy with a local hostile namespace-shadow source. Hosted
runner steps are still unexecuted as of authoring.

## Stage-2 numeric.atto typed control-flow admission

The Forge native callable frontend now supports a separate
`parse_core_numeric` path, exposing `run-core-numeric` and
`emit-core-numeric-asm x86_64`. It independently parses the existing
**unchanged** `contracts/atto-core-v1/numeric.atto` via a CMake-
embedded resource and resolves only the six canonical/historical
numeric import spellings. Its `if CONDITION; do return VALUE; fi`
early-return chain is lowered to lazy typed `If` nodes in the native
call IR. A greater-than comparison uses the existing checked signed
i64 comparison opcode, not a source-level text rewrite or host helper.
This intentionally bounded numeric grammar does not support arbitrary
nested language features, allocator/Std/actor capabilities, or general
module graphs. All such requests remain fail-closed.

## Native stage-2 numeric CI regression gates

The new CTest and the existing GCC/Clang GitHub bootstrap workflow
compile the canonical embedded `numeric.atto` with exact checked-in
source identity, then execute native x86-64 guest outputs 42 and 43
through a hosted harness. Native assembly and binary hashes must
change with the source mutation, and both binaries must remain PIE and
non-executable-stack. Seven unsupported imports fail without any
published assembly. Twelve in-process numeric interpreter/CIL parity
controls cover min/max/clamp equality, extremes and reversed bounds.
The job receipt records immutable git HEAD/tree, compiler SHA and
source blob identities. These are authored gates, not executed
GitHub receipts; source-owned runner allocation is still blocked.

## Working-directory and incremental source-freshness hardening

Both Core stage-1 and numeric stage-2 native test scripts now
canonicalize their compiler and repository paths with `realpath`
**before** switching to hostile per-test working directories. A
relative `./build/oresforge` otherwise resolved under the scratch
directory and prevented the guest test from running at all. CMake also
tracks both canonical compiler-owned `.atto` inputs in
`CMAKE_CONFIGURE_DEPENDS`, so an incremental rebuild cannot silently
reuse stale generated embedded source after a source mutation.

## Generated-header raw-literal escape control

Both canonical `.atto` units are embedded in generated C++
raw-string literals. A future file containing the exact
`)__ATTO_NUM__"` or `)__ATTO_CORE__"` sequence would
prematurely close the literal, potentially breaking or injecting
C++ in the compiler build. The configure-time guard now rejects
that source **before** generating a header. A CTest host-only
gate verifies the two real canonical inputs are accepted and both
malicious closing-delimiter variants are rejected with the intended
diagnostic. This does not replace compiler-backed guest evidence.

## Compiler binary pre/post identity

Both native guest test scripts now pin the SHA-256 of the actual Forge
executable **before** invoking its parser and native emitter, then
recheck it after running positive, mutation and negative cases.
An executable replaced during the tests causes a hard failure.
The pinned `ATTO_FORGE_STAGE{1,2}_COMPILER_SHA256` is printed
alongside the guest artifact digest. This supplies a more defensible
compiler binary witness but does not prove the compiler source
itself or provide independent native guest parity review.
