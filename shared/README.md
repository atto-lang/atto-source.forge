# Shared counted-ready native profile

Explicit `--counted-ready-v1` compiler entry emits static C++ from a bounded Atto source, using the byte-identical shared frontend and native arbiter headers from atto-lang/atto-source.llvm commit `1324a3087ca1b3d90942883fcac040e22d40cdf5`. This is a shared profile, not independent Forge machine-code lowering. The normal Forge scalar/callable emitters remain unchanged. The emitted source is assembled and linked by the system C++ toolchain; no JIT or external producer is supported.

- `backend/CountedReadyGuest.hpp`: `f7d5a7b864ec2dbe443ac97d6ca4943445604b20`
- `runtime/NativeCountedI64Cursor.hpp`: `3a1b3ff4cf235b721edf9dc64c1af411a94a74ee`
- `runtime/NativeSelectArbiter.hpp`: `2d85351e66e55aae19eb5e6a08a434643c2dc4e0`
- `runtime/NativeSelectableI64Channel.hpp`: `bcb3a3eb9e0504f4bfa4808531a17126cacd0325`
- `runtime/NativeSelectableI64Future.hpp`: `0c972dc190fc25533792a928461298492ba3b5ea`
