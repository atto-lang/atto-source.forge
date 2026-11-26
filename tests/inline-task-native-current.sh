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
guest void_no_separator 'do sync task {} do sync task<void> { return; } return 42;' 42 0
guest local 'return sync task<int> {val int x=40; val int y=x+1; return y+1;};' 42 0
guest nested_local 'return sync task<int> {val int x=40; return sync task {val int y=x+1;return y+1;};};' 42 0
guest sibling_local 'return sync task {val int x=20;return x;}+sync task {val int x=22;return x;};' 42 0
guest unused_overflow 'return sync task {val int x=9223372036854775807+1;return 42;};' E_OVERFLOW 70
guest overflow 'return sync task<int> { return 9223372036854775807+1; };' E_OVERFLOW 70
reject discard 'do sync task<int> {return 1;}; return 42;' E_TASK_RETURN_TYPE
reject inferred_discard 'do sync task {return 1;}; return 42;' E_TASK_RETURN_TYPE
reject bare 'sync task {}; return 42;' E_PARSE_SYNTAX
reject void_value 'return sync task<void> {};' E_TASK_RETURN_TYPE
reject scheduled 'return task<int> {return 42;};' E_TASK_UNSUPPORTED
reject cleanup 'do sync task {defer {};}; return 42;' E_TASK_UNSUPPORTED
reject local_escape 'return sync task {val int x=42;return x;}+x;' E_PARSE_SYNTAX
reject forward_local 'return sync task {val int x=y;val int y=42;return x;};' E_PARSE_SYNTAX
reject duplicate_local 'return sync task {val int x=1;val int x=42;return x;};' E_TASK_UNSUPPORTED
reject mutable_local 'return sync task {let int x=42;return x;};' E_TASK_UNSUPPORTED
printf 'FORGE_INLINE_NATIVE=%s/9 NEGATIVE=%s/10\n' "$pass" "$neg"
test "$pass" = 9; test "$neg" = 10
