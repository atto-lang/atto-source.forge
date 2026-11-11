#!/usr/bin/env bash
# Actual Forge compiler admission, not source-only or JS simulation.
set -euo pipefail
compiler="${1:?usage: $0 /path/to/oresforge}"
[[ -x "$compiler" ]] || { echo FORGE_COMPILER_NOT_EXECUTED >&2; exit 2; }
base="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
for id in with-scalar-reject with-trivia-reject; do
  if "$compiler" ir "$base/tests/with-ownership-v1/$id.atto" >"$tmp/$id.ir" 2>"$tmp/$id.err"; then
    echo "UNSOUND_FORGE_WITH_ADMISSION $id" >&2; exit 1
  fi
  grep -q 'WITH_BACKEND_UNSUPPORTED:' "$tmp/$id.err"
  test ! -s "$tmp/$id.ir"
done
for id in with-inert-comment with-inert-lookalike with-call-identifier; do
  "$compiler" ir "$base/tests/with-ownership-v1/$id.atto" >"$tmp/$id.ir" 2>"$tmp/$id.err"
  test -s "$tmp/$id.ir"
done
echo FORGE_WITH_FRONTEND_ADMISSION_PASS
