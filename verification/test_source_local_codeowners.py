"""Fail closed on loss of independent source-local CI CODEOWNER coverage.

This is a conservative matcher for the exact-file and root-directory patterns
used in this repository, not a full implementation of GitHub CODEOWNERS globbing.
"""
from pathlib import Path
import unittest

OWNER_FILE = Path(__file__).resolve().parents[1] / '.github' / 'CODEOWNERS'
EXPECTED = {
    '.github/workflows/atto-logical-words-source-local-trusted.yml':
        ('@ORESoftware', '@the1mills'),
    'verification/test_atto_source_local_trusted_runner_gate.py':
        ('@ORESoftware', '@the1mills'),
}
UNCHANGED_EXPORT_POLICY = {
    '.github/CODEOWNERS',
    '.github/workflows/private-ci-policy.yml',
    '.github/private-ci-independent-reviewer.json',
    '.github/private-ci-approved-repositories.txt',
    '.github/private-ci-approved-source-snapshots.txt',
    '.github/private-ci-required-checks.json',
}


def owners_for(content, path):
    """Return final owners under only supported simple rooted CODEOWNERS patterns."""
    owners = None
    for raw in content.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split()
        pattern = fields[0]
        if not pattern.startswith('/') or any(ch in pattern for ch in '*?[]'):
            raise ValueError(f'unsupported CODEOWNERS pattern: {pattern}')
        canonical = pattern[1:]
        match = path.startswith(canonical) if pattern.endswith('/') else path == canonical
        if match:
            owners = tuple(fields[1:])
    return owners


def assert_expected(content):
    for path, approved in EXPECTED.items():
        actual = owners_for(content, path)
        if actual != approved:
            raise AssertionError(f'{path}: required independent owners {approved}, got {actual}')
    for path in UNCHANGED_EXPORT_POLICY:
        if owners_for(content, path) != ('@ORESoftware',):
            raise AssertionError(f'export policy CODEOWNERS unexpectedly changed: {path}')


class TrustedCIReviewerCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = OWNER_FILE.read_text(encoding='utf8')

    def test_source_local_paths_have_independent_codeowners(self):
        assert_expected(self.source)

    def test_removing_independent_owner_fails(self):
        for path in EXPECTED:
            rule = '/' + path + ' @ORESoftware @the1mills'
            with self.subTest(path=path):
                self.assertIn(rule, self.source)
                with self.assertRaisesRegex(AssertionError, 'required independent owners'):
                    assert_expected(self.source.replace(rule, '/' + path + ' @ORESoftware', 1))

    def test_typos_fall_back_to_author_only_and_fail(self):
        for path in EXPECTED:
            with self.subTest(path=path):
                changed = self.source.replace('/' + path + ' @', '/' + path + '.typo @', 1)
                with self.assertRaisesRegex(AssertionError, 'required independent owners'):
                    assert_expected(changed)

    def test_later_broad_ownership_override_fails(self):
        for directory in ('/.github/workflows/', '/verification/'):
            with self.subTest(directory=directory):
                with self.assertRaisesRegex(AssertionError, 'required independent owners'):
                    assert_expected(self.source + directory + ' @ORESoftware\n')

    def test_accidental_private_export_owner_change_fails(self):
        row = '/.github/private-ci-independent-reviewer.json @ORESoftware'
        with self.assertRaisesRegex(AssertionError, 'export policy CODEOWNERS unexpectedly changed'):
            assert_expected(self.source.replace(row, row + ' @the1mills', 1))

    def test_unrelated_path_has_no_new_privileged_owner(self):
        self.assertEqual(owners_for(self.source, 'verification/anything_else.py'), ('@ORESoftware',))
        self.assertEqual(owners_for(self.source, '.github/workflows/ci.yml'), ('@ORESoftware',))


if __name__ == '__main__':
    unittest.main()
