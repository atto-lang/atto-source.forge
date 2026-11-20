#!/usr/bin/env bash
# Linux-only real Forge compiler -> C++ -> native guest output-publication gate.
# No test below touches original Atto source, source branch, or caller files.
set -euo pipefail
[[ $# -ge 5 ]] || { echo 'E_TEST_ARGS' >&2; exit 2; }
forge="$1"; source="$2"; parent="$3"; cxx="$4"; includes="$5"
shift 5
scratch="$(mktemp -d "$parent/atto-publish-XXXXXXXX")"
trap 'rm -rf "$scratch"' EXIT

# Sixteen simultaneous independent CLI processes compete for one output.
# Exactly one complete immutable generated source must be published.
dest="$scratch/winner.cpp"
pids=()
for n in $(seq 1 16); do
  "$forge" "$source" -o "$dest" >"$scratch/stdout-$n" 2>"$scratch/stderr-$n" &
  pids+=("$!")
done
passed=0
rejected=0
n=0
for pid in "${pids[@]}"; do
  n=$((n+1))
  if wait "$pid"; then
    passed=$((passed+1))
    test ! -s "$scratch/stdout-$n"
    test ! -s "$scratch/stderr-$n"
  else
    rejected=$((rejected+1))
    test ! -s "$scratch/stdout-$n"
    test "$(cat "$scratch/stderr-$n")" = "E_OUTPUT"
  fi
done
[[ "$passed" -eq 1 && "$rejected" -eq 15 ]] || {
  echo "E_PUBLISH_RACE passed=$passed rejected=$rejected" >&2
  exit 1
}
test -s "$dest"
test "$(stat -c %a "$dest")" = 600
test ! -e "$dest.part"

# Recompile to a fresh pathname and compare exact generated C++ bytes;
# no partial or adversary-substituted inode may be reported as success.
"$forge" "$source" -o "$scratch/independent.cpp"
cmp "$dest" "$scratch/independent.cpp"
before="$(sha256sum "$dest" | cut -d' ' -f1)"
set +e
"$forge" "$source" -o "$dest" >"$scratch/replay.out" 2>"$scratch/replay.err"
rc=$?
set -e
test "$rc" -eq 2
test "$(cat "$scratch/replay.err")" = E_OUTPUT
test ! -s "$scratch/replay.out"
test "$(sha256sum "$dest" | cut -d' ' -f1)" = "$before"

# Adversarial existing files and links are never replaced or dereferenced.
printf 'retain this value\n' >"$scratch/important"
ln -s "$scratch/important" "$scratch/link.cpp"
set +e
"$forge" "$source" -o "$scratch/link.cpp" >"$scratch/link.out" 2>"$scratch/link.err"
rc=$?
set -e
test "$rc" -eq 2 && test "$(cat "$scratch/link.err")" = E_OUTPUT
test "$(cat "$scratch/important")" = 'retain this value'
test -L "$scratch/link.cpp"
ln -s "$scratch/important" "$scratch/temp-block.cpp.part"
set +e
"$forge" "$source" -o "$scratch/temp-block.cpp" >"$scratch/part.out" 2>"$scratch/part.err"
rc=$?
set -e
test "$rc" -eq 2 && test "$(cat "$scratch/part.err")" = E_OUTPUT
test ! -e "$scratch/temp-block.cpp"
test "$(cat "$scratch/important")" = 'retain this value'

# Most importantly, compile the actual race winner into a real guest
# executable; a host-only string comparison would not prove valid emission.
"$cxx" -std=c++20 -Wall -Wextra -Werror -pedantic "$@" \
  "-I$includes" "$dest" -o "$scratch/winner.native"
test "$("$scratch/winner.native")" = 83
echo "ATTO_FORGE_LINUX_FD_PUBLICATION_PASS winners=1 rejected=15 native_main=83 private_mode=600"
