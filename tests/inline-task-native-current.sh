#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
compiler=${1:?forge_scalar path required}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass=0; neg=0
guest() {
  local name=$1 body=$2 expected=$3 rc=$4
  printf 'pub fnc main(): int { %s }\n' "$body" > "$work/$name.atto"
  "$compiler" "$work/$name.atto" -o "$work/$name.cpp"
  "${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I "$root/include" "$work/$name.cpp" -o "$work/$name"
  local actual=0
  "$work/$name" > "$work/$name.out" 2> "$work/$name.err" || actual=$?
  test "$actual" = "$rc"
  if [[ "$rc" = 0 ]]; then test "$(cat "$work/$name.out")" = "$expected"; test ! -s "$work/$name.err"
  else test ! -s "$work/$name.out"; test "$(cat "$work/$name.err")" = "$expected"; fi
  sha256sum "$work/$name.atto" "$work/$name.cpp" "$work/$name"
  pass=$((pass+1))
}
reject() {
  local name=$1 body=$2 expected=$3 rc=0
  printf 'pub fnc main(): int { %s }\n' "$body" > "$work/$name.atto"
  "$compiler" "$work/$name.atto" -o "$work/$name.cpp" > "$work/$name.out" 2> "$work/$name.err" || rc=$?
  test "$rc" = 2; test ! -e "$work/$name.cpp"; grep -F "$expected" "$work/$name.err"
  neg=$((neg+1))
}
guest typed 'return sync task<int> { return 40+2; };' 42 0
guest nested 'return 1+sync task { return sync task<int> { return 40; }+1; };' 42 0
guest void 'do sync task {}; do sync task<void> { return; }; return 42;' 42 0
guest overflow 'return sync task<int> { return 9223372036854775807+1; };' E_OVERFLOW 70
reject discard 'do sync task<int> {return 1;}; return 42;' E_TASK_RETURN_TYPE
reject inferred_discard 'do sync task {return 1;}; return 42;' E_TASK_RETURN_TYPE
reject bare 'sync task {}; return 42;' E_PARSE_SYNTAX
reject void_value 'return sync task<void> {};' E_TASK_RETURN_TYPE
reject scheduled 'return task<int> {return 42;};' E_TASK_UNSUPPORTED
reject cleanup 'do sync task {defer {};}; return 42;' E_TASK_UNSUPPORTED
printf 'FORGE_INLINE_NATIVE=%s/4 NEGATIVE=%s/6\n' "$pass" "$neg"
test "$pass" = 4; test "$neg" = 6
