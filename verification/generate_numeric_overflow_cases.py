#!/usr/bin/env python3
"""Generate checked-overflow negative guest cases from pinned shared corpus.

Data integrity and a separate Python oracle are preflight checks. Only the
subsequent Forge source parser/verified IR execution counts as guest testing.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
from generate_numeric_i64_cases import evaluate

PIN = "2ce0b0ff5c52e54a788280fd64d67fa8dc32614a"
IDS = ("positive-add-overflow", "negative-sub-overflow",
       "positive-multiply-overflow", "negative-unary-overflow")


def generate(path):
    raw = Path(path).read_bytes()
    sha = hashlib.sha1(b"blob " + str(len(raw)).encode("ascii")
                       + bytes((0,)) + raw).hexdigest()
    if sha != PIN:
        raise ValueError("signed-i64 overflow corpus diverged from reviewed blob")
    document = json.loads(raw)
    if (document.get("schema_version") != "1.0.0"
            or document.get("policy") != "no-wrapping-i64"
            or document.get("status") != "requires-executable-negative-tests"):
        raise ValueError("invalid shared overflow corpus policy")
    cases = document.get("cases")
    if not isinstance(cases, list) or tuple(c.get("id") for c in cases) != IDS:
        raise ValueError("overflow corpus case IDs/order are not canonical")
    expressions = []
    for item in cases:
        expr = item.get("source")
        if (not isinstance(expr, str)
                or not re.fullmatch(r"[0-9()+* -]{1,128}", expr)
                or item.get("expected") != "reject-or-runtime-error"):
            raise ValueError("invalid signed-i64 overflow case")
        try:
            evaluate(ast.parse(expr, mode="eval").body)
        except ValueError:
            pass
        else:
            raise ValueError("overflow fixture no longer overflows i64")
        expressions.append('    "' + expr + '"')
    return ('// Reviewed signed-i64 overflow Git blob ' + PIN + '\n'
            '#pragma once\n#include <array>\n'
            'inline constexpr auto kOverflowNumericalCases = '
            'std::array<const char*,4>{\n'
            + ',\n'.join(expressions) + '\n};\n')


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--corpus", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    content = generate(args.corpus)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix(args.output.suffix + ".tmp")
    temp.write_text(content, encoding="ascii")
    temp.replace(args.output)


if __name__ == "__main__":
    main()
