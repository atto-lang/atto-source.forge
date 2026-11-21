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


CI_FILE = Path(__file__).resolve().parents[1] / '.github' / 'workflows' / 'ci.yml'


def assert_ci_wiring(content):
    """Require a separate exact-head, source-free, read-only governance job."""
    start = content.find("  source-free-codeowners-contract:\n")
    end = content.find("  bootstrap:\n", start)
    if start < 0 or end <= start:
        raise AssertionError('source-free CODEOWNERS CI job missing')
    job = content[start:end]
    if '\\' + '
    @classmethod
    def setUpClass(cls):
        cls.source = OWNER_FILE.read_text(encoding='utf8')

    def test_ci_job_executes_contract_on_exact_checkout(self):
        assert_ci_wiring(CI_FILE.read_text(encoding='utf8'))

    def test_ci_job_rejects_removed_or_weakened_guards(self):
        original = CI_FILE.read_text(encoding='utf8')
        changes = (
            ("job removed", "  source-free-codeowners-contract:\n",
             "  renamed-codeowners-job:\n"),
            ("test omitted", "python3 -m unittest discover -s verification -p test_source_local_codeowners.py -v",
             "echo NOT_RUN"),
            ("checkout unpinned", "actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
             "actions/checkout@main"),
            ("credentials enabled", "persist-credentials: false", "persist-credentials: true"),
            ("source no longer pinned", 'ref: ' + '
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
 + '{{' in job:
        raise AssertionError('CODEOWNERS CI has escaped/non-executable GitHub expressions')
    workflow_permissions = content.split('\npermissions:\n', 1)
    if len(workflow_permissions) != 2 or workflow_permissions[1].split('\nconcurrency:', 1)[0].strip() != 'contents: read':
        raise AssertionError('CODEOWNERS CI must inherit read-only workflow permissions')
    required = (
        "    if: github.repository == 'atto-lang/atto-source.forge'\n",
        "    runs-on: ubuntu-24.04\n",
        "    timeout-minutes: 5\n",
        "      PYTHONDONTWRITEBYTECODE: '1'\n",
        'actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683',
        'ref: ' + '
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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
        'persist-credentials: false',
        'clean: true',
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test -z "$(git status --porcelain)"',
        'python3 -m unittest discover -s verification -p test_source_local_codeowners.py -v',
    )
    for requirement in required:
        if requirement not in job:
            raise AssertionError('source-free CODEOWNERS CI safeguard missing: ' + requirement)
    if job.count('actions/checkout@') != 1:
        raise AssertionError('unexpected additional CODEOWNERS CI checkout')
    if '    permissions:' in job:
        raise AssertionError('CODEOWNERS CI job must not override workflow token scopes')


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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
             "ref: main"),
            ("clean removed", "clean: true", "clean: false"),
            ("identity check removed", 'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
             "echo NOT_CHECKED"),
            ("repository scope removed", "    if: github.repository == 'atto-lang/atto-source.forge'",
             "    if: always()"),
        )
        for name, old, new in changes:
            self.assertIn(old, original, name)
            altered = original.replace(old, new, 1)
            with self.subTest(case=name), self.assertRaises(AssertionError):
                assert_ci_wiring(altered)

    def test_ci_job_denies_additional_checkout_or_token_scopes(self):
        original = CI_FILE.read_text(encoding='utf8')
        extra = original.replace(
            "      - name: Verify exact checkout for source-free CODEOWNERS policy test",
            "      - uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683\n"
            "        with:\n"
            "          persist-credentials: true\n"
            "      - name: Verify exact checkout for source-free CODEOWNERS policy test", 1)
        with self.assertRaisesRegex(AssertionError, 'unexpected additional'):
            assert_ci_wiring(extra)
        override = original.replace(
            "  source-free-codeowners-contract:\n",
            "  source-free-codeowners-contract:\n    permissions:\n      contents: write\n", 1)
        with self.assertRaisesRegex(AssertionError, 'must not override'):
            assert_ci_wiring(override)
        workflow_write = original.replace('\npermissions:\n  contents: read\n',
                                          '\npermissions:\n  contents: write\n', 1)
        with self.assertRaisesRegex(AssertionError, 'read-only workflow'):
            assert_ci_wiring(workflow_write)
        escaped = original.replace('ref: ' + '
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
 + '{{' in job:
        raise AssertionError('CODEOWNERS CI has escaped/non-executable GitHub expressions')
    workflow_permissions = content.split('\npermissions:\n', 1)
    if len(workflow_permissions) != 2 or workflow_permissions[1].split('\nconcurrency:', 1)[0].strip() != 'contents: read':
        raise AssertionError('CODEOWNERS CI must inherit read-only workflow permissions')
    required = (
        "    if: github.repository == 'atto-lang/atto-source.forge'\n",
        "    runs-on: ubuntu-24.04\n",
        "    timeout-minutes: 5\n",
        "      PYTHONDONTWRITEBYTECODE: '1'\n",
        'actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683',
        'ref: ' + '
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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
        'persist-credentials: false',
        'clean: true',
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test -z "$(git status --porcelain)"',
        'python3 -m unittest discover -s verification -p test_source_local_codeowners.py -v',
    )
    for requirement in required:
        if requirement not in job:
            raise AssertionError('source-free CODEOWNERS CI safeguard missing: ' + requirement)
    if job.count('actions/checkout@') != 1:
        raise AssertionError('unexpected additional CODEOWNERS CI checkout')
    if '    permissions:' in job:
        raise AssertionError('CODEOWNERS CI job must not override workflow token scopes')


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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
                                   'ref: \\' + '
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
 + '{{' in job:
        raise AssertionError('CODEOWNERS CI has escaped/non-executable GitHub expressions')
    workflow_permissions = content.split('\npermissions:\n', 1)
    if len(workflow_permissions) != 2 or workflow_permissions[1].split('\nconcurrency:', 1)[0].strip() != 'contents: read':
        raise AssertionError('CODEOWNERS CI must inherit read-only workflow permissions')
    required = (
        "    if: github.repository == 'atto-lang/atto-source.forge'\n",
        "    runs-on: ubuntu-24.04\n",
        "    timeout-minutes: 5\n",
        "      PYTHONDONTWRITEBYTECODE: '1'\n",
        'actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683',
        'ref: ' + '
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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
        'persist-credentials: false',
        'clean: true',
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test -z "$(git status --porcelain)"',
        'python3 -m unittest discover -s verification -p test_source_local_codeowners.py -v',
    )
    for requirement in required:
        if requirement not in job:
            raise AssertionError('source-free CODEOWNERS CI safeguard missing: ' + requirement)
    if job.count('actions/checkout@') != 1:
        raise AssertionError('unexpected additional CODEOWNERS CI checkout')
    if '    permissions:' in job:
        raise AssertionError('CODEOWNERS CI job must not override workflow token scopes')


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
 + '{{ github.event.pull_request.head.sha || github.sha }}', 1)
        with self.assertRaisesRegex(AssertionError, 'escaped/non-executable'):
            assert_ci_wiring(escaped)

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
 + '{{' in job:
        raise AssertionError('CODEOWNERS CI has escaped/non-executable GitHub expressions')
    workflow_permissions = content.split('\npermissions:\n', 1)
    if len(workflow_permissions) != 2 or workflow_permissions[1].split('\nconcurrency:', 1)[0].strip() != 'contents: read':
        raise AssertionError('CODEOWNERS CI must inherit read-only workflow permissions')
    required = (
        "    if: github.repository == 'atto-lang/atto-source.forge'\n",
        "    runs-on: ubuntu-24.04\n",
        "    timeout-minutes: 5\n",
        "      PYTHONDONTWRITEBYTECODE: '1'\n",
        'actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683',
        'ref: ' + '
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
 + '{{ github.event.pull_request.head.sha || github.sha }}',
        'persist-credentials: false',
        'clean: true',
        'test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"',
        'test -z "$(git status --porcelain)"',
        'python3 -m unittest discover -s verification -p test_source_local_codeowners.py -v',
    )
    for requirement in required:
        if requirement not in job:
            raise AssertionError('source-free CODEOWNERS CI safeguard missing: ' + requirement)
    if job.count('actions/checkout@') != 1:
        raise AssertionError('unexpected additional CODEOWNERS CI checkout')
    if '    permissions:' in job:
        raise AssertionError('CODEOWNERS CI job must not override workflow token scopes')


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
