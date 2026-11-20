# Forge functional-pipeline bootstrap compiler (bounded native subset)

**Local compiled native guests executed on GCC 14.2 and Clang 17; exact-head GitHub Actions/CTest admission remains not executed.**

The new `forge_functional` C++20 frontend compiles real Atto `.atto` source into deterministic C++ source, which GCC/Clang compile into native binaries. It is adjacent to the unchanged `forge_scalar` stage-0 compiler. It is **not** the production Forge JIT/AOT engine, an actor runtime or TenantVM sandbox.

Accepted syntax: signed int64-only named functions with up to eight args, `: int { return ...; }`, `: int -> expression;`, `-> int { return ...; }`, up to four immutable `val name = expr;` locals before the final explicit return, checked arithmetic `+`, `-` and `*`, named calls, and left-to-right `|>` plus right-associated `<|`. Arbitrarily mixed directions require parentheses. Calls are validated for symbol existence, argument count and cycles; local bindings reject future reads, duplicates and shadowing. Bounded source, token, recursion, function, argument, and output budgets apply. No guest expression bytes are injected into generated C++; only validated identifiers and typed integer literals are emitted.

Example compilation:

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DATTO_FORGE_SANITIZERS=ON
cmake --build build --parallel 2
build/forge_functional fixtures/functional-pipelines-v1/block-body.atto -o /tmp/functional-pipeline.cpp
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -I include /tmp/functional-pipeline.cpp -o /tmp/functional-pipeline
/tmp/functional-pipeline
# expected stdout: 83
```

Both exact user examples print `83\n`; nested right association and explicitly grouped directions print `42\n`. The Forge scalar CLI convention prints the result and returns OS status 0 (unlike the Rust/LLVM/.NET fixtures which may use OS exit status). Native overflow prints `E_OVERFLOW` to stderr and returns 70. Invalid sources return 2 without a generated C++ file.

The tests `tests/functional_pipelines.cmake` are authored to compile the four canonical native guests plus signed-i64 minimum, checked-overflow and filename-only-output guests, check deterministic generated outputs and reject 18 cases without output artifacts. Four positive and four negative sources are copied **byte-for-byte** from atto-spec#121's canonical fixture blobs. Existing Forge scalar compiler tests remain intact.

## Observed local native execution (not CI certification)

The Forge C++ frontend Git blob `c64c2ea8ed8c4bbb7fcf4024561cd57fd2d3aa97` and runtime header blob `e9a6598416c919f9ac7c629847f8d6677c703a14` were reconstructed byte-for-byte in a local Linux container, verified using `git hash-object`, and compiled with both GCC 14.2.0 and Clang 17.0.0 using `-std=c++20 -Wall -Wextra -Werror -pedantic`.

Both toolchains compiled and executed **all four** source-Git-blob-matched canonical Atto programs: guest stdout `83, 83, 42, 42`, OS exit 0. The same canonical four rejection sources returned type/syntax diagnostics, status 2, and no generated guest artifact. Both toolchains also compiled/ran signed `i64::MIN` correctly and rejected arithmetic overflow with `E_OVERFLOW`/exit 70. Separate `-fsanitize=address,undefined -fno-omit-frame-pointer` native builds of the six positive/boundary runtime cases produced expected results for both toolchains. Rejected out-of-range literals and forbidden authority keywords produced no output artifact. The updated compiler also accepted a filename-only `-o` output under the current working directory without clobbering existing output.

This verifies **local checked source-to-native guest execution** of the bounded Forge compiler. It does **not** verify an exact GitHub Actions run, all CMake/CTest jobs, production Forge JIT integration, TenantVM security, or full AttoLang parity. Those statuses remain `not_executed` in the conformance ledger.

## Source publication hardening

Generated C++ source now uses POSIX `open(O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)`, a bounded interrupted-write loop, and atomic no-replace hard-link publication. The compiler **never uses `O_TRUNC` on a preflight-checked temporary path**, so a competing regular file or symlink cannot be silently truncated at that pathname. GCC 14.2 and Clang 17 local runs reconfirmed all four exact canonical native guests and exercised rejection of existing `.part` files and symlinks; generated C++ files were mode 0600.

**Residual limitation:** build-directory ancestor races and attacker-controlled directory entries still require a separate directory-descriptor-anchored threat model and formal race stress gate. This bootstrap publisher supports POSIX platforms only; non-POSIX emission fails closed until equivalent atomic output APIs are implemented. Do not claim secure untrusted TenantVM guest execution from these filesystem checks.

## Verification and security boundaries

No performance, Native AOT, hard-real-time, full language, or multitenant security parity claim is made by this bounded path. In-process untrusted actors require a separate validated TenantVM memory/capability/CPU/I/O boundary, never the C++ emitter alone. Production integration and exact native guest execution logs on GCC/Clang with source/tree SHA remain required.

Related implementation: atto-source.rs#381, atto-source.llvm#54, atto-source.net#16, atto-source.java#51; contract atto-spec#121; evidence ledger atto-platform-conformance#112.


## Linux publication: descriptor-anchored, unnamed inode

The Linux guest-source publisher has been hardened beyond an exclusive named
temporary file. It opens the *actual output directory* with
`O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC`, creates an **unnamed** mode-0600
`O_TMPFILE` inode relative to that open directory, writes and fsyncs the
complete checked generated C++ to that still-open descriptor, and publishes
the **exact same inode** with `linkat(fd, "", dirfd, basename, AT_EMPTY_PATH)`.
The link is atomic and refuses an existing destination. A hostile process
cannot replace a path-based `.part` file to make the publisher link an
attacker-controlled symlink. If `O_TMPFILE` or descriptor-link publication
is unavailable on a Linux filesystem, output fails closed; it does not
silently fall back to vulnerable pathname publication.

No rollback removes a published output by its pathname; on a cleanup
error the build reports failure while preserving the linked artifact.
The macOS bootstrap retains an explicitly *trusted-directory only*
exclusive named temporary path; Windows is unsupported by this writer.

`tests/functional_publication_race.sh` (invoked from the native CTest
script on Linux) requires sixteen independent actual Forge CLI processes
to compete for one output, exactly one successful atomic publisher, fifteen
`E_OUTPUT` losers, mode 0600, deterministic independently generated C++
content, existing output and symlink no-clobber, and actual native guest
execution returning 83.

**Observed locally in the implementation environment:** the exact isolated
Linux anonymous-inode publication logic was compiled with GCC 14.2 and
Clang 17 under both ordinary C++20 and ASan/UBSan; all four
compiler/sanitizer configurations passed a 24-writer race (one winner,
23 losers), no-overwrite and symlink gates, 0600 output, and absence of
named temporary files. This was a **standalone syscall-logic probe**, not
a full newly compiled Forge CLI or CTest run. The new 16-writer
`tests/functional_publication_race.sh` is authored but still needs actual
compiler execution on the current PR head.

Remaining security boundary: untrusted process tenants must not be given
ambient host filesystem or C++ compiler access; the directory is a
trusted compiler-build destination, not a TenantVM storage API. Hostile
ancestor-directory replacement and differing Linux filesystems still
require dedicated adversarial review and exact-head CI.


## Source admission: one descriptor, one bounded inode

The standalone Forge compiler no longer validates a source pathname and
then reopens it via `std::ifstream`. It atomically opens the source once
with `O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK`, validates the
opened descriptor with `fstat` and `S_ISREG`, rejects inputs larger
than 16 KiB, reads through that same descriptor, and rejects short
reads or unexpected trailing bytes. The nonblocking open prevents
an adversarial path replacement with a FIFO from wedging the compiler
before its file-type check. Symlink substitution cannot redirect a
second open into an out-of-scope host file.

The Linux CMake suite now checks symlink source rejection, FIFO rejection
without blocking, and the 16 KiB limit. Local isolated C++ reproductions
of the exact descriptor read routine were compiled and executed with
**GCC 14.2 and Clang 17** under C++20, and again under ASan/UBSan.
All four compiler/sanitizer variants passed regular/symlink/FIFO/oversize/
empty-file cases. This is a **syscall routine unit probe**, not a fresh
full Forge guest-compiler pass.

Limit: an external actor that already holds *write permission to the same
opened inode* could still mutate its contents while they are read. Source
files in a hostile workspace must be staged immutably and identity-checked
by the host before guest compilation. This compiler is not a general
filesystem access capability for untrusted TenantVM actors.
