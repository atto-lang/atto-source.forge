"""Source-free mock GitHub regression tests for the actual protected-main workflow gate.

Tests execute the Python extracted from BOTH workflow admission steps; they do
not execute compiler guest code and do not access the network.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import re
import unittest
from urllib.parse import parse_qs, urlparse
from unittest.mock import patch

REPO = "atto-lang/atto-source.forge"
REPO_ID = 1448701874
PR_NUMBER = 207
HEAD = "a" * 40
TREE = "b" * 40
PATH = Path(__file__).resolve().parents[1] / ".github/workflows/atto-logical-words-source-local-trusted.yml"

class HttpResult:
    def __init__(self, data):
        self.data = data
    def __enter__(self):
        return self
    def __exit__(self, *_):
        return False
    def read(self, *_):
        return json.dumps(self.data).encode("utf-8")

def workflow_gates():
    source = PATH.read_text(encoding="utf-8")
    matches = re.findall(r"(?m)^          python3 - <<'PY'\n(.*?)\n          PY$", source, re.S)
    if len(matches) != 2:
        raise AssertionError("exactly two protected-main pre/post admission gates required")
    scripts = ["\n".join(line[10:] for line in block.splitlines()) for block in matches]
    if scripts[0] != scripts[1]:
        raise AssertionError("pre-test and post-test authorization diverged")
    # A workflow_dispatch presence check is not sufficient: reject any
    # additional schedule/workflow_run/repository_dispatch trigger.
    if len(re.findall(r"(?m)^on:", source)) != 1:
        raise AssertionError("only one manual dispatch trigger section is permitted")
    trigger = re.search(r"(?ms)^on:\n(.*?)^permissions:", source)
    if not trigger:
        raise AssertionError("only manual dispatch is permitted")
    events = re.findall(r"(?m)^  ([A-Za-z_][A-Za-z_0-9-]*):", trigger.group(1))
    if events != ["workflow_dispatch"]:
        raise AssertionError("unexpected GitHub workflow event trigger")
    # Validate the actual YAML sections, not words found elsewhere in comments
    # or unused jobs. No token write scopes, privileged dispatch, or fallback
    # to GitHub-hosted/shared runner admission may be silently introduced.
    permissions_match = re.search(r"(?ms)^permissions:\n(.*?)^concurrency:", source)
    if not permissions_match:
        raise AssertionError("workflow token permissions section required")
    permissions = [line for line in permissions_match.group(1).splitlines()
                   if line.strip() and not line.lstrip().startswith("#")]
    if permissions != ["  contents: read", "  pull-requests: read"]:
        raise AssertionError("workflow token may only read contents and pull requests")
    jobs_match = re.search(r"(?ms)^jobs:\n(.*)\Z", source)
    if not jobs_match:
        raise AssertionError("expected trusted source-local job missing")
    job_names = re.findall(r"(?m)^  ([A-Za-z_][A-Za-z_0-9-]*):",
                           jobs_match.group(1))
    if job_names != ["native-reviewed-source-full-suite"]:
        raise AssertionError("only one trusted source-local job permitted")
    job_match = re.search(
        r"(?ms)^  native-reviewed-source-full-suite:\n(.*?)(?=^  [A-Za-z_][A-Za-z_0-9-]*:|\Z)",
        source)
    if not job_match:
        raise AssertionError("expected trusted source-local job missing")
    job_source = job_match.group(1)
    if re.search(r"(?m)^    permissions:", job_source):
        raise AssertionError("job cannot override read-only workflow token permissions")
    expected_runner = ("    runs-on:\n"
                       "      group: atto-forge-source-local-trusted\n"
                       "      labels: [self-hosted, linux, x64, atto-ci-trusted]\n")
    if job_source.count(expected_runner) != 1 or \
            len(re.findall(r"(?m)^    runs-on:", job_source)) != 1:
        raise AssertionError("job must use only dedicated approved runner group")
    if "    if: github.repository == 'atto-lang/atto-source.forge' && github.ref == 'refs/heads/main'\n" not in job_source:
        raise AssertionError("source-local job must be main-and-repository scoped")
    # Inspect EVERY checkout. One safe checkout does not make a second,
    # credential-persisting checkout safe on a privileged ephemeral runner.
    # A checkout step may use "- uses:" or "- name:" then "uses:".
    # Inspect both styles and reject any extra checkout step.
    step_bodies = re.findall(
        r"(?ms)^      - (.*?)(?=^      - |\Z)", source)
    checkouts = []
    for body in step_bodies:
        uses = re.findall(
            r"(?m)^(?:uses:|        uses:) actions/checkout@([^\n]+)$", body)
        if len(uses) > 1:
            raise AssertionError("checkout step must have exactly one action")
        if uses:
            checkouts.append((uses[0], body))
    if len(checkouts) != 2:
        raise AssertionError("exactly two pinned source-local checkouts required")
    expected_refs = ["github.sha", "inputs.expected_head_sha"]
    for (checkout_action, checkout), expected_ref in zip(checkouts, expected_refs):
        if not re.fullmatch(r"[0-9a-f]{40}(?: #.*)?", checkout_action):
            raise AssertionError("all checkout actions must use immutable pinned SHA")
        if not re.search(r"(?m)^          persist-credentials: false$", checkout) or \
                not re.search(r"(?m)^          clean: true$", checkout):
            raise AssertionError("every checkout must erase and deny credentials")
        ref_line = "          ref: " + "$" + "{{ " + expected_ref + " }}"
        if not re.search(r"(?m)^" + re.escape(ref_line) + r"$", checkout):
            raise AssertionError("checkout ref must be immutable expected SHA")
    if 'branch_state = fetch("branches/main")' not in source or \
            'branch_state.get("protected") is not True' not in source:
        raise AssertionError("source protected main must be enforced by admission gate")
    if 'source_repo.get("private") is not True' not in source or \
            'base_repo.get("private") is not True' not in source:
        raise AssertionError("private source repo identity must be checked")
    pre_start = source.find("      - name: Verify exact checked-out commit/tree and clean source\n")
    build_start = source.find("      - name: GCC full-source compiler build, CTest inventory and full suite\n")
    if pre_start < 0 or build_start <= pre_start:
        raise AssertionError("pre-build exact-source verification step missing")
    pre_tests = source[pre_start:build_start]
    required_pre = (
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test "$(git rev-parse \'HEAD^{tree}\')" = "$EXPECTED_TREE"',
        'test -z "$(git status --porcelain)"',
    )
    for assertion in required_pre:
        if assertion not in pre_tests:
            raise AssertionError("pre-build checked-out source integrity check missing")
    post_start = source.find("      - name: Reconfirm exact live head tree and approval after tests\n")
    if post_start < 0:
        raise AssertionError("post-test head/tree/source verification step missing")
    post_tests = source[post_start:]
    required_post = (
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test "$(git rev-parse \'HEAD^{tree}\')" = "$EXPECTED_TREE"',
        'test -z "$(git status --porcelain --untracked-files=no)"',
        'ATTO_FORGE_POST_TEST_TRACKED_SOURCE_UNCHANGED=',
    )
    for assertion in required_post:
        if assertion not in post_tests.split("          python3 - <<'PY'", 1)[0]:
            raise AssertionError("post-test tracked-source integrity check missing")
    if post_tests.count("          python3 - <<'PY'") != 1:
        raise AssertionError("post-test approval gate must remain last")
    if "ATTO_SOURCE_LOCAL_REVIEWED_HEAD=" not in source:
        raise AssertionError("source review gate is missing")
    if "git rev-parse 'HEAD^{tree}'" not in source:
        raise AssertionError("exact checked-out Git tree comparison is missing")
    if "ATTO_FORGE_GCC_FULL_CTEST=EXECUTED_EXACT_HEAD" not in source or \
            "ATTO_FORGE_CLANG_FULL_CTEST=EXECUTED_EXACT_HEAD" not in source or \
            "ATTO_FORGE_CLANG_ASAN_UBSAN_FULL_CTEST=EXECUTED_EXACT_HEAD" not in source:
        raise AssertionError("full GCC/Clang/sanitizer CTest gates required")
    if "scripts/verify_ctest_discovery.py --build-dir" not in source:
        raise AssertionError("fail-closed full CTest inventory required")
    if "ores-truffle-oreslang/oreslang-source.forge" in source:
        raise AssertionError("legacy source repository forbidden in workflow")
    return [compile(script, str(PATH), "exec") for script in scripts]

class TrustedRunnerAdmissionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gates = workflow_gates()

    def setUp(self):
        self.pull = {
            "state": "open",
            "head": {"sha": HEAD, "repo": {"id": REPO_ID, "full_name": REPO, "private": True}},
            "base": {"ref": "main", "repo": {"id": REPO_ID, "private": True}},
            "user": {"id": 110},
        }
        self.branch_state = {"protected": True}
        self.commit = {"tree": {"sha": TREE}}
        self.reviews = [self.review()]
        self.permission = "write"
        self.permission_user_id = 220
        self.env = {
            "EXPECTED_PR": str(PR_NUMBER), "EXPECTED_HEAD": HEAD, "EXPECTED_TREE": TREE,
            "READ_ONLY_GITHUB_TOKEN": "mock-readonly-token",
        }
        self.calls = []

    def review(self, **changes):
        item = {
            "id": 200, "commit_id": HEAD, "state": "APPROVED",
            "user": {"id": 220, "login": "independent-reviewer", "type": "User"},
            "author_association": "MEMBER",
        }
        item.update(changes)
        return item

    def urlopen(self, request, timeout=20):
        url = urlparse(request.full_url)
        self.assertEqual(url.scheme, "https")
        self.assertEqual(url.netloc, "api.github.com")
        self.assertTrue(url.path.startswith("/repos/" + REPO + "/"))
        self.assertIn(timeout, (20,))
        self.calls.append(request.full_url)
        if url.path.endswith("/branches/main"):
            return HttpResult(self.branch_state)
        if url.path.endswith("/pulls/" + str(PR_NUMBER)):
            return HttpResult(self.pull)
        if url.path.endswith("/git/commits/" + HEAD):
            return HttpResult(self.commit)
        if url.path.endswith("/pulls/" + str(PR_NUMBER) + "/reviews"):
            page = int(parse_qs(url.query)["page"][0])
            return HttpResult(self.reviews[(page - 1) * 100:page * 100])
        if url.path.endswith("/collaborators/independent-reviewer/permission"):
            return HttpResult({"permission": self.permission, "user": {"id": self.permission_user_id}})
        raise AssertionError("unexpected GitHub endpoint: " + request.full_url)

    def run_gate(self, index=0):
        with patch.dict(os.environ, self.env, clear=True), \
                patch("urllib.request.urlopen", self.urlopen), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            exec(self.gates[index], {})
            return output.getvalue()

    def rejected(self, reason, index=0):
        with self.assertRaises(SystemExit) as raised:
            self.run_gate(index)
        self.assertIn(reason, str(raised.exception))

    def test_workflow_token_is_read_only(self):
        original = PATH.read_text(encoding="utf-8")
        for label, changed in [
            ("contents escalated", original.replace("  contents: read", "  contents: write", 1)),
            ("pull requests escalated", original.replace("  pull-requests: read",
                                                          "  pull-requests: write", 1)),
            ("id token added", original.replace("  pull-requests: read",
                                                 "  pull-requests: read\n  id-token: write", 1)),
            ("scope removed", original.replace("  pull-requests: read", "", 1)),
        ]:
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=changed), \
                    self.assertRaisesRegex(AssertionError, "workflow token"):
                workflow_gates()

    def test_actual_source_local_runner_group_required(self):
        original = PATH.read_text(encoding="utf-8")
        for label, changed in [
            ("runner switched to hosted", original.replace(
                "    runs-on:\n      group: atto-forge-source-local-trusted\n"
                "      labels: [self-hosted, linux, x64, atto-ci-trusted]\n",
                "    runs-on: ubuntu-24.04\n", 1)),
            ("runner group changed", original.replace(
                "      group: atto-forge-source-local-trusted",
                "      group: default", 1)),
            ("additional runs-on", original.replace(
                "    timeout-minutes: 45",
                "    runs-on: ubuntu-24.04\n    timeout-minutes: 45", 1)),
            ("main condition removed", original.replace(
                "    if: github.repository == 'atto-lang/atto-source.forge' && "
                "github.ref == 'refs/heads/main'\n", "", 1)),
        ]:
            expected = "main-and-repository" if label == "main condition removed" else "runner group"
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=changed), \
                    self.assertRaisesRegex(AssertionError, expected):
                workflow_gates()

    def test_pre_build_checked_out_source_integrity_is_mandatory(self):
        original = PATH.read_text(encoding="utf-8")
        pre = "      - name: Verify exact checked-out commit/tree and clean source\n"
        gcc = "      - name: GCC full-source compiler build, CTest inventory and full suite\n"
        self.assertEqual(original.count(pre), 1)
        self.assertEqual(original.count(gcc), 1)
        start = original.index(pre)
        end = original.index(gcc)
        self.assertLess(start, end)
        first, source_step, remaining = original[:start], original[start:end], original[end:]
        requirements = [
            ("pre head", 'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"'),
            ("pre tree", 'test "$(git rev-parse \'HEAD^{tree}\')" = "$EXPECTED_TREE"'),
            ("pre clean", 'test -z "$(git status --porcelain)"'),
        ]
        for label, requirement in requirements:
            self.assertIn(requirement, source_step, label)
            altered = first + source_step.replace(requirement, "echo SKIPPED", 1) + remaining
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=altered), \
                    self.assertRaisesRegex(AssertionError, "pre-build checked-out source integrity"):
                workflow_gates()

    def test_post_test_tracked_source_integrity_is_mandatory(self):
        original = PATH.read_text(encoding="utf-8")
        post = "      - name: Reconfirm exact live head tree and approval after tests\n"
        self.assertEqual(original.count(post), 1)
        pre, suffix = original.split(post)
        modifications = [
            ("missing post head", 'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"'),
            ("missing post tree", 'test "$(git rev-parse \'HEAD^{tree}\')" = "$EXPECTED_TREE"'),
            ("missing tracked clean", 'test -z "$(git status --porcelain --untracked-files=no)"'),
            ("missing tracked proof", "ATTO_FORGE_POST_TEST_TRACKED_SOURCE_UNCHANGED="),
        ]
        for label, required in modifications:
            self.assertIn(required, suffix, label)
            altered = pre + post + suffix.replace(required, "echo SKIPPED", 1)
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=altered), \
                    self.assertRaisesRegex(AssertionError, "post-test tracked-source integrity"):
                workflow_gates()

    def test_no_extra_jobs_or_job_permission_override(self):
        original = PATH.read_text(encoding="utf-8")
        for label, changed, expected in [
            ("extra job", original + "\n  unexpected:\n    runs-on: ubuntu-24.04\n",
             "only one trusted"),
            ("job override", original.replace(
                "    timeout-minutes: 45",
                "    permissions:\n      contents: write\n    timeout-minutes: 45", 1),
             "job cannot override"),
            ("read job override", original.replace(
                "    timeout-minutes: 45",
                "    permissions:\n      contents: read\n    timeout-minutes: 45", 1),
             "job cannot override"),
        ]:
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=changed), \
                    self.assertRaisesRegex(AssertionError, expected):
                workflow_gates()

    def test_alternate_event_triggers_are_denied(self):
        original = PATH.read_text(encoding="utf-8")
        for event in ("schedule", "workflow_run", "repository_dispatch",
                      "workflow_call", "workflow_dispatch"):
            trigger = "  workflow_dispatch:\n"
            altered = original.replace(trigger, trigger + f"  {event}:\n", 1)
            with self.subTest(event=event), \
                    patch.object(Path, "read_text", return_value=altered):
                with self.assertRaisesRegex(AssertionError, "unexpected GitHub workflow event"):
                    workflow_gates()

    def test_both_checkout_credentials_and_refs_are_enforced(self):
        original = PATH.read_text(encoding="utf-8")
        credential = "          persist-credentials: false"
        second = original.rfind(credential)
        self.assertGreater(second, original.find(credential))
        second_invalid = original[:second] + original[second:].replace(
            credential, "          persist-credentials: true", 1)
        mutations = [
            ("first credentials", original.replace(
                credential, "          persist-credentials: true", 1), "every checkout"),
            ("second credentials", second_invalid, "every checkout"),
            ("wrong ref", original.replace(
                "          ref: " + "$" + "{{ inputs.expected_head_sha }}",
                "          ref: main", 1), "checkout ref"),
            ("unpinned checkout", original.replace(
                "actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
                "actions/checkout@main", 1), "immutable pinned SHA"),
            ("extra checkout", original.replace(
                "      - name: Execute source-free hostile-input regression tests",
                "      - uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683\n"
                "        with:\n"
                "          persist-credentials: true\n"
                "      - name: Execute source-free hostile-input regression tests", 1),
             "exactly two pinned"),
            ("no clean", original.replace(
                "          clean: true", "          clean: false", 1), "every checkout"),
        ]
        for label, altered, expected in mutations:
            with self.subTest(case=label), \
                    patch.object(Path, "read_text", return_value=altered):
                with self.assertRaisesRegex(AssertionError, expected):
                    workflow_gates()

    def test_pinned_independent_approval_allows_source_local_test_only(self):
        for index in (0, 1):
            self.assertIn("ATTO_SOURCE_LOCAL_SOURCE_EXPORT=NONE", self.run_gate(index))
        self.assertEqual(len(self.calls), 10)

    def test_source_identity_fail_closed(self):
        mutations = [
            ("wrong PR head", lambda: self.pull["head"].update(sha="f" * 40), "identity"),
            ("foreign source repo", lambda: self.pull["head"]["repo"].update(full_name="foreign/repo"), "identity"),
            ("public source repo", lambda: self.pull["head"]["repo"].update(private=False), "identity"),
            ("public base repo", lambda: self.pull["base"]["repo"].update(private=False), "identity"),
            ("wrong immutable repo id", lambda: self.pull["head"]["repo"].update(id=1234), "identity"),
            ("wrong base ref", lambda: self.pull["base"].update(ref="evil"), "identity"),
            ("wrong base repo", lambda: self.pull["base"]["repo"].update(id=1234), "identity"),
            ("closed PR", lambda: self.pull.update(state="closed"), "identity"),
            ("wrong Git tree", lambda: self.commit["tree"].update(sha="f" * 40), "tree"),
            ("malformed PR number", lambda: self.env.update(EXPECTED_PR="../bad"), "malformed pull request number"),
            ("malformed head", lambda: self.env.update(EXPECTED_HEAD="../bad"), "malformed"),
            ("malformed tree", lambda: self.env.update(EXPECTED_TREE="x" * 40), "malformed"),
        ]
        for label, mutate, reason in mutations:
            with self.subTest(case=label):
                self.setUp()
                mutate()
                self.rejected(reason)

    def test_protected_source_main_required_for_both_admission_gates(self):
        for index in (0, 1):
            with self.subTest(gate=index):
                self.branch_state["protected"] = False
                self.rejected("source main is not protected", index=index)
                self.branch_state = {}
                self.rejected("source main is not protected", index=index)
                self.branch_state = {"protected": True}
                self.assertIn("ATTO_SOURCE_LOCAL_REVIEWED_HEAD=", self.run_gate(index))

    def test_stale_untrusted_and_dismissed_reviews_fail_closed(self):
        mutations = [
            ("no review", lambda: self.reviews.clear()),
            ("author self-approval", lambda: self.reviews[0]["user"].update(id=110)),
            ("approval for old commit", lambda: self.reviews[0].update(commit_id="f" * 40)),
            ("bot approval", lambda: self.reviews[0]["user"].update(type="Bot")),
            ("noncollaborator approval", lambda: self.reviews[0].update(author_association="CONTRIBUTOR")),
            ("dismissed review", lambda: self.reviews.append(self.review(id=201, state="DISMISSED"))),
        ]
        for label, mutate in mutations:
            with self.subTest(case=label):
                self.setUp()
                mutate()
                self.rejected("no independent")

    def test_later_change_request_overrides_approval(self):
        self.reviews.append(self.review(id=201, state="CHANGES_REQUESTED"))
        self.rejected("requested changes")
        self.reviews.append(self.review(id=202, state="APPROVED"))
        self.run_gate()

    def test_other_reviewers_change_request_blocks_dispatch(self):
        self.reviews.append(self.review(id=203, state="CHANGES_REQUESTED",
                                        user={"id": 330, "login": "second", "type": "User"}))
        self.rejected("requested changes")

    def test_complete_review_pagination(self):
        self.reviews = [self.review(id=i+1) for i in range(250)]
        self.run_gate()
        self.assertEqual(len(self.calls), 7)

    def test_read_only_org_member_cannot_authorize_runner(self):
        self.permission = "read"
        self.rejected("no independent")

    def test_maintain_role_has_write_authority(self):
        self.permission = "maintain"
        self.assertIn("ATTO_SOURCE_LOCAL_REVIEWED_HEAD=", self.run_gate())

    def test_repository_permission_identity_must_match_reviewer(self):
        self.permission_user_id = 500
        self.rejected("no independent")

    def test_revoked_permission_denies_even_after_approval(self):
        self.run_gate()
        self.permission = "none"
        self.rejected("no independent", index=1)

    def test_review_json_must_be_list(self):
        original = self.urlopen
        def bad(request, timeout=20):
            if "/reviews?" in request.full_url:
                return HttpResult({"not": "an array"})
            return original(request, timeout)
        with patch.dict(os.environ, self.env, clear=True), \
                patch("urllib.request.urlopen", bad), \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(SystemExit, "invalid GitHub review response"):
                exec(self.gates[0], {})

if __name__ == "__main__":
    unittest.main()
