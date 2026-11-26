# Oreslang `unsafe` G0 — private CI export review packet (2026-10-27)

**Proposal only. This document does not authorize any source export.** The current source policy is fail-closed, with zero approved repositories/snapshots and `independent-reviewer.enabled=false`. Do not copy private source into another organization on the basis of this document or the source-free runner canary.

## Review target: one exact canary (Forge first)

| Property | Verified value |
| --- | --- |
| Source | `ores-truffle-oreslang/oreslang-source.forge`, draft PR #114 |
| Source commit | `3a7176f404244fcc936ff6d6cb4c57852cdd2495` |
| Exact source root tree | `c837ce9d05bdedeb43f8017a8b63dc0fb6b073f7` |
| Source tree listing | 207 entries, 178 blobs, not truncated; no symlinks/submodules |
| Destination | `honeypot-r-us-test/hnpt-e2e` |
| Destination repository ID | `1346492776` |
| Destination visibility | Private |
| Prior source-free hosted Actions | [run #40511361307](https://github.com/honeypot-r-us-test/hnpt-e2e/actions/runs/40511361307), PASS with real steps |
| Security-owner issue | [#115](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/115) |

### Proposed policy records — NOT APPLIED

After an independent, authorized reviewer is selected and verified, owner-approved protected changes would list **exactly one** destination and **exactly one** source snapshot:

- `.github/private-ci-approved-repositories.txt`: `honeypot-r-us-test/hnpt-e2e 1346492776`
- `.github/private-ci-approved-source-snapshots.txt`: `3a7176f404244fcc936ff6d6cb4c57852cdd2495 c837ce9d05bdedeb43f8017a8b63dc0fb6b073f7 1346492776`

The protected independent-reviewer policy has schema 2; reviewer identity, numeric GitHub ID, write/maintain/admin permissions, required-check identity, CODEOWNERS review and source-branch protections **must be validated independently**. Never invent reviewer identity, populate placeholder reviewer IDs, bypass a failed check, or claim a zero-step workflow pass. If the approved PR head changes, redo the tree attestation and authorization.


**Updated 2026-10-27, new draft head above:** A source-free CTest `unsafe_source_harness_selftest` is now registered in the upstream `CMakeLists.txt` and uses verified script blob `a44e7d9b6d87223f21735e3441d6d5b1ffa49891`. It passed **1/1 locally** using a minimal CMake registration after exercising all 120 intended subprocess paths and catching six deliberate failure mutations. This **does not** replace the still-pending 120-case real `oresforge` admission test. The full upstream native compiler, actor runtime, and JIT CTests remain unexecuted on this new head.

**Updated 2026-10-27, new draft head above:** `verification/unsafe_admission.py` now defines **120 checks**: 80 rejected unsafe mode/cases, 4 safe comment cases, 2 depth-129 rejects, **2 depth-128 accepts**, and **32 malformed-byte mode cases** covering all 8 CLI entry points. The test file's new Git blob is `a0e87636a01fde8fc1fff28ef542e3dbd3e068d6`. The earlier 94-case local execution evidence was on the previous immutable head; **the 120-case suite has not been executed against the new compiler commit**. Do not reuse an earlier pass as a new exact-head pass. Only after independent approval of the revised snapshot may the private cross-org carrier transfer proceed.

### Approval prerequisites

1. Security owner confirms cross-organization source transfer is permitted for this **private destination**, controls access/retention, and appoints an eligible independent reviewer.
2. Authorized reviewer approves the exact source snapshot and destination ID on protected policy files; preserve the required `private-ci-policy` check issued by GitHub Actions app ID 15368 and CODEOWNERS controls.
3. Confirm the approved policy is effective on the intended protected source revision before copying source.
4. Source and target Git tree operations must be driven by the GitHub connector, not by a destination `GITHUB_TOKEN` with cross-org source access. Reject paths/modes not represented in the source tree, symlinks, submodules, truncated listings and blob/content hash mismatches; prove reconstructed root tree is byte-for-byte identical.
5. Run exact-head Forge CMake/CTest `unsafe_source_fail_closed` with 80 negative CLI/mode combinations; require reject status, explicit diagnostics and no successful output. Add scalar conformance, positive safe execution, GCC/Clang, ASan/UBSan and AOT coverage as supported. Capture exact run/job ID, compiler versions and immutable commit/tree receipt.
6. Delete temporary private carrier branch/artifacts after review-approved retention and record cleanup.

### Four other backends

The protected Forge policy permits only one active canary. Evaluate further mirrors only as **separately approved, sequential** source-to-destination exports:

| Backend | Source commit | Root tree |
| --- | --- | --- |
| Java #2148 | `1cb80acfb11a8fca3edd4946435e943278ed32eb` | `24fb9ea9799093b8fe0c95b7edb16a642cb3f62d` |
| LLVM #1082 | `1eed545b6b810d7f89e5e3ff4144b49a9ae445b8` | `225601b58e59ca0856ed35c6ca0f21255bee7f00` |
| .NET #184 | `1010d1056274ec63b7d41e64a15f88d391ac3650` | `9da6d88af39deae4ffbda6156818522b0bc49ba5` |
| WASM #150 | `b77963befd17cea056270ba31854f25ce066f621` | `42a162b79d69ed8f503e7310f8d46b40b4c309dd` |

Shared contracts #170 at `3b74ede7f8a48ade58519a12eb2ebd17dbb64e18` / tree `0420527af53ee1a74ae6de90ed468265544a2933` likewise need explicit authorization if copied.

The shared contract draft now contains **52 semantic scenarios**, including an explicit 128/129 nested-comment boundary across all five backends; this remains contract-only and unexecuted across most compilers. Forge now has 80 negative CLI-mode cases, 4 supported positive comment cases, 2 excessive-nesting rejections, and 8 malformed-source byte rejections specified in draft tests. At the **previous** Forge head `5fce70fbee1724999ba556b5facd47aefb8aceb2`, these **94** checks passed under the *actual local Forge CLI* under GCC 14.2, Clang 17, GCC+ASan/UBSan, and Clang+ASan/UBSan. Both GCC/Clang local CTest runs passed using a **minimal verification-only CMake project** calling the exact Git-blob-verified `unsafe_admission.py`, not the repository-wide upstream CMake test configuration. Six emitted native executables (3 assembly modes × 2 toolchains) were assembled, linked and executed, all returned 42. There is **no approved cross-org mirror, no full upstream repository-wide CI run, and no five-backend parity claim**. The exact callable-emitter initialization fix is `Native{m, {}, 0}` (new blob `93e8b39408385e47e6bbc2e73992dc21b1428721`). The target Forge tree was rechecked as a complete 178-blob listing, without submodules or symlinks.

LLVM #1082 now bounds nested source and template block comments to 128 levels, with ordinary/template overdepth negatives and an exact-boundary safe positive; these LLVM tests are **committed, not runtime verified**. Java #2148 now adds a 128-level nested block-comment budget and fail-closed regression tests; .NET #184 adds six line-ending forms, nesting-limit and invalid UTF-16/NUL cases across both parser entry points; WASM #150 has locally executed 6/6 unsafe-preflight tests, a 256-brace interpolation budget, and a deterministic 2,000-input lexical regression. **Those source-level facts are not backend compiler or Native AOT parity evidence.** Prior cross-org approval (if later obtained) must match the new exact heads, not an earlier snapshot.

### Exact-source local compiler G0 receipt (not cross-org CI)

At current draft Forge PR #114 commit `3a7176f404244fcc936ff6d6cb4c57852cdd2495` / root tree `c837ce9d05bdedeb43f8017a8b63dc0fb6b073f7` (complete 178-blob listing), the actual `oresforge` CLI was built locally from ten production source/header Git blobs plus the exact unmodified `verification/unsafe_admission.py` file. All **11 blobs** matched GitHub via `git hash-object` before compilation.

- **GCC 14.2 / ASan + UBSan:** 94/94 G0 CLI admission tests passed (80 negative, four safe, two depth, eight malformed-source), plus 3/3 emitted x86-64 programs assembled/linked/executed returning 42 and 2/2 supplemental depth-128 lexical checks. Compiler binary SHA-256: `a8863686f7b182d057231d374dee1a1e4e39c8dc31102ebf89ef908b3e93432b`.
- **Clang 17 / ASan + UBSan:** same **94/94 + 3/3 + 2/2** passed. Binary SHA-256: `2e61966a5d436688ee661fa241f775b4139b39f8ef6a1ff1fc0c01378eb1d7b6`.
- No sanitizer findings or compiler warnings. The current `compiler/call_native.cpp` exact blob is `93e8b39408385e47e6bbc2e73992dc21b1428721`; the full local suites were rerun after its update.

**Scope limit:** this is execution of the *real compiler CLI and exact Python suite directly*, not a full upstream 177-blob CMake/CTest run or official GitHub Actions evidence. It does not prove G1/G2, private CI authorization or other backend parity. Independent source-export security issue #115 remains open and the protected allowlists/reviewer policy must stay unchanged.

### Evidence distinction

The existing alternate-carrier canary checks toolchain availability and independent source-free lexical expectations only. It **does not compile or run any Oreslang code**. Source-org zero-step workflow failures and source-free carrier success cannot satisfy release, sanitizer or AOT gates. All `unsafe` source PRs must stay draft until real executable exact-source evidence exists.
