#!/usr/bin/env bash
# Actual canonical AttoLang direct-call fixture through Forge x86_64 native compiler.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$root"
: "${EXPECTED_HEAD:?exact source SHA required}" "${ORESFORGE:?compiled Forge tool required}"
test "$(git rev-parse HEAD)" = "$EXPECTED_HEAD"
test -z "$(git status --porcelain)"
forge="$(realpath -e -- "$ORESFORGE")"
test -x "$forge" && test "$(basename "$forge")" = oresforge
test "$(uname -s)" = Linux && test "$(uname -m)" = x86_64
command -v cc >/dev/null
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
source="$root/contracts/dispatch-v1/direct_call_42.atto"
test -f "$source"
test "$("$forge" run-calls "$source")" = 42
# The older postfix-typed stage-0 spelling must remain source-compatible.
test "$("$forge" run-calls "$root/contracts/dispatch-v1/stage0_direct_call_42.atto")" = 42
"$forge" emit-call-asm x86_64 "$source" > "$tmp/entry.s"
test -s "$tmp/entry.s"
grep -Fq '  call .Lores_fn_' "$tmp/entry.s"
cat > "$tmp/driver.c" <<'C'
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
extern int64_t oresforge_entry(void);
int main(void) {
  const int64_t actual = oresforge_entry();
  if (printf("%" PRId64 "\n", actual) < 0) return 10;
  return fflush(stdout) != 0 ? 11 : 0;
}
C
cc -std=c11 -Wall -Wextra -Werror "$tmp/driver.c" "$tmp/entry.s" -o "$tmp/native"
test "$("$tmp/native")" = 42
printf 'FORGE_CANONICAL_NATIVE_DIRECT_PASS|head=%s|source_blob=%s|asm_sha256=%s|native_sha256=%s|value=42|other_cases=NOT_EXECUTED\n' \
 "$EXPECTED_HEAD" "$(git hash-object "$source")" \
 "$(sha256sum "$tmp/entry.s" | cut -d' ' -f1)" "$(sha256sum "$tmp/native" | cut -d' ' -f1)"
# A compiler-owned wrong-arity diagnostic, not a generic syntax rejection.
invalid="$root/contracts/dispatch-v1/wrong_arity_reject.atto"
if "$forge" run-calls "$invalid" >"$tmp/bad.stdout" 2>"$tmp/bad.stderr"; then
  echo "wrong-arity canonical source executed" >&2
  exit 1
fi
test ! -s "$tmp/bad.stdout"
grep -Fq 'function arity mismatch' "$tmp/bad.stderr"
if "$forge" emit-call-asm x86_64 "$invalid" >"$tmp/bad.s" 2>"$tmp/bad-asm.stderr"; then
  echo "wrong-arity canonical source emitted native assembly" >&2
  exit 1
fi
test ! -s "$tmp/bad.s"
grep -Fq 'function arity mismatch' "$tmp/bad-asm.stderr"
printf 'FORGE_CANONICAL_ARITY_REJECT|head=%s|source_blob=%s|no_asm=TRUE\n' \
 "$EXPECTED_HEAD" "$(git hash-object "$invalid")"
echo 'FORGE_CANONICAL_DISPATCH_CORPUS=DIRECT_AND_ARITY_ONLY;INSTANCE_STATIC_VIRTUAL=NOT_IMPLEMENTED'
