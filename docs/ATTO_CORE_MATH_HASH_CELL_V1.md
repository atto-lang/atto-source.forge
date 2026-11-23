# Forge core math/hash/cell candidate (source-owned)

The Forge compiler currently runs a restricted scalar-only guest profile.
These three `library/atto.core/*.atto` modules are real AttoLang source,
not implemented in host C++, but **are not yet resolved or linked** by Forge
as compiled imports. A Python source-free test pins their Git blobs, checks
golden hash vectors and denies host-interoperability substitution. It is
not an AOT guest test.

The `contracts/core-math-hash-cell-v1/guest.atto` candidate is a future
test. Promotion demands parser -> checked IR -> native object emission ->
compiled guest return 42 plus negative import/path/borrow cases on exact
head under GCC/Clang O1/O2 and ASan/UBSan where meaningful. Keep imports
fail-closed until that path is available.

Core math here covers only checked i64 min/max/signum and closed bounds;
hash is noncryptographic 31-fold modulo 1,000,000,007 with checked bytes;
cell is a pure policy, NOT Cell<T>, not RefCell<T>, no runtime loans.
No cross-backend guest parity is certified.
