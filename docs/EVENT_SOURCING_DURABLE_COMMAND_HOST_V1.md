# Forge native durable command inbox v1 — draft

This is a trusted, synchronous C++20 SQLite WAL/FULL **host provider**, not
a compiled AttoLang actor or a certified Forge/Cranelift lowering.

`runtime/durable_command_inbox_v1.hpp` adds exact actor identity/slot
fencing; a bounded immutable request and reply (64 KiB each); canonical
big-endian framing of 1–256 events (64 KiB each, 1 MiB total); a retained
operation ledger, contiguous journal versions and a SQLite transaction
atomically persisting head, event rows, canonical request and original reply.

With a newly granted activation epoch, `resolve()` verifies the original
command bytes and all retained event rows before yielding its original reply.
Same operation ID with changed request fails closed, and stale incarnations
cannot read or write using an old epoch. Event records and replies are owned
copies rather than pointers into another actor heap.

`verification/durable_command_inbox_v1_test.cpp` exercises actual SQLite,
including second-event-trigger rollback, process-handle close/reopen,
corrupted-journal rejection, and cross-epoch duplicates. The test can be
compiled independently with a C++20 compiler and libsqlite3; CTest is
registered only when SQLite3 is found.

**Host scope and limitations:** no trusted actor-turn binding, storage worker
scheduler, compiler-derived pure reducer, sealed actor state, guest .atto
execution, OS tenant isolation, Cranelift output, Native AOT proof or
supervisor recovery. Backend parity status remains **NOT_CERTIFIED**.

Local host execution is evidence only for exact locally compiled source
blobs. GitHub CI status must be evaluated by executed steps, not the run
conclusion; do not mirror private code outside an independently approved
private carrier and protected publisher.
