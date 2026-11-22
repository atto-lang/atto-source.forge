# std/env v1 — Forge JIT/AOT

`stdlib/env.atto` is a **backend-owned source candidate**, not a tested or installed standard library.

## Public contract

`get`: Option<String>; `has`: bool; `get_or`: String; `get_required`: Result<String,String>; `get_nonempty`: Option<String>; `get_required_nonempty`: Result<String,String>; `parse_bool`: Result<bool,String>; `get_bool`: Result<bool,String>; `get_bool_or`: Result<bool,String>.

Unsets are None; present empty strings remain Some("") except in explicit nonempty helpers. Only exact `true`, `false`, `1`, `0` are accepted booleans. Missing, denied, and malformed values are distinct. A fallback never bypasses a denial or masks malformed present values. Do not leak values, enumerate keys, mutate process state, or authorize host effects through .atto code.

**Blocker:** No verified String, Option/Result, or env.get/has module compilation on scalar Forge. An AOT-declared bounded host intrinsic and permission admission are prerequisites.

A minimal host interface must check coarse isolate ENV grants and scoped key allow/deny (denies win) before ANY lookup, validate key bytes, bound resource use, and return owned strings. Native AOT/guest execution and denial-before-read tests are required. The included fixtures are only source-level targets, including `pure_bool.atto` (expected exit 42 after real guest execution) and `denied_fallback.atto` (must fail before fallback with key denied). **Status: NOT_EXECUTED.**
