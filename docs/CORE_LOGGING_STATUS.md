# Core logging / OTel boundary — Forge / C++20

**Audit date:** 2026-10-16. **Status:** documented target only; implementation parity **not established**.

Shared design: [core logging and userland OTel contract](https://github.com/ores-truffle-oreslang/oreslang-common-source/blob/audit/core-logging-separation-2026-10-16/docs/CORE_LOGGING_AND_OTEL.md).

## Verified repository status

The checked arithmetic parser/interpreter and byte mailbox prototype do not expose a general guest stdlib or userland package import system. Java-style logging flags are not implemented.

## Required backend behavior

- Core/internal standard-library diagnostics are controlled by host CLI/configuration, disabled by default, and independent of explicit userland `oreslang-otel` imports.
- Future canonical core flags: `--core-log-level=off|error|warn|info|debug|trace` and `--no-core-log`. These are **not currently declared implemented for this backend**; do not advertise them as usable.
- Never perform guest message formatting, string interpolation, allocating event construction, filesystem/network export, or telemetry callbacks in a scheduler/actor/borrow checker critical path. Disabled paths must not sample the clock or evaluate an expensive payload.
- Keep guest stdout and functional errors unaffected by diagnostic settings. No guest payload, headers, credentials or arbitrary dynamic fields in core events.
- Version schemas; record numeric IDs and timestamps in bounded primitive buffers; export to host stderr only off the hot path.

## Implementation and test gates

Implement a bounded fixed-ID recorder in native host/compiler code, gated by a startup-parsed scalar severity threshold. Verify relaxed/acquire/release ordering if using atomics, memory limits, saturation/drop counters and no hot-path allocation. No C++ streams, formatting, dynamic strings or user callbacks in enabled critical paths. Reject unsupported guest OTel imports. Run CTest and native checks at an exact commit SHA.

Do not mark parity green until a compiled/linked artifact has passed negative admission tests, CLI configuration-precedence tests, saturation and concurrent publication tests, and disabled/enabled performance tests. Record the exact source tree and CI run.
