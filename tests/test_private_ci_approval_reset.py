"""Bootstrap denial canary: verifies zero active cross-org private-source grants.

This is a local, source-only policy regression suite, NOT an authentication
of protected main, reviewer approval, or GitHub Actions allocation.
"""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POLICIES = [".github/private-ci-approved-repositories.txt",".github/private-ci-approved-source-snapshots.txt"]
FORMER_CANDIDATES = ["honeypot-r-us-test/hnpt-lib-core","1343209023","46dc99c5261843e64bc1f232a2d3158bf02b71bf"]
PROFILE = 'forge'


def read_regular_policy(path):
    if path.is_symlink() or path.parent.is_symlink() or not path.is_file():
        raise ValueError("policy must be a regular, non-symlink file")
    raw = path.read_bytes()
    return validate_policy_bytes(raw)


def validate_policy_bytes(raw):
    if not isinstance(raw, bytes) or not raw or len(raw) > 65536:
        raise ValueError("policy bytes missing or oversized")
    if any(c != 10 and not 32 <= c <= 126 for c in raw):
        raise ValueError("noncanonical policy bytes")
    if not raw.endswith(b"\n"):
        raise ValueError("policy requires LF terminator")
    lines = raw.decode("ascii").split("\n")
    active = []
    for line in lines:
        if not line:
            continue
        if line != line.strip():
            raise ValueError("ambiguous whitespace")
        if line.startswith("#"):
            continue
        active.append(line)
    return active


def require_zero_authorizations(raw):
    if validate_policy_bytes(raw):
        raise ValueError("active private-source transfer approval is forbidden in bootstrap")


