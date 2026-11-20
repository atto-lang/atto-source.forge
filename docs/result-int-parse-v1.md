# Forge checked Result parser foundation v1

HOST_ONLY, NOT_SOURCE_COMPILED. This is not source parser or native guest codegen.
The scalar frontend does not admit Result, Option, ?, trap, or tagged match.
This host-only parser uses a 1,024-byte strict ASCII signed decimal grammar
and a tenant-local monotonically increasing Error identity. It returns
tagged Err for malformed/overflow/oversize input and preserves a copied
Err identity; quota exhaustion remains fatal. The C++ result shape is not an
approved cross-tenant or cross-backend wire ABI.

The existing 32-byte source-token decimal lexer remains unchanged; this
new runtime helper must only be wired into a real compiler after a typed
HIR/MIR and validated guest ABI implementation. See spec #124, platform
conformance #68, compiler parity #20 and the Rust/Wasm candidate #331.
C++ host tests alone do not certify compiled Atto guest behavior.
