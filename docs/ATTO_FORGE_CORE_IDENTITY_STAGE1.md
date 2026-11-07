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
