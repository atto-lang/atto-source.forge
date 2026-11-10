# AttoLang logical-word Forge tests — source-local trusted runner runbook

**State on 2026-11-10:** workflow proposed in PR #246, not merged or dispatched. Source-org GitHub-hosted jobs are blocked before runner admission. This runbook cannot certify tests and does not authorize any private-source export.

## Security gate (before enabling)

1. Have an independent CODEOWNER review the **manual-only** workflow in Forge PR #246. It must remain on protected `main` before `workflow_dispatch` is available. Do not add a `pull_request` or `push` trigger to the self-hosted job.
2. An organization/repository administrator must check GitHub Actions settings, billing/spending restrictions, runner groups, labels and access to this **private** repository. The ChatGPT GitHub App cannot query billing, enumerate runners or register a runner. A job with `runner_id=0` and `steps=[]` is a *pre-runner admission failure*; it does not identify billing as the root cause.
3. Prefer an organization-managed, **ephemeral** dedicated VM (one job, destroyed and reprovisioned afterward), or equivalently strong isolated runner. Restrict its runner group to `ores-truffle-oreslang/oreslang-source.forge` only and give it labels `self-hosted`, `linux`, `x64`, **`atto-ci-trusted`**. Use a dedicated unprivileged runner account. Do not mount personal credentials, cloud identity, Docker host sockets, production secrets, or sensitive host volumes. Restrict outgoing network access to what GitHub API/checkout and approved dependencies require. Protect and expire registration credentials; never paste them into PR comments.
4. Use `config.sh --ephemeral` (or an equivalent just-in-time runner), preserve runner diagnostic logs under a protected retention policy, and **destroy the entire runner VM/image after this one job**. A runner merely disappearing from GitHub's inventory is not proof its filesystem was wiped. No long-lived shared runner is an acceptable default for this untrusted compiler toolchain.

Official GitHub background: [Manual workflow dispatch](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow), [ephemeral self-hosted runners](https://docs.github.com/en/actions/reference/runners/self-hosted-runners), [self-hosted runner security](https://docs.github.com/en/actions/reference/security/secure-use).

## Execute exact live PR head

1. Re-read [Forge logical-word PR #241](https://github.com/ores-truffle-oreslang/oreslang-source.forge/pull/241), record its current head SHA, and independently read `HEAD^{tree}`. A superseded SHA or tree must not be represented as the PR's current exact head.
2. Once PR #246 has been reviewed and merged into `main`, open **Actions > AttoLang Forge logical words source-local trusted tests (NOT guest parity) > Run workflow** on `main`. Supply:
   - `expected_head_sha`: current Forge #241 PR head (historically `e19b4934744c0dad762b63cdb27b133f54a8b85b`).
   - `expected_tree_sha`: matching tree (historically `b2dea768508950706ff6618ef5147ba31bf77e37`).
3. The first step uses a **read-only token** to confirm the PR is open, same-repo, and exactly the supplied head SHA. Only then does `actions/checkout` fetch that immutable commit with `persist-credentials: false`. The second gate compares actual checkout commit and Git tree to both inputs. All source remains in the original Forge repo's organization and on its dedicated source-local runner.
4. Expected test stages: GCC build + at least three `^oresforge_logical_word` CTests; Clang build + the same CTests; Clang ASan/UBSan build + the same CTests. The CTest targets include direct-IR reservation, CLI no-publication checks and Python optimized (`-O`) checks. A failed stage leaves the run **failed**, never partially verified.
5. Require the **latest attempt's** GitHub job to show `runner_id > 0`, `steps.length > 0`, completed successful named steps, the exact source head/tree, CTest summaries, real compiler binary SHA-256, and no unwanted IR or executable publication on compile rejects. Re-check PR head after the run before accepting it as current evidence. An old-head success does not verify a moved PR.

## Scope / non-claims

- This workflow executes the real **Forge stage-0** compiler's rejection-only logical-word tests. It does **not** implement or establish typed bool CFG, `and/or` lazy guest execution, or `xor` eager guest execution. Do not promote it to five-backend or guest parity.
- Running an isolated compiler-verifier harness, source-free carrier smoke, or manifest hash check is insufficient to pass the source-compiler gate.
- A separate independent security review is still mandatory before **any** cross-organization private source transfer; this source-local plan needs **no export** and does not relax `.github/private-ci-*` policies.
- Keep canonical `.atto` source fixtures; legacy `.ores` fixtures remain supported.
- If runner provisioning cannot be approved, leave Forge #241 and Conformance #658 as unverified drafts and use [Forge infrastructure issue #108](https://github.com/ores-truffle-oreslang/oreslang-source.forge/issues/108) to record the actual administrative blocker.
