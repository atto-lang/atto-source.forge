# Forge restricted allocation guest compiler v1

Stage status: experimental and draft. tools/forge_alloc.cpp compiles a bounded
subset of actual .atto guest source into C++20 and a native executable.
The admitted API calls are recognized compiler builtins, NOT parsed or linked
canonical .atto module bodies from atto-common-source. This does not prove
portable core/alloc/std module implementation or full Forge parity.

Admitted: one pub fnc main(): int throws Error; import alloc.byte_region
(optionally std.byte_ops); at most 16 region variables and 48 statements;
literal allocations, exclusive mutable write_byte, read_byte, length,
and bytes_equal return with ternary. Invalid callers are rejected before
code emission; direct OwnedBytes calls report E_SEALED_ALLOC.

Generated guest creates a fixed test-only arena principal (1,1,1) and
65536-byte quota. This is NOT an authenticated TenantVM context; a real
runtime must inject and verify actor/tenant/generation and enforce loans.
The CLI checks input size (16 KiB), tokens (255), bounded plans and
preflights paths, but output-path checks are not race-free.

Return convention: logical main result on stdout, OS process exit 0;
runtime failure exits: bounds=72 value=73 invalid_size=74 quota=75.
This remains distinct from unified cross-backend main exit ABI.

Test: cmake -S . -B build -DATTO_FORGE_SANITIZERS=ON
      cmake --build build && ctest --test-dir build -R alloc_guest_v1_compiled
Locally, the identical checked-in compiler blob was tested on 12 compiled
guest positives/error exits and 10 compile-negative cases (GCC O0).
The compiled equal-bytes native guest also passed GCC/Clang ASan/UBSan.
These are restricted guest results, not canonical .atto stdlib execution.
No exact-head source-organization Actions step execution is yet verified.

The frontend requires --experimental-host-profile before source/output arguments. Without this explicit opt-in it refuses code generation with E_ALLOC_EXPERIMENTAL_REQUIRED. This prevents accidental use as the production TenantVM compiler.
