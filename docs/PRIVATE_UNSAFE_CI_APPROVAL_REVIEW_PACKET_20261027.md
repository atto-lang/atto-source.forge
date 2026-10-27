# Oreslang `unsafe` G0 — private CI export review packet (2026-10-27)

**Proposal only. This document does not authorize any source export.** The current source policy is fail-closed, with zero approved repositories/snapshots and `independent-reviewer.enabled=false`. Do not copy private source into another organization on the basis of this document or the source-free runner canary.

## Review target: one exact canary (Forge first)

| Property | Verified value |
| --- | --- |
| Source | `ores-truffle-oreslang/oreslang-source.forge`, draft PR #114 |
| Source commit | `5fce70fbee1724999ba556b5facd47aefb8aceb2` |
| Exact source root tree | `ef9750ae9a124ae0df643f6793495a32ec559a88` |
| Source tree listing | 206 entries, 177 blobs, not truncated; no symlinks/submodules |
| Destination | `honeypot-r-us-test/hnpt-e2e` |
| Destination repository ID | `1346492776` |
| Destination visibility | Private |
| Prior source-free hosted Actions | [run #40511361307](https://github.com/honeypot-r-us-test/hnpt-e2e/actions/runs/40511361307), PASS with real steps |
| Security-owner issue | [#115](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/115) |

### Proposed policy records — NOT APPLIED

After an independent, authorized reviewer is selected and verified, owner-approved protected changes would list **exactly one** destination and **exactly one** source snapshot:

- `.github/private-ci-approved-repositories.txt`: `honeypot-r-us-test/hnpt-e2e 1346492776`
- `.github/private-ci-approved-source-snapshots.txt`: `5fce70fbee1724999ba556b5facd47aefb8aceb2 ef9750ae9a124ae0df643f6793495a32ec559a88 1346492776`

The protected independent-reviewer policy has schema 2; reviewer identity, numeric GitHub ID, write/maintain/admin permissions, required-check identity, CODEOWNERS review and source-branch protections **must be validated independently**. Never invent reviewer identity, populate placeholder reviewer IDs, bypass a failed check, or claim a zero-step workflow pass. If the approved PR head changes, redo the tree attestation and authorization.

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

The shared contract draft now contains **52 semantic scenarios**, including an explicit 128/129 nested-comment boundary across all five backends; this remains contract-only and unexecuted across most compilers. Forge now has 80 negative CLI-mode cases, 4 supported positive comment cases, 2 excessive-nesting rejections, and 8 malformed-source byte rejections specified in draft tests. These **94** checks now passed under the *actual local Forge CLI* built from the exact listed draft head under GCC 14.2, Clang 17, GCC+ASan/UBSan, and Clang+ASan/UBSan. Both GCC/Clang local CTest runs passed using a **minimal verification-only CMake project** calling the exact Git-blob-verified `unsafe_admission.py`, not the repository-wide upstream CMake test configuration. Six emitted native executables (3 assembly modes × 2 toolchains) were assembled, linked and executed, all returned 42. There is **no approved cross-org mirror, no full upstream repository-wide CI run, and no five-backend parity claim**. The exact callable-emitter initialization fix is `Native{m, {}, 0}` (new blob `93e8b39408385e47e6bbc2e73992dc21b1428721`). The target Forge tree was rechecked as a complete 177-blob listing, without submodules or symlinks.

LLVM #1082 now bounds nested source and template block comments to 128 levels, with ordinary/template overdepth negatives and an exact-boundary safe positive; these LLVM tests are **committed, not runtime verified**. Java #2148 now adds a 128-level nested block-comment budget and fail-closed regression tests; .NET #184 adds six line-ending forms, nesting-limit and invalid UTF-16/NUL cases across both parser entry points; WASM #150 has locally executed 6/6 unsafe-preflight tests, a 256-brace interpolation budget, and a deterministic 2,000-input lexical regression. **Those source-level facts are not backend compiler or Native AOT parity evidence.** Prior cross-org approval (if later obtained) must match the new exact heads, not an earlier snapshot.

### Evidence distinction

The existing alternate-carrier canary checks toolchain availability and independent source-free lexical expectations only. It **does not compile or run any Oreslang code**. Source-org zero-step workflow failures and source-free carrier success cannot satisfy release, sanitizer or AOT gates. All `unsafe` source PRs must stay draft until real executable exact-source evidence exists.
