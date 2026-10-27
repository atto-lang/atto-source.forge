# Oreslang `unsafe` G0 — private CI export review packet (2026-10-27)

**Proposal only. This document does not authorize any source export.** The current source policy is fail-closed, with zero approved repositories/snapshots and `independent-reviewer.enabled=false`. Do not copy private source into another organization on the basis of this document or the source-free runner canary.

## Review target: one exact canary (Forge first)

| Property | Verified value |
| --- | --- |
| Source | `ores-truffle-oreslang/oreslang-source.forge`, draft PR #114 |
| Source commit | `24c4c4649fa5b82a88ced0761f2808a90b689f30` |
| Exact source root tree | `fb400d7eff6e3ddad5de14f5c9b034077a2a61bd` |
| Source tree listing | 206 entries, 177 blobs, not truncated; no symlinks/submodules |
| Destination | `honeypot-r-us-test/hnpt-e2e` |
| Destination repository ID | `1346492776` |
| Destination visibility | Private |
| Prior source-free hosted Actions | [run #40511361307](https://github.com/honeypot-r-us-test/hnpt-e2e/actions/runs/40511361307), PASS with real steps |
| Security-owner issue | [#115](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/115) |

### Proposed policy records — NOT APPLIED

After an independent, authorized reviewer is selected and verified, owner-approved protected changes would list **exactly one** destination and **exactly one** source snapshot:

- `.github/private-ci-approved-repositories.txt`: `honeypot-r-us-test/hnpt-e2e 1346492776`
- `.github/private-ci-approved-source-snapshots.txt`: `24c4c4649fa5b82a88ced0761f2808a90b689f30 fb400d7eff6e3ddad5de14f5c9b034077a2a61bd 1346492776`

The protected independent-reviewer policy has schema 2; reviewer identity, numeric GitHub ID, write/maintain/admin permissions, required-check identity, CODEOWNERS review and source-branch protections **must be validated independently**. Never invent reviewer identity, populate placeholder reviewer IDs, bypass a failed check, or claim a zero-step workflow pass. If the approved PR head changes, redo the tree attestation and authorization.

### Approval prerequisites

1. Security owner confirms cross-organization source transfer is permitted for this **private destination**, controls access/retention, and appoints an eligible independent reviewer.
2. Authorized reviewer approves the exact source snapshot and destination ID on protected policy files; preserve the required `private-ci-policy` check issued by GitHub Actions app ID 15368 and CODEOWNERS controls.
3. Confirm the approved policy is effective on the intended protected source revision before copying source.
4. Source and target Git tree operations must be driven by the GitHub connector, not by a destination `GITHUB_TOKEN` with cross-org source access. Reject paths/modes not represented in the source tree, symlinks, submodules, truncated listings and blob/content hash mismatches; prove reconstructed root tree is byte-for-byte identical.
5. Run exact-head Forge CMake/CTest `unsafe_source_fail_closed` with 49 negative CLI/mode combinations; require reject status, explicit diagnostics and no successful output. Add scalar conformance, positive safe execution, GCC/Clang, ASan/UBSan and AOT coverage as supported. Capture exact run/job ID, compiler versions and immutable commit/tree receipt.
6. Delete temporary private carrier branch/artifacts after review-approved retention and record cleanup.

### Four other backends

The protected Forge policy permits only one active canary. Evaluate further mirrors only as **separately approved, sequential** source-to-destination exports:

| Backend | Source commit | Root tree |
| --- | --- | --- |
| Java #2148 | `53601ca7861602b891d99b2a1f376f322f7f4d92` | `d06fdddaf76cfc7ba303aa60ac2776b4eadef8d4` |
| LLVM #1082 | `7da66554bb59f07fc8678969f45023c162cea5e2` | `420c6dbe3807e840f105cefe4181901c90bf8b60` |
| .NET #184 | `b7f8e68e62a646cff295417116730c0777d2cb6b` | `066b380a09228ac955b34e95f7d5a49d54808144` |
| WASM #150 | `ba60dd4bdadd3c3cc075f97609c7d82fa1b07b8d` | `f5bd0d402805df601a3f5f628d883007baf246a2` |

Shared contracts #170 at `4e1bd2a7933a294669646f5a6acf40f5f67a83dc` / tree `5ec7eb290a630acc68257be13448cba78730c3e7` likewise need explicit authorization if copied.

### Evidence distinction

The existing alternate-carrier canary checks toolchain availability and independent source-free lexical expectations only. It **does not compile or run any Oreslang code**. Source-org zero-step workflow failures and source-free carrier success cannot satisfy release, sanitizer or AOT gates. All `unsafe` source PRs must stay draft until real executable exact-source evidence exists.
