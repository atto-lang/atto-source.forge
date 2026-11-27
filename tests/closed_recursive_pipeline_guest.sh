#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cxx="${CXX:-c++}"
flags=(-std=c++20 -Wall -Wextra -Werror -pedantic -O1 -fsanitize=undefined -fno-sanitize-recover=all -I"$root/include")
"$cxx" "${flags[@]}" "$root/tests/closed_recursive_pipeline_emit.cpp" -o "$tmp/emitter"
"$tmp/emitter" invalid
for spec in 'chain:0:42:' 'depth-reset:0:1024:' 'quota:75::E_STACK' 'upstream-first:71::E_DIVZERO' 'stage-first:70::E_OVERFLOW' 'max-stages:0:6:'; do
  IFS=: read -r id status want err <<< "$spec"
  "$tmp/emitter" "$id" > "$tmp/guest.cpp"
  "$cxx" "${flags[@]}" "$tmp/guest.cpp" -o "$tmp/guest"
  rc=0
  (ulimit -s 256; "$tmp/guest") > "$tmp/out" 2> "$tmp/err" || rc=$?
  test "$rc" -eq "$status"
  if [[ -n "$want" ]]; then printf '%s\n' "$want" > "$tmp/want"; else : > "$tmp/want"; fi
  cmp "$tmp/want" "$tmp/out"
  if [[ -n "$err" ]]; then printf '%s\n' "$err" > "$tmp/want"; else : > "$tmp/want"; fi
  cmp "$tmp/want" "$tmp/err"
  echo "PASS $id ($cxx)"
done
