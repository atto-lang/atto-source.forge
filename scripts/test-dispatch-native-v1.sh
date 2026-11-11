#!/usr/bin/env bash
# Forge's existing x86_64 opt-in call backend, not complete AttoLang syntax.
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
source="$root/contracts/dispatch-v1/stage0_direct_call_42.atto"
test -f "$source"
test "$("$forge" run-calls "$source")" = 42
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
printf 'FORGE_STAGE0_NATIVE_DIRECT_PASS|head=%s|source_blob=%s|asm_sha256=%s|native_sha256=%s|value=42|general_atto_parity=NOT_EXECUTED\n' \
 "$EXPECTED_HEAD" "$(git hash-object "$source")" \
 "$(sha256sum "$tmp/entry.s" | cut -d' ' -f1)" "$(sha256sum "$tmp/native" | cut -d' ' -f1)"
# Canonical source must be verified independently; the stage-0 file does not
# prove its int-first parameter grammar, class, virtual or static cases.
echo 'FORGE_CANONICAL_DISPATCH_CORPUS=NOT_EXECUTED'
