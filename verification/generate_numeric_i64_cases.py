#!/usr/bin/env python3
"""Generate Forge native test cases from the reviewed Oreslang source corpus.

The source fixture, rather than duplicated C++ constants, owns the inputs.
Fail closed on fixture drift, nonportable syntax, overflow and tampering.
This generator is a test-only build tool; it does not compile Oreslang.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

FIXTURE_SHA = "bbc319c19ecd007109d33592e210091eb267e5a2"
LO, HI = -(1 << 63), (1 << 63) - 1


def evaluate(node):
    if isinstance(node, ast.Constant) and type(node.value) is int:
        # +2**63 cannot appear as an ordinary Oreslang int literal.
        # It is admitted solely as the immediate syntactic magnitude
        # of unary minus used to spell INT64_MIN.
        if not 0 <= node.value <= HI:
            raise ValueError("portable integer literal out of bounds")
        return node.value
    if isinstance(node, ast.UnaryOp) and type(node.op) is ast.USub:
        if (isinstance(node.operand, ast.Constant)
                and type(node.operand.value) is int
                and node.operand.value == (1 << 63)):
            return LO
        result = -evaluate(node.operand)
    elif isinstance(node, ast.BinOp) and type(node.op) in (ast.Add, ast.Sub, ast.Mult):
        x, y = evaluate(node.left), evaluate(node.right)
        result = x + y if type(node.op) is ast.Add else (
            x - y if type(node.op) is ast.Sub else x * y)
    else:
        # No unary plus, floats, exponentiation or implicit promotion:
        # this must match the actual checked-i64 source parser.
        raise ValueError("nonportable numeric source expression")
    if not LO <= result <= HI:
        raise ValueError("i64 intermediate overflow")
    return result


def generate(source):
    raw = Path(source).read_bytes()
    sha = hashlib.sha1(b"blob " + str(len(raw)).encode("ascii")
                       + bytes((0,)) + raw).hexdigest()
    if sha != FIXTURE_SHA:
        raise ValueError("numeric source fixture Git blob identity differs")
    data = json.loads(raw)
    if (data.get("schema_version") != "1.0.0"
            or data.get("status") != "reference-only-not-compiled-execution"):
        raise ValueError("unexpected numeric corpus schema/status")
    cases = data.get("cases")
    if not isinstance(cases, list) or len(cases) != 7:
        raise ValueError("expected precisely seven portable signed i64 cases")
    ids = set()
    entries = []
    for case in cases:
        identifier, expression, expected = (
            case["id"], case["expression"], case["expected"])
        if (not isinstance(identifier, str) or not identifier.isascii()
                or not re.fullmatch(r"[a-z0-9-]{1,60}", identifier)
                or identifier in ids):
            raise ValueError("invalid/duplicate portable case ID")
        ids.add(identifier)
        if (not isinstance(expression, str) or
                not re.fullmatch(r"[0-9()+* -]{1,128}", expression)):
            raise ValueError("nonportable expression")
        got = evaluate(ast.parse(expression, mode="eval").body)
        # The parser admits the positive 2^63 token solely so unary minus
        # can produce i64::MIN. A *bare* 2^63 is not an i64 value: catch
        # it before generating a C++ narrowing/compile failure.
        if not LO <= got <= HI:
            raise ValueError("portable i64 final value out of bounds")
        if not isinstance(expected, str) or str(got) != expected:
            raise ValueError("portable case has an invalid reference result")
        native = ("std::numeric_limits<std::int64_t>::min()"
                  if got == LO else "std::int64_t{" + str(got) + "}")
        entries.append('    std::pair{"' + expression + '", ' + native + '}')
    return ('// Generated from reviewed numeric source corpus ' + FIXTURE_SHA + '\n'
            '#pragma once\n'
            '#include <array>\n#include <cstdint>\n'
            '#include <limits>\n#include <utility>\n'
            'inline constexpr auto kPortableNumericalCases = std::array{\n'
            + ',\n'.join(entries) + '\n};\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.corpus)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Avoid replacing a valid source-controlled output with a failed parse.
    tmp = args.output.with_suffix(args.output.suffix + ".tmp")
    tmp.write_text(result, encoding="ascii")
    tmp.replace(args.output)


if __name__ == "__main__":
    main()