class PrivateCiApprovalResetTest(unittest.TestCase):
    def test_active_destinations_and_snapshots_are_empty(self):
        for name in POLICIES:
            with self.subTest(name=name):
                path = ROOT / name
                self.assertEqual(read_regular_policy(path), [])

    def test_superseded_candidates_removed_from_live_policy_bytes(self):
        for name in POLICIES:
            raw = (ROOT / name).read_bytes()
            for candidate in FORMER_CANDIDATES:
                with self.subTest(policy=name, candidate=candidate):
                    self.assertNotIn(candidate.encode("ascii"), raw)

    def test_malformed_inputs_and_accidental_grants_fail_closed(self):
        original = b"# Private CI approvals disabled\n"
        self.assertIsNone(require_zero_authorizations(original))
        attacks = [
            b"",
            b"# Missing final newline",
            b"# comment\r\n",
            b"# comment\x00\n",
            b"# comment\xff\n",
            b"  # indented comment\n",
            b"# trailing whitespace \n",
            b"\t# lookalike\n",
            b"owner/private-ci 123456\n",
            b"# comment\nowner/private-ci 123456\n",
            b"# comment\nowner/private-ci\n",
            b"# comment\n" + b"A" * 65536,
        ]
        for index, raw in enumerate(attacks):
            with self.subTest(attack=index):
                with self.assertRaises(ValueError):
                    require_zero_authorizations(raw)

    def test_policy_parent_and_file_are_not_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            policy = root / "policy"
            policy.write_bytes(b"# deny\n")
            link = root / "linked"
            link.symlink_to(policy)
            with self.assertRaises(ValueError):
                read_regular_policy(link)

    def test_disabled_independent_reviewer_cannot_authorize_carrier(self):
        path = ROOT / ".github/private-ci-independent-reviewer.json"
        self.assertTrue(path.is_file() and not path.is_symlink())
        policy = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(policy["schema"], 2)
        self.assertIs(policy["enabled"], False)
        self.assertIsNone(policy["reviewer"])
        self.assertIn(11139560, policy["disallowedReviewerIds"])
        self.assertIn(2148448, policy["disallowedReviewerIds"])
        workflow = (ROOT / ".github/workflows/private-ci-policy.yml").read_text(
            encoding="utf-8")
        self.assertIn('if reviewer["enabled"] is False:', workflow)
        self.assertIn('if reviewer["reviewer"] is not None or repos or snaps:', workflow)
        self.assertIn('approvals require enabled independent reviewer', workflow)

    def test_active_snapshot_and_destination_both_fail(self):
        for row in (
            b"carrier/private 123456\n",
            b"0" * 40 + b" " + b"1" * 40 + b" 123456\n",
        ):
            with self.assertRaises(ValueError):
                require_zero_authorizations(b"# bootstrap disabled\n" + row)


    def test_real_workflow_validator_denies_synthetic_policy_mutations(self):
        """Execute the workflow's real validator on policy-only temporary fixtures.

        This is NOT approval to publish source, nor hosted CI evidence.
        """
        import shutil
        import sys
        import textwrap
        workflow_path = ROOT / ".github/workflows/private-ci-policy.yml"
        workflow = workflow_path.read_text(encoding="utf-8")
        marker = "          python3 - <<'PY'\n"
        self.assertEqual(workflow.count(marker), 1)
        start = workflow.index(marker) + len(marker)
        end = workflow.index("\n          PY", start)
        validator = textwrap.dedent(workflow[start:end]) + "\n"
        self.assertIn("approvals require enabled independent reviewer", validator)
        self.assertNotIn("git push", validator)
        self.assertNotIn("gh api", validator)

        policy_files = (
            ".github/CODEOWNERS",
            ".github/private-ci-approved-repositories.txt",
            ".github/private-ci-approved-source-snapshots.txt",
            ".github/private-ci-independent-reviewer.json",
            ".github/private-ci-required-checks.json",
            ".github/workflows/private-ci-policy.yml",
        )
        destinations = ".github/private-ci-approved-repositories.txt"
        snapshots = ".github/private-ci-approved-source-snapshots.txt"
        reviewer_path = ".github/private-ci-independent-reviewer.json"
        required_path = ".github/private-ci-required-checks.json"

        cases = (
            "baseline", "destination", "snapshot", "paired_while_disabled",
            "duplicate_reviewer_json", "crlf_comment", "missing_newline",
            "oversized_policy", "missing_policy", "symlinked_file",
            "symlinked_parent", "hardlinked_file", "invalid_required_check",
        )
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                workspace = Path(tmp)
                for relative in policy_files:
                    destination = workspace / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / relative, destination)

                approval = workspace / destinations
                snapshot = workspace / snapshots
                reviewer = workspace / reviewer_path
                if case in ("destination", "paired_while_disabled"):
                    approval.write_bytes(b"carrier/private 123456\n")
                if case in ("snapshot", "paired_while_disabled"):
                    snapshot.write_bytes(b"0" * 40 + b" " + b"1" * 40 + b" 123456\n")
                if case == "duplicate_reviewer_json":
                    data = json.loads(reviewer.read_text(encoding="utf-8"))
                    payload = json.dumps(data)
                    self.assertIn('"enabled": false', payload)
                    reviewer.write_text(payload.replace(
                        '"enabled": false', '"enabled": false, "enabled": true', 1
                    ) + "\n", encoding="utf-8")
                if case == "crlf_comment":
                    approval.write_bytes(b"# disabled\r\n")
                if case == "missing_newline":
                    approval.write_bytes(b"# disabled")
                if case == "oversized_policy":
                    approval.write_bytes(b"# disabled\n" + b"A" * 70000)
                if case == "missing_policy":
                    approval.unlink()
                if case == "symlinked_file":
                    backing = workspace / "approval-backing"
                    approval.replace(backing)
                    approval.symlink_to(backing)
                if case == "hardlinked_file":
                    backing = workspace / "approval-backing"
                    approval.replace(backing)
                    os.link(backing, approval)
                if case == "symlinked_parent":
                    real_directory = workspace / "actual-github"
                    (workspace / ".github").rename(real_directory)
                    (workspace / ".github").symlink_to(
                        real_directory, target_is_directory=True)
                if case == "invalid_required_check":
                    required = workspace / required_path
                    data = json.loads(required.read_text(encoding="utf-8"))
                    data["checks"][0]["context"] = "untrusted-check"
                    required.write_text(json.dumps(data) + "\n", encoding="utf-8")

                result = subprocess.run(
                    [sys.executable, "-I", "-c", validator],
                    cwd=workspace, stdin=subprocess.DEVNULL,
                    capture_output=True, text=True, timeout=10,
                    env={"HOME": str(workspace), "LC_ALL": "C", "PATH": "/usr/bin:/bin"},
                    check=False,
                )
                if case == "baseline":
                    self.assertEqual(result.returncode, 0,
                                     result.stdout + result.stderr)
                    self.assertIn("private CI export policy: PASS", result.stdout)
                else:
                    self.assertNotEqual(result.returncode, 0,
                                        "policy mutation was wrongly accepted: " + case)



if __name__ == "__main__":
    unittest.main()
