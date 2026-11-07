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
