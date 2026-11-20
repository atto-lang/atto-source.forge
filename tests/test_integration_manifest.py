"""Adversarial tests for the cross-backend declaration-only manifest gate."""
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

MODULE_FILE = Path(__file__).resolve().parent.parent / 'scripts' / 'verify_integration_manifest.py'
spec = importlib.util.spec_from_file_location('manifest_gate', MODULE_FILE)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

REPO_NAME = 'oreslang-source.java'


def valid_manifest(repo=REPO_NAME):
    return {
        'schemaVersion': 1,
        'protocol': module.PROTOCOL,
        'repository': 'ores-truffle-oreslang/' + repo,
        'role': 'compiler',
        'upstreams': list(module.UPSTREAMS[repo]),
        'evidence': 'not-attested',
        'note': module.NOTE,
    }


def encode(payload):
    return json.dumps(payload, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


class IntegrationManifestGateTests(unittest.TestCase):
    def test_all_seven_source_backends(self):
        self.assertEqual(len(module.UPSTREAMS), 7)
        for repo in module.UPSTREAMS:
            with self.subTest(repo=repo):
                self.assertEqual(module.verify_bytes(encode(valid_manifest(repo)), repo)['evidence'], 'not-attested')

    def test_real_local_manifest(self):
        root = Path(__file__).resolve().parent.parent
        self.assertEqual(module.verify_bytes((root / '.oreslang/integration.json').read_bytes(), root.name)['role'], 'compiler')

    def assert_reject(self, raw, repo=REPO_NAME):
        with self.assertRaises(module.InvalidManifest):
            module.verify_bytes(raw, repo)

    def test_duplicate_key(self):
        self.assert_reject(encode(valid_manifest())[:-1] + b',"evidence":"not-attested"}')

    def test_unsupported_repo(self):
        self.assert_reject(encode(valid_manifest()), 'oreslang-source.invented')

    def test_repository_mismatch(self):
        self.assert_reject(encode(valid_manifest('oreslang-source.llvm')))

    def test_evidence_claim_rejected(self):
        value = valid_manifest()
        for claim in ['passed', 'attested', 'verified', None, 1]:
            with self.subTest(claim=claim):
                value['evidence'] = claim
                self.assert_reject(encode(value))

    def test_upstream_injection_and_reordering(self):
        value = valid_manifest()
        for dependencies in [[], ['oreslang-target-profiles', 'oreslang-common-source'],
                             ['oreslang-common-source', 'oreslang-common-source'],
                             ['../oreslang-target-profiles'], ['oreslang-common-source', 'oreslang-target-profiles', 'other']]:
            with self.subTest(dependencies=dependencies):
                value['upstreams'] = dependencies
                self.assert_reject(encode(value))

    def test_unknown_field(self):
        value = valid_manifest()
        value['runtimeParity'] = True
        self.assert_reject(encode(value))

    def test_missing_field(self):
        value = valid_manifest()
        value.pop('note')
        self.assert_reject(encode(value))

    def test_schema_bool_is_not_int(self):
        value = valid_manifest()
        value['schemaVersion'] = True
        self.assert_reject(encode(value))

    def test_invalid_json_constants(self):
        self.assert_reject(encode(valid_manifest()).replace(b'"schemaVersion":1', b'"schemaVersion":NaN'))

    def test_oversize(self):
        self.assert_reject(b' ' * (module.MAX_BYTES + 1))

    def test_invalid_utf8(self):
        self.assert_reject(b'\xff' + encode(valid_manifest()))

    def test_utf8_bom(self):
        self.assert_reject(b'\xef\xbb\xbf' + encode(valid_manifest()))

    def test_noncanonical_note(self):
        value = valid_manifest()
        value['note'] = 'CI parity is asserted.'
        self.assert_reject(encode(value))

    def test_cli_rejects_embedded_nul_root_without_unhandled_exception(self):
        # Argparse accepts this when invoked as a Python callable even though
        # an OS shell cannot pass an actual NUL in argv.
        self.assertEqual(module.main(['--repository-root',
                                      '/' + REPO_NAME + '\x00outside']), 1)

    @unittest.skipUnless(os.name == 'posix', 'POSIX descriptor instrumentation')
    def test_cli_accepts_regular_manifest_with_instrumented_open(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            native_open = os.open
            opened = []

            def trace_open(path, flags, *args, **kwargs):
                opened.append(str(path))
                return native_open(path, flags, *args, **kwargs)

            with mock.patch.object(module.os, 'open', side_effect=trace_open):
                self.assertEqual(module.main(['--repository-root', str(root)]), 0)
            self.assertIn('integration.json', opened)

    @unittest.skipUnless(os.name == 'posix', 'POSIX dir_fd support gate')
    def test_cli_denies_missing_descriptor_relative_open_support(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            # A platform with O_NOFOLLOW constants but no dir_fd-compatible
            # os.open must deny the manifest, never fall back to path reads.
            with mock.patch.object(module.os, 'supports_dir_fd', set()):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)

    def test_cli_rejects_symlinked_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            external = Path(temp) / 'external.json'
            external.write_bytes(encode(valid_manifest()))
            (root / '.oreslang/integration.json').symlink_to(external)
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)


    def test_cli_rejects_symlinked_manifest_parent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            root.mkdir()
            external = Path(temp) / 'outside'
            external.mkdir()
            (external / 'integration.json').write_bytes(encode(valid_manifest()))
            (root / '.oreslang').symlink_to(external, target_is_directory=True)
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)

    def test_cli_rejects_hardlinked_manifest_alias(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            external = Path(temp) / 'outside-integration.json'
            external.write_bytes(encode(valid_manifest()))
            (root / '.oreslang/integration.json').hardlink_to(external)
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)


    @unittest.skipUnless(os.name == 'posix', 'Unix root traversal gate')
    def test_cli_rejects_parent_traversal_even_to_existing_valid_repo(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            (Path(temp) / 'intermediate').mkdir()
            noncanonical = Path(temp) / 'intermediate' / '..' / REPO_NAME
            self.assertEqual(module.main(['--repository-root', str(root)]), 0)
            self.assertEqual(module.main(['--repository-root', str(noncanonical)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix root traversal gate')
    def test_cli_rejects_parent_traversal_through_symlink_component(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            external = Path(temp) / 'outside'
            external.mkdir()
            (Path(temp) / 'alias').symlink_to(external, target_is_directory=True)
            deceptive = Path(temp) / 'alias' / '..' / REPO_NAME
            self.assertEqual(module.main(['--repository-root', str(deceptive)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix bounded manifest gate')
    def test_cli_accepts_exact_4096_bytes_and_rejects_4097(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            raw = encode(valid_manifest())
            self.assertLess(len(raw), module.MAX_BYTES)
            leaf.write_bytes(raw + b' ' * (module.MAX_BYTES - len(raw)))
            self.assertEqual(module.main(['--repository-root', str(root)]), 0)
            leaf.write_bytes(raw + b' ' * (module.MAX_BYTES + 1 - len(raw)))
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix no-follow ancestor gate')
    def test_cli_rejects_symlinked_root_ancestor_with_valid_external_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            tmp = Path(temp)
            outside_root = tmp / 'outside-parent' / REPO_NAME
            (outside_root / '.oreslang').mkdir(parents=True)
            (outside_root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            alias_parent = tmp / 'parent-alias'
            alias_parent.symlink_to(outside_root.parent, target_is_directory=True)
            self.assertEqual(module.main(['--repository-root', str(alias_parent / REPO_NAME)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix no-follow root gate')
    def test_cli_rejects_symlinked_repository_root_with_valid_external_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            tmp = Path(temp)
            external_root = tmp / 'outside' / REPO_NAME
            (external_root / '.oreslang').mkdir(parents=True)
            (external_root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            root_alias = tmp / REPO_NAME
            root_alias.symlink_to(external_root, target_is_directory=True)
            # Even a fully valid outside manifest must not be admitted through
            # a symlinked repository-root argument.
            self.assertEqual(module.main(['--repository-root', str(root_alias)]), 1)
            self.assertEqual(module.main(['--repository-root', str(root_alias) + '/.']), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix descriptor gate')
    def test_cli_rejects_directory_as_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang/integration.json').mkdir(parents=True)
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix descriptor gate')
    def test_cli_rejects_oversized_manifest_before_parsing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(b'x' * (module.MAX_BYTES + 1))
            self.assertEqual(module.main(['--repository-root', str(root)]), 1)

    @unittest.skipUnless(os.name == 'posix', 'Unix bounded short-read gate')
    def test_cli_accepts_short_reads_without_losing_manifest_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            original_read = os.read
            sizes = []

            def fragmented_read(fd, size):
                sizes.append(size)
                return original_read(fd, min(size, 7))

            with mock.patch.object(module.os, 'read', side_effect=fragmented_read):
                self.assertEqual(module.main(['--repository-root', str(root)]), 0)
            self.assertGreater(len(sizes), 2)
            self.assertTrue(all(0 < count <= module.MAX_BYTES + 1 for count in sizes))

    @unittest.skipUnless(os.name == 'posix', 'Unix parent no-follow gate')
    def test_cli_rejects_parent_swapped_to_symlink_at_open(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            outside = Path(temp) / 'outside'
            outside.mkdir()
            (outside / 'integration.json').write_bytes(encode(valid_manifest()))
            original_open = os.open
            swapped = False

            def parent_swap(path, flags, *args, **kwargs):
                nonlocal swapped
                if path == '.oreslang' and not swapped:
                    (root / '.oreslang').rename(root / '.original-oreslang')
                    (root / '.oreslang').symlink_to(outside, target_is_directory=True)
                    swapped = True
                return original_open(path, flags, *args, **kwargs)

            with mock.patch.object(module.os, 'open', side_effect=parent_swap):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(swapped)

    @unittest.skipUnless(os.name == 'posix', 'descriptor relative Unix gate')
    def test_cli_accepts_regular_manifest_after_fd_pinning(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            self.assertEqual(module.main(['--repository-root', str(root)]), 0)

    @unittest.skipUnless(os.name == 'posix' and hasattr(os, 'mkfifo'), 'Unix FIFO gate')
    def test_cli_rejects_fifo_without_waiting_for_writer(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            os.mkfifo(root / '.oreslang/integration.json')
            # Run as a child with a timeout so a regression cannot hang CI.
            result = subprocess.run(
                [sys.executable, str(MODULE_FILE), '--repository-root', str(root)],
                capture_output=True, timeout=3, check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'DENIED', result.stderr)

    @unittest.skipUnless(os.name == 'posix', 'Unix no-follow gate')
    def test_cli_rejects_leaf_swapped_to_symlink_immediately_before_open(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            leaf.write_bytes(encode(valid_manifest()))
            outside = Path(temp) / 'external.json'
            outside.write_bytes(encode(valid_manifest()))
            original_open = os.open
            swapped = False

            def swap_at_open(path, flags, *args, **kwargs):
                nonlocal swapped
                if path == 'integration.json' and not swapped:
                    leaf.unlink()
                    leaf.symlink_to(outside)
                    swapped = True
                return original_open(path, flags, *args, **kwargs)

            with mock.patch.object(module.os, 'open', side_effect=swap_at_open):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(swapped)

    @unittest.skipUnless(os.name == 'posix', 'Unix parent fd pinning')
    def test_cli_parent_swap_after_dir_open_uses_original_inode(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            (root / '.oreslang/integration.json').write_bytes(encode(valid_manifest()))
            outside = Path(temp) / 'outside'
            outside.mkdir()
            (outside / 'integration.json').write_bytes(b'invalid external contents')
            original_open = os.open
            swapped = False

            def swap_parent(path, flags, *args, **kwargs):
                nonlocal swapped
                if path == 'integration.json' and not swapped:
                    (root / '.oreslang').rename(root / '.original-oreslang')
                    (root / '.oreslang').symlink_to(outside, target_is_directory=True)
                    swapped = True
                return original_open(path, flags, *args, **kwargs)

            with mock.patch.object(module.os, 'open', side_effect=swap_parent):
                self.assertEqual(module.main(['--repository-root', str(root)]), 0)
            self.assertTrue(swapped)

    @unittest.skipUnless(os.name == 'posix', 'Unix stable descriptor gate')
    def test_cli_rejects_in_place_append_during_read(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            leaf.write_bytes(encode(valid_manifest()))
            original_read = os.read
            mutated = False

            def append_while_reading(fd, size):
                nonlocal mutated
                chunk = original_read(fd, size)
                if not mutated:
                    with leaf.open('ab') as output:
                        output.write(b' ')
                    mutated = True
                return chunk

            with mock.patch.object(module.os, 'read', side_effect=append_while_reading):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(mutated)

    @unittest.skipUnless(os.name == 'posix', 'Unix stable descriptor gate')
    def test_cli_rejects_same_size_rewrite_during_read(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            raw = encode(valid_manifest())
            leaf.write_bytes(raw)
            initial = leaf.stat()
            original_read = os.read
            mutated = False

            def rewrite_while_reading(fd, size):
                nonlocal mutated
                chunk = original_read(fd, size)
                if not mutated:
                    leaf.write_bytes(raw)
                    # Force an observable metadata delta even on filesystems
                    # with coarse ctime/mtime clock resolution.
                    os.utime(leaf, ns=(initial.st_atime_ns,
                                       initial.st_mtime_ns + 1_000_000_000))
                    mutated = True
                return chunk

            with mock.patch.object(module.os, 'read', side_effect=rewrite_while_reading):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(mutated)

    @unittest.skipUnless(os.name == 'posix', 'Unix stable descriptor gate')
    def test_cli_rejects_hardlink_added_during_read(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            leaf.write_bytes(encode(valid_manifest()))
            alias = Path(temp) / 'late-link.json'
            original_read = os.read
            linked = False

            def link_while_reading(fd, size):
                nonlocal linked
                chunk = original_read(fd, size)
                if not linked:
                    alias.hardlink_to(leaf)
                    linked = True
                return chunk

            with mock.patch.object(module.os, 'read', side_effect=link_while_reading):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(linked)

    @unittest.skipUnless(os.name == 'posix', 'Unix descriptor bound')
    def test_cli_caps_read_if_file_grows_after_fstat(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / REPO_NAME
            (root / '.oreslang').mkdir(parents=True)
            leaf = root / '.oreslang/integration.json'
            leaf.write_bytes(encode(valid_manifest()))
            original_fstat = os.fstat
            original_read = os.read
            sizes = []
            grew = False

            def grow_after_fstat(fd):
                nonlocal grew
                metadata = original_fstat(fd)
                if stat.S_ISREG(metadata.st_mode) and not grew:
                    leaf.write_bytes(b'A' * (module.MAX_BYTES + 100_000))
                    grew = True
                return metadata

            def bounded_read(fd, n):
                sizes.append(n)
                return original_read(fd, n)

            with mock.patch.object(module.os, 'fstat', side_effect=grow_after_fstat), \
                 mock.patch.object(module.os, 'read', side_effect=bounded_read):
                self.assertEqual(module.main(['--repository-root', str(root)]), 1)
            self.assertTrue(grew)
            self.assertTrue(sizes)
            self.assertLessEqual(max(sizes), module.MAX_BYTES + 1)
            self.assertLessEqual(sum(sizes), module.MAX_BYTES + 1)



if __name__ == '__main__':
    unittest.main()
