# std.process — native Forge JIT/AOT engine


## Public API naming: asynchronous by default

In AttoLang, non-parking asynchronous operations are the **default** and
their names have no `_async` suffix: `spawn`, `collect_output`, `wait`,
`terminate`, and `kill`. The call sites may use `await child.wait()`
and `await child.collect_output()`. An intentionally synchronous/blocking
variant, if ever exposed, **must** end in `_blocking`. This is a public
semantic contract, not evidence that this backend implements OS process
launch or guest execution. Browser WebAssembly and synthesizable HDL remain
unsupported unless a separately authorized host provider exists.


## Public library path contract (2026-11-22)

Atto's preferred public import is `std/process`; `atto.std/process`
is the explicit, disambiguating built-in import. Apply the same rule to
`std/process_policy` and `atto.std/process_policy`. Do **not** publish
`ores.std/process` or `ores.std/process_policy` as new import names.
The two accepted spellings must resolve to the same compiler-owned module
identity and source, not two copies or an unchecked filesystem path.
The `atto.std/*` root must be reserved against local-package shadowing.

This backend's current relative fixture imports and source-only policy
**do not establish that either hosted import alias is implemented** in its
compiler. Resolver registration, positive/negative guest tests and an
exact-head execution receipt remain required before calling namespace
parity implemented.


This backend owns `stdlib/process_policy.atto` and its own import-based
`tests/fixtures/std-process-policy-v1/main.atto` expected-exit-42 fixture.
They contain pure guest admission checks only. Their success never grants
process creation, permission to execute a shell, or a real OS handle.

The public target is an async `process.spawn(Command)` returning an owned
`Result<ChildProcess,ProcessError>`, `wait()`, `terminate()`, `kill()`,
stdin/stdout/stderr bounded streams and a bounded `run()` convenience API.
The host must authenticate RUN, perform canonical command and environment
admission at the actual spawn boundary, and preserve actor-local permission
attenuation. Return denial, spawn failure, timeout and ordinary exit as
**distinct** outcomes. Avoid PID reuse and leaking raw host process objects.

## Forge native provider (not implemented)
Use native `posix_spawn` on Unix and `CreateProcess` on Windows, with explicit executable paths, bounded argv/env and no implicit shell. Bind the child to an actor-owned generation-checked handle and quota reservation; enforce RUN and cwd/env/filesystem rights before starting a process. Pipe stdout/stderr concurrently using nonblocking reactor I/O; cleanup must close stdin, request termination, escalate after bounded grace and reap exactly once. Do not call blocking wait on a carrier; do not treat a host C++ model test as compiled Forge guest execution. Hard-real-time and freestanding targets reject this hosted module.

## Verification status

The `.atto` policy and fixture are source-only. The backend has not proven
parse/typecheck, linked import, real guest execution, AOT, an OS spawn,
stream/cancellation lifecycle or exact-head CI for this change. Mark each as
**NOT EXECUTED / NOT VERIFIED**, not as full `std.process` parity.
