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

REPO = "ores-truffle-oreslang/oreslang-source.forge"
REPO_ID = 1419703674
PR_NUMBER = 241
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
    if not re.search(r"(?m)^on:\s*\n  workflow_dispatch:", source):
        raise AssertionError("only manual dispatch is permitted")
    if re.search(r"(?m)^  (?:pull_request|push|pull_request_target):", source):
        raise AssertionError("untrusted auto-dispatch is prohibited")
    if "group: atto-forge-source-local-trusted" not in source or \
            "labels: [self-hosted, linux, x64, atto-ci-trusted]" not in source:
        raise AssertionError("approved dedicated runner group and label are required")
    if "persist-credentials: false" not in source:
        raise AssertionError("credentials must not be persisted by checkout")
    if "ATTO_SOURCE_LOCAL_REVIEWED_HEAD=" not in source:
        raise AssertionError("source review gate is missing")
    if "git rev-parse 'HEAD^{tree}'" not in source:
        raise AssertionError("exact checked-out Git tree comparison is missing")
    return [compile(script, str(PATH), "exec") for script in scripts]

class TrustedRunnerAdmissionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gates = workflow_gates()

    def setUp(self):
        self.pull = {
            "state": "open",
            "head": {"sha": HEAD, "repo": {"id": REPO_ID, "full_name": REPO}},
            "base": {"ref": "main", "repo": {"id": REPO_ID}},
            "user": {"id": 110},
        }
        self.commit = {"tree": {"sha": TREE}}
        self.reviews = [self.review()]
        self.permission = "write"
        self.permission_user_id = 220
        self.env = {
            "EXPECTED_HEAD": HEAD, "EXPECTED_TREE": TREE,
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

    def test_pinned_independent_approval_allows_source_local_test_only(self):
        for index in (0, 1):
            self.assertIn("ATTO_SOURCE_LOCAL_SOURCE_EXPORT=NONE", self.run_gate(index))
        self.assertEqual(len(self.calls), 8)

    def test_source_identity_fail_closed(self):
        mutations = [
            ("wrong PR head", lambda: self.pull["head"].update(sha="f" * 40), "identity"),
            ("foreign source repo", lambda: self.pull["head"]["repo"].update(full_name="foreign/repo"), "identity"),
            ("wrong immutable repo id", lambda: self.pull["head"]["repo"].update(id=1234), "identity"),
            ("wrong base ref", lambda: self.pull["base"].update(ref="evil"), "identity"),
            ("wrong base repo", lambda: self.pull["base"]["repo"].update(id=1234), "identity"),
            ("closed PR", lambda: self.pull.update(state="closed"), "identity"),
            ("wrong Git tree", lambda: self.commit["tree"].update(sha="f" * 40), "tree"),
            ("malformed head", lambda: self.env.update(EXPECTED_HEAD="../bad"), "malformed"),
            ("malformed tree", lambda: self.env.update(EXPECTED_TREE="x" * 40), "malformed"),
        ]
        for label, mutate, reason in mutations:
            with self.subTest(case=label):
                self.setUp()
                mutate()
                self.rejected(reason)

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
        self.assertEqual(len(self.calls), 6)

    def test_read_only_org_member_cannot_authorize_runner(self):
        self.permission = "read"
        self.rejected("no independent")

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
