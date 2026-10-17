#!/usr/bin/env python3
"""Canonical actor selector fixtures shared with .java/.llvm/.net/.wasm."""
import json
import pathlib
import sys

def main() -> int:
    source = pathlib.Path(sys.argv[1])
    data = json.loads(source.read_text(encoding="utf-8"))
    if data["schema"] != "oreslang.actor-pipeline-routing.v1":
        raise AssertionError("wrong routing fixture schema")
    assert data["operations"]["forward_all"] == [
        "worker-a", "worker-b", "worker-c"]
    assert data["operations"]["forward_round_robin"] == [
        "worker-a", "worker-b", "worker-c",
        "worker-a", "worker-b", "worker-c"]
    for entry in data["operations"]["by_key"]:
        h = 0xcbf29ce484222325
        for byte in entry["key"].encode("utf-8", "strict"):
            h = ((h ^ byte) * 0x100000001b3) & 0xffffffffffffffff
        if f"{h:016x}" != entry["fnv1a64_hex"]:
            raise AssertionError(f"hash mismatch for {entry['key']!r}")
        index = h % 3
        if data["operations"]["forward_all"][index] != entry["target"]:
            raise AssertionError(f"route mismatch for {entry['key']!r}")
    print("ORES_FORGE_ACTOR_PIPELINE_FIXTURE_PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
