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
