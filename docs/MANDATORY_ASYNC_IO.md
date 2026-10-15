# Mandatory nonblocking guest I/O

The language invariant is `native-async-only`; it has no opt-in/compatibility
mode. Forge currently lowers pure checked scalar arithmetic and exposes no
guest filesystem, HTTP, network or stdio binding. Unsupported operations,
aliases and dead-code examples reject before emitting IR or assembly. A fake
`_async` identifier cannot admit a blocking host call.

CTest `mandatory_async_io` runs the shared 22-case corpus through run, IR,
x86_64 assembly and ARM64 assembly paths and rejects policy-selection flags.
No output is accepted from a failed compilation. This is denial evidence for
this subset, not a complete effect system or implemented native async I/O.
Trusted compiler input/artifact loading and CLI result/diagnostic transport
remain host tooling, never guest bindings or worker fallbacks.
