# Event-sourced durable command wire v1 — Forge host

**Host-only verified protocol subset:** `runtime/durable_command_wire_v1.hpp`
implements the same big-endian length-delimited event batch as Java/LLVM:
1–256 immutable owned event values, 64KiB maximum per event, 1MiB maximum
batch, 64KiB maximum request/reply. It rejects truncation, excess length,
empty/oversized event counts, zero schema and trailing bytes. It never
treats a host-provided operation ID as a trusted capability.

`verification/durable_command_wire_v1_test.cpp` is a strict native
C++20 host test with golden vector, copy ownership and adversarial inputs.
It is registered in CMake and isolated CI. This wire codec does not
implement a disk journal, transactional inbox, actor scheduler, compiler
sealing, pure reduction, supervisor replay, or executed AttoLang guest.
Forge's compiler remains an experimental scalar bootstrap; unsupported
event-sourced actor guest programs must not be claimed as accepted.

**Evidence:** local GCC native host executable can establish wire parity;
production Forge guest and durable storage parity remain NOT_EXECUTED.
The source org's GitHub Actions runner must execute real nonzero steps;
do not mirror private source into an unapproved CI carrier.
