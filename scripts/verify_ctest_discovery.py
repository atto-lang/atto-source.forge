#!/usr/bin/env python3
"""Fail closed if a Forge CTest inventory omits critical tests.

CTest may print 'No tests were found!!!' and still return exit status zero.
This gate checks the *discovered* test inventory before running it, rather
than treating the ctest exit status or a test-name substring as evidence.
"""

import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path


REQUIRED = frozenset({
    "oresforge_unit",
    "oresforge_legacy_operator_grammar",
    "oresforge_generator_frame_v1",
    "oresforge_state_region_provenance_v1",
    "oresforge_tenant_isolation_policy",
    "oresforge_actor_mailbox_destructor",
    "oresforge_actor_mailbox_limits",
    "oresforge_actor_mailbox_auth_precedence",
    "oresforge_actor_mailbox_concurrency",
    "oresforge_actor_mailbox_mpmc",
    "oresforge_actor_mailbox_model",
    "oresforge_actor_arena",
    "oresforge_actor_incarnation_memory_v1_host",
    "oresforge_tenant_isolate_host_admission",
    "oresforge_declarative_export_top",
    "oresforge_declarative_export_bottom",
    "oresforge_declarative_export_package",
    "oresforge_declarative_export_name-256",
    "oresforge_declarative_export_reject_missing",
    "oresforge_declarative_export_reject_duplicate",
    "oresforge_declarative_export_reject_visibility-conflict",
    "oresforge_declarative_export_reject_pkg-entry",
    "oresforge_declarative_export_reject_name_257",
    "scoped_permission_host_policy_substrate",
    "native_pinned_tcp_host_boundary",
    "source_scalar_parity",
    "source_import_parity_admission",
    "scalar_parity_harness_selftest",
    "oresforge_source_i64_interpreter",
    "oresforge_cli_source_budget",
    "oresforge_ets_pg_fail_closed_source",
    "oresforge_static_facts_v0_negative",
    "oresforge_static_facts_v0_real_compiler",
    "forge_wrapper_option_host_conformance",
    "oresforge_cli_atto_guest",
    "oresforge_cli_legacy_ores_guest",
    "oresforge_project_root_resolver_host_only",
    "value_boundary_admission",
    "numeric_reference_and_capability_admission",
    "refinement_admission",
    "agent_model_dispatch_host_fencing_v1",
})

LINUX_X86_REQUIRED = frozenset({
    "oresforge_native_asm_executable",
    "oresforge_runtime_argument_native_executable",
    "oresforge_real_native_recursive_calls",
    "oresforge_compiled_tenant_isolate_native",
    "forge_actor_stack_domain_host_v1",
})


def verify_inventory(payload: object, *, linux_x86: bool) -> tuple[int, int]:
    if not isinstance(payload, dict) or not isinstance(payload.get("tests"), list):
        raise ValueError("CTEST_DISCOVERY_MALFORMED: missing tests array")
    tests = payload["tests"]
    names = []
    for item in tests:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or not item["name"]:
            raise ValueError("CTEST_DISCOVERY_MALFORMED: invalid test name")
        names.append(item["name"])
    if len(names) != len(set(names)):
        raise ValueError("CTEST_DISCOVERY_DUPLICATE: test names are not unique")
    # CTest's JSON inventory can include a test with NO executable when its
    # target was never built. It also lists tests marked disabled or configured
    # to skip or invert failure. Counting those as executed gates is unsafe.
    disallowed = {"DISABLED", "SKIP_RETURN_CODE", "SKIP_REGULAR_EXPRESSION",
                  "WILL_FAIL", "PASS_REGULAR_EXPRESSION"}
    for item in tests:
        name = item["name"]
        command = item.get("command")
        if (not isinstance(command, list) or not command or
                not all(isinstance(arg, str) for arg in command) or
                not command[0]):
            raise ValueError("CTEST_DISCOVERY_NO_EXECUTABLE: " + name)
        properties = item.get("properties", [])
        if not isinstance(properties, list):
            raise ValueError("CTEST_DISCOVERY_MALFORMED: invalid test properties")
        for prop in properties:
            if not isinstance(prop, dict) or not isinstance(prop.get("name"), str):
                raise ValueError("CTEST_DISCOVERY_MALFORMED: invalid test property")
            if prop["name"] in disallowed:
                raise ValueError("CTEST_DISCOVERY_SKIP_POLICY: " + name +
                                 " has " + prop["name"])
    required = REQUIRED | (LINUX_X86_REQUIRED if linux_x86 else frozenset())
    missing = sorted(required - set(names))
    if missing:
        raise ValueError("CTEST_DISCOVERY_MISSING: " + ", ".join(missing))
    return len(names), len(required)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    options = parser.parse_args()
    try:
        build_dir = options.build_dir.resolve(strict=True)
        if not build_dir.is_dir():
            raise ValueError("CTEST_DISCOVERY_BAD_BUILD_DIR")
        run = subprocess.run(
            ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", timeout=30, check=False,
        )
        if run.returncode != 0:
            raise ValueError(f"CTEST_DISCOVERY_COMMAND_FAILED: exit {run.returncode}: {run.stderr.strip()}")
        payload = json.loads(run.stdout)
        linux_x86 = sys.platform == "linux" and platform.machine().lower() in {"x86_64", "amd64"}
        actual, minimum = verify_inventory(payload, linux_x86=linux_x86)
        print(f"CTEST_DISCOVERY_VERIFIED: {actual} unique tests, {minimum} critical names present")
        return 0
    except (OSError, ValueError, json.JSONDecodeError, subprocess.TimeoutExpired) as exc:
        print(f"CTEST_DISCOVERY_REJECTED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
