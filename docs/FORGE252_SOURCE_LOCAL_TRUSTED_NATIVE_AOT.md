# Forge PR #252 — manual source-local trusted native-AOT test gate

**Status: PROPOSED.** This runbook and workflow are not an executed compiler receipt and do not authorize private-source export.

## Bound purpose

Run the *real*, complete private Forge source tree under the **same GitHub source repository**, without copying it to an alternate organization. The target is [PR #252](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/252), stage-0 direct callable scalar AOT. This does **not** prove class/trait/actor/generic dispatch or five-backend parity.

This is distinct from [logical-word source-local workflow PR #246](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/246), which is deliberately restricted to Forge #241.

## Independent preconditions — administrator and code-owner

1. Enforce protected `main` branch/ruleset and independent `@ORESoftware` CODEOWNERS review of any workflows, verification scripts and policy files. Require the exact GitHub Actions app-bound `private-ci-policy` check and do not bypass it. At authoring, GitHub branch metadata reported `main.protected=false`. **Do not run/merge without protection.**
2. Independently review this workflow/verification-code PR and merge it **only after** its own code review and actually executed policy/security checks. Workflow exists only as reviewed `@main` code before it may be manually dispatched.
3. An org admin provisions a dedicated **ephemeral**, isolated source-org Linux x64 runner, registered with group `atto-forge-source-local-trusted` and labels `self-hosted, linux, x64, atto-ci-trusted`. Restrict the runner group to **only** the private `ores-truffle-oreslang/oreslang-source.forge` repo and **only** the reviewed `.github/workflows/forge252-source-local-trusted-native-aot.yml@main` workflow. Labels alone are insufficient; never run this PR on a generic shared persistent runner.
4. Keep the host VM/container free of production credentials, personal files, cloud instance identity, privileged sockets and production mounts. Register with ephemeral/JIT mode; no secrets or checkout token persist in the workspace. Prohibit unreviewed PR/push automatic triggers and arbitrary external caller workflows. Remove the runner and **destroy the VM and disks** after the single job; honor the independently approved retention policy. Registering the runner may require a GitHub org admin and is not possible with this GitHub connector.
5. Obtain genuine independent approval on the **exact current head** of Forge #252. The workflow's inline pre/post gate checks that the PR is open and same-repository, that its exact head+root Git tree match provided SHA values, and that a non-author human reviewer with confirmed effective write+ permission has an `APPROVED` review for that exact commit. Older approvals, dismissed approvals, bot approvals, approval by the author, and `CHANGES_REQUESTED` fail closed. `ORESoftware` may act as independent compiler code-owner reviewer if it is not the author; the separate private-**export** policy's denial list is not a restriction on source-local code review.

## Manual execution

After the workflow is independently reviewed and merged to protected `main`:

1. Re-read the current Forge #252 head from GitHub PR metadata and `git/commits/<SHA>` root tree. Do **not** reuse superseded values. Historical candidate as of authoring:
   - PR head: `ec796b3a8fba3f59ba588e5a0b8eec38f6e0cfee`
   - Root Git tree: `b9be7cf9bb72f57203e4a7253c9185587f243c84`
2. Run **Actions → Forge 252 source-local trusted native AOT (review required) → Run workflow** from branch `main` with:
   - `expected_head_sha`: current 40-character lowercase PR head
   - `expected_tree_sha`: matching 40-character lowercase Git root tree
3. The source-local runner first checks out **reviewed main** with `persist-credentials: false` and executes source-free adversarial admission regression tests; it then revalidates exact PR head/tree and independent privileged review **before** checking out PR code. If the approval or admin group is missing, source checkout must never happen.
4. Once admitted, checkout is credential-free and pinned to reviewed PR SHA. Full-tree Forge CMake build and full native CTest execute separately with g++ and clang++. The named `oresforge_static_dispatch_aot_source_and_ir` verifier (including its 13 authored negative IR/ABI scenarios) is required and run in verbose mode.
5. Emit actual direct x86-64 assembly from the real `.atto` source fixture `tests/fixtures/static-dispatch/direct-42.atto`; reject emitted indirect branches. Compile and **execute** it using GCC/Clang × `-O0/-O2`, with a native driver accepting only `oresforge_entry() == 42`.
6. Require four `FORGE252_NATIVE_AOT_PASS` receipt lines (`gcc/O0`, `gcc/O2`, `clang/O0`, `clang/O2`), compiler binary SHA256 and linked executable SHA256 digests, plus a *post-run revalidation* of the exact live PR head/tree/reviewer. These receipts are not sufficient unless GitHub records a real assigned runner ID, nonzero executed job steps, job ID, run attempt and successful conclusion.

The workflow neither uploads source/artifacts nor mirrors them across organizations. Never treat unrelated alternate-carrier admission proof, zero-step Actions, an old-head seven-file test subset, or local source inspection as a full-tree Forge run.

## Blocker handling and verification claims

- `steps=[]`, `runner_id=0`, or missing logs: **NOT_EXECUTED**; escalate Actions billing/runner policy/admission to the GitHub organization administrator, not to the compiler author.
- No independent exact-head review: **DENIED_BEFORE_CHECKOUT**; request a real eligible human review rather than adding a self-approval or weakening code.
- Missing trusted runner-group selected-workflow restrictions or unprotected main: **DO_NOT_DISPATCH**.
- Any PR-head change invalidates the input and all past receipts, requiring new review and a new execution.
- Failed native outputs or negative tests are actual test failures, not reasons to skip tests or rewrite expected outcomes.
- Source-local execution **does not** grant permission to transfer this private source into `honeypot-r-us-test` or any other organization. Cross-organization exports remain subject to the independent approved exact snapshot under issue [#274](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/274).

Tracking: [Forge #252](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/252), [CI admission #108](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/108), [private export authorization #274](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/274).
