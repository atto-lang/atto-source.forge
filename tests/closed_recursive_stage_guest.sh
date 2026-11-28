#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cxx="${CXX:-c++}"
flags=(-std=c++20 -Wall -Wextra -Werror -pedantic -O1 -fsanitize=undefined -fno-sanitize-recover=all -I"$root/include")
"$cxx" "${flags[@]}" "$root/tests/closed_recursive_stage_emit.cpp" -o "$tmp/emitter"
"$tmp/emitter" invalid
for spec in 'fib0:0:0:' 'fib1:0:1:' 'fib10:0:55:' 'fib20:0:6765:' 'count1024:0:1024:' 'count1025:75::E_STACK' 'wide:0:9223372036854775807:' 'overflow:70::E_OVERFLOW' 'ordered:71::E_DIVZERO' 'min:0:-9223372036854775808:'; do
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
