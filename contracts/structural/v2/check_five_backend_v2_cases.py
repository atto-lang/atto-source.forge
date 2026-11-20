#!/usr/bin/env python3
"""Verify normative tuple-v2 contract bytes match the independently pinned
Wasm and .NET contracts. This is target-only metadata, never guest evidence.
"""
import hashlib
import json
from pathlib import Path
import sys

CONTRACT = Path(__file__).with_name("cases.json")
EXPECTED_GIT_BLOB = "57c80adaa4029c6fd3e46a63c490cb193f951568"
BACKENDS = ["java", "llvm", "net", "wasm", "forge"]
REQUIRED_NEGATIVE_CASES = {
    "safety.tuple.width.65",
    "safety.family.size.129",
    "safety.unknown.slot",
    "safety.mutable.borrow.slot",
}


class ContractError(ValueError):
    pass


def unique_pairs(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise ContractError("duplicate JSON field")
        result[k] = v
    return result


def verify(data):
    if not isinstance(data, bytes) or not 0 < len(data) <= 64 * 1024:
        raise ContractError("invalid bounded contract bytes")
    actual = hashlib.sha1(
        b"blob " + str(len(data)).encode("ascii") + b"\0" + data
    ).hexdigest()
    if actual != EXPECTED_GIT_BLOB:
        raise ContractError("Forge tuple-v2 fixture differs from .NET/Wasm exact blob")
    try:
        item = json.loads(data.decode("utf-8"), object_pairs_hook=unique_pairs)
    except (ValueError, UnicodeError) as error:
        raise ContractError("invalid tuple-v2 JSON") from error
    if (not isinstance(item, dict)
            or item.get("contract") != "oreslang.structural-positional-tuple.v2"
            or item.get("status") != "target-only"
            or item.get("backend_ids") != BACKENDS
            or not isinstance(item.get("cases"), list)):
        raise ContractError("tuple-v2 contract identity mismatch")
    cases = item["cases"]
    if len(cases) != 30:
        raise ContractError("tuple-v2 case count mismatch")
    ids = [entry.get("id") for entry in cases if isinstance(entry, dict)]
    if len(ids) != 30 or len(set(ids)) != 30:
        raise ContractError("tuple-v2 case identity mismatch")
    indexed = {entry["id"]: entry for entry in cases}
    if any(indexed.get(k, {}).get("expect") != "reject"
           for k in REQUIRED_NEGATIVE_CASES):
        raise ContractError("missing tuple security rejection controls")
    if any(entry.get("expect") not in ("accept", "reject")
           or not isinstance(entry.get("source"), str) or not entry["source"].strip()
           for entry in cases):
        raise ContractError("invalid tuple case")
    return len(cases), actual


def run_self_test(reference):
    mutations = (
        reference.replace(b"safety.tuple.width.65", b"safety.tuple.width.64"),
        reference.replace(b"safety.mutable.borrow.slot", b"safety.mutable.slot"),
        reference.replace(b'"target-only"', b'"certified"'),
        reference + b" ",
        b'{"contract":"x","contract":"y"}',
    )
    for candidate in mutations:
        try:
            verify(candidate)
        except ContractError:
            pass
        else:
            raise ContractError("unsafe tuple-v2 mutation passed")


def main(argv):
    if argv not in ([], ["--self-test"]):
        print("usage: check_five_backend_v2_cases.py [--self-test]", file=sys.stderr)
        return 2
    try:
        source = CONTRACT.read_bytes()
        length, digest = verify(source)
        if argv:
            run_self_test(source)
    except (OSError, ContractError) as error:
        print("TUPLE_V2_CONTRACT_FAILED: " + str(error), file=sys.stderr)
        return 1
    print("TUPLE_V2_TARGET_CONTRACT=PASS cases=%d git_blob=%s" % (length, digest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
