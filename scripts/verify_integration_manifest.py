#!/usr/bin/env python3
"""Fail-closed verification of Oreslang's declaration-only integration manifests.

This is intentionally not an implementation or runtime parity test.
Uses only Python's standard library and does not contact GitHub or the network.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import sys

PROTOCOL = 'oreslang-ecosystem-integration'
NOTE = 'Declared integration touchpoints only; no CI or runtime parity is asserted.'
UPSTREAMS = {
    'oreslang-source.java': ('oreslang-common-source', 'oreslang-target-profiles'),
    'oreslang-source.llvm': ('oreslang-common-source', 'oreslang-target-profiles'),
    'oreslang-source.wasm': ('oreslang-source.llvm', 'oreslang-target-profiles', 'oreslang-wasm'),
    'oreslang-source.js': ('oreslang-common-source', 'oreslang-target-profiles'),
    'oreslang-source.hdl': ('oreslang-common-source', 'oreslang-target-profiles'),
    'oreslang-source.forge': ('oreslang-common-source', 'oreslang-target-profiles'),
    'oreslang-source.net': ('oreslang-common-source', 'oreslang-target-profiles'),
}
EXPECTED_KEYS = frozenset({'schemaVersion', 'protocol', 'repository', 'role', 'upstreams', 'evidence', 'note'})
MAX_BYTES = 4096
# Capture the actual OS implementation for capability detection. Adversarial
# tests instrument os.open without changing the platform's dir_fd support.
_NATIVE_OPEN = os.open


class InvalidManifest(ValueError):
    pass


def _unique_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    obj: dict[str, object] = {}
    for key, value in pairs:
        if key in obj:
            raise InvalidManifest(f'duplicate JSON key: {key!r}')
        obj[key] = value
    return obj


def _reject_constant(value: str) -> None:
    raise InvalidManifest(f'non-JSON constant: {value}')


def verify_bytes(raw: bytes, repository_name: str) -> dict[str, object]:
    """Return the verified manifest or raise InvalidManifest (no false attestations)."""
    if repository_name not in UPSTREAMS:
        raise InvalidManifest('unknown source repository')
    if not (1 <= len(raw) <= MAX_BYTES):
        raise InvalidManifest('manifest is empty or oversized')
    try:
        decoded = raw.decode('utf-8', errors='strict')
    except UnicodeError as exc:
        raise InvalidManifest('manifest must be valid UTF-8') from exc
    if decoded.startswith('\ufeff'):
        raise InvalidManifest('UTF-8 BOM is prohibited')
    try:
        value = json.loads(decoded, object_pairs_hook=_unique_pairs, parse_constant=_reject_constant)
    except (json.JSONDecodeError, UnicodeError) as exc:
        raise InvalidManifest('invalid JSON') from exc
    if not isinstance(value, dict) or set(value) != EXPECTED_KEYS:
        raise InvalidManifest('manifest must contain exactly the seven declared keys')
    if type(value['schemaVersion']) is not int or value['schemaVersion'] != 1:
        raise InvalidManifest('unsupported schema version')
    if value['protocol'] != PROTOCOL or type(value['protocol']) is not str:
        raise InvalidManifest('unsupported protocol')
    if type(value['repository']) is not str or value['repository'] != 'ores-truffle-oreslang/' + repository_name:
        raise InvalidManifest('repository identity mismatch')
    if type(value['role']) is not str or value['role'] != 'compiler':
        raise InvalidManifest('unexpected integration role')
    upstreams = value['upstreams']
    if type(upstreams) is not list or any(type(item) is not str for item in upstreams):
        raise InvalidManifest('upstreams must be a string array')
    if tuple(upstreams) != UPSTREAMS[repository_name]:
        raise InvalidManifest('unexpected, reordered, or duplicate upstream dependencies')
    if type(value['evidence']) is not str or value['evidence'] != 'not-attested':
        raise InvalidManifest('unverified parity/CI attestation is forbidden')
    if type(value['note']) is not str or value['note'] != NOTE:
        raise InvalidManifest('evidence disclaimer was changed')
    return value



def _read_manifest_physically(root: Path) -> bytes:
    """Pin both path components to directory fds; never follow a swapped leaf.

    A source-controlled security gate cannot silently fall back to racy path
    reads on a platform without descriptor-relative, no-follow access.
    Native Windows reparse-point support needs a separate implementation.
    """
    required = ('O_DIRECTORY', 'O_NOFOLLOW', 'O_NONBLOCK', 'O_CLOEXEC')
    if os.name != 'posix' or any(not hasattr(os, flag) for flag in required):
        raise InvalidManifest('secure descriptor-relative manifest reads unavailable')
    # Some POSIX Python builds expose the flag constants but do not support
    # dir_fd for os.open. Reject the platform before opening any descriptor.
    if _NATIVE_OPEN not in getattr(os, 'supports_dir_fd', ()):
        raise InvalidManifest('descriptor-relative open is unsupported')

    directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    leaf_flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
    # A no-follow open of an absolute path checks only its final component.
    # Walk from the filesystem root to reject symlinks in *all* ancestors.
    if not root.is_absolute():
        raise InvalidManifest('repository root must be absolute')
    root_fd = os.open(root.anchor, directory_flags)
    try:
        for component in root.parts[1:]:
            next_fd = os.open(component, directory_flags, dir_fd=root_fd)
            os.close(root_fd)
            root_fd = next_fd
    except BaseException:
        os.close(root_fd)
        raise
    try:
        parent_fd = os.open('.oreslang', directory_flags, dir_fd=root_fd)
        try:
            leaf_fd = os.open('integration.json', leaf_flags, dir_fd=parent_fd)
            try:
                opened = os.fstat(leaf_fd)
                if not stat.S_ISREG(opened.st_mode):
                    raise InvalidManifest('opened manifest is not a regular file')
                if opened.st_nlink != 1:
                    raise InvalidManifest('manifest has unexpected hardlink aliases')
                if opened.st_size > MAX_BYTES:
                    raise InvalidManifest('manifest exceeds size limit')

                # The file may grow after fstat. Request no more than 4097
                # bytes total, including the one-byte overlimit witness.
                chunks: list[bytes] = []
                remaining = MAX_BYTES + 1
                while remaining:
                    chunk = os.read(leaf_fd, remaining)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    remaining -= len(chunk)
                # Reject detectable in-place mutation after the descriptor was
                # admitted. Identity, link count, size, mtime and ctime must
                # remain stable. This is defense in depth, not a cryptographic
                # guarantee against a malicious concurrent filesystem writer.
                after = os.fstat(leaf_fd)
                fields = ('st_mode', 'st_dev', 'st_ino', 'st_nlink',
                          'st_size', 'st_mtime_ns', 'st_ctime_ns')
                if any(getattr(opened, field) != getattr(after, field)
                       for field in fields):
                    raise InvalidManifest('manifest changed during bounded read')
                return b''.join(chunks)
            finally:
                os.close(leaf_fd)
        finally:
            os.close(parent_fd)
    finally:
        os.close(root_fd)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository-root', type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args(argv)
    # Preserve the final path component for O_NOFOLLOW; resolve() would
    # silently dereference a symlinked repository-root authority alias.
    try:
        # Reject parent traversal before abspath() folds it lexically. In
        # particular, "symlink/../repo" can resolve somewhere different from
        # the lexically normalized path when the kernel follows the symlink.
        if '..' in args.repository_root.parts:
            raise InvalidManifest('repository root parent traversal is forbidden')
        # os.path.abspath preserves embedded NULs; os.open then raises
        # ValueError, outside this CLI's normal (InvalidManifest/OSError)
        # fail-closed diagnostic path.
        if '\x00' in os.fspath(args.repository_root):
            raise InvalidManifest('repository root contains NUL')
        root = Path(os.path.abspath(args.repository_root))
        expected_name = root.name
        verify_bytes(_read_manifest_physically(root), expected_name)
    except (InvalidManifest, OSError) as exc:
        print(f'integration manifest DENIED: {exc}', file=sys.stderr)
        return 1
    print(f'integration declaration verified: {expected_name}; evidence remains not-attested')
    return 0


if __name__ == '__main__':
    sys.exit(main())
