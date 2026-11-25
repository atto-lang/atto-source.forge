# SocketBroker v1 — trusted host only

This C++20 POSIX component is not a hostile guest sandbox. It supports adopting already-connected IPv4/IPv6 TCP streams supplied by a separately authorized trusted host, followed by nonblocking read, write, close and tenant retirement. It does not open, connect, bind, listen, accept, resolve DNS, authorize endpoints or copy from guest memory. No guest compiler or TenantVM is connected to it.

## Security contract

- start_tenant mints a host-only TenantAuthority; a guest-provided tenant ID must never grant authority. Each retired tenant restart gets a new generation. Guest handles contain only logical, non-FD IDs; every operation additionally authenticates the host-owned TenantAuthority and socket rights.
- adopt_connected_tcp rejects foreign/stale authorities, invalid rights, quotas, non-TCP descriptor families, duplicate descriptors and aliases with the same live device/inode identity, non-TCP protocols and unconnected sockets. FD ownership transfers to the broker only on success. Admission fails closed when SO_PROTOCOL or usable socket identity is unavailable. A trusted caller must retain exclusive control of the descriptor lifecycle, verify actual peer/IP/port/DNS/TLS and enforce endpoint permission before admission. This initial adapter does not independently prove endpoint allowlisting.
- Reads and writes use nonblocking socket operations, bounded requests (64 KiB), per-tenant charged bytes and rights checks. Descriptor close-on-exec and SIGPIPE protection are enabled. Close/retirement serialize against I/O and reject stale handles.
- Input/output spans are trusted-host memory. A future guest call gate must validate bounded linear memory and copy data. Never pass raw guest pointers, guest-provided TenantAuthority or OS FDs directly to this API.
- No host reactor/cancellation queue, connection policy, untrusted code confinement or actual compiled Atto actor integration exists. Untrusted in-process admission stays DENIED/UNSUPPORTED until independently verified.

## Host tests

The positive native CTest profile is Linux; other hosts are not certified and admission rejects if socket protocol or identity cannot be checked.

    cmake -S . -B build -DATTO_FORGE_SANITIZERS=ON
    cmake --build build --target socket_broker_v1_test
    ctest --test-dir build -R '^socket_broker_v1_test$' --output-on-failure

The tests use real loopback TCP sockets to check read-only vs write-only tenants, cross-tenant denial, FD duplicate and alias rejection, AF_UNIX denial, byte and handle quotas, nonblocking/CLOEXEC, physical close, generation fencing and retirement-versus-I/O races. These are HOST_ONLY tests; they do not certify real malicious guest execution.

## Overlap review

The legacy [Oreslang Forge capability-I/O PR #142](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/142) has a host-only scoped TCP adapter and lifecycle checks; its endpoint-policy semantics remain complementary rather than replaced. Legacy [Forge TenantVM PR #296](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/296) contains actor memory region enforcement, not full hostile sandboxing. Reconcile those changes and preserve unique tests before merging.

Normative new Atto contract: [spec #49](https://github.com/atto-lang/atto-spec/pull/49). Guest execution registry: [conformance #43](https://github.com/atto-lang/atto-platform-conformance/pull/43).
