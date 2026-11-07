#!/usr/bin/env bash
# Compiles source through the REAL Forge stage-0 frontend, then executes its
# interpreted scalar result and verifies native assembly emission. This is not
# cross-module or zero-libc/OS parity.
set -euo pipefail
test "$#" -eq 2 || { echo 'usage: check_... COMPILER REPO_ROOT' >&2; exit 2; }
compiler="$1"
root="$2"
test -x "$compiler"
test -f "$root/examples/atto-core-stage0.atto"
test -f "$root/examples/atto-core-stage0-deny.atto"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
answer="$("$compiler" run "$root/examples/atto-core-stage0.atto")"
test "$answer" = 42 || { echo "Forge stage-0 .atto guest returned $answer, expected 42" >&2; exit 1; }
"$compiler" ir "$root/examples/atto-core-stage0.atto" > "$tmp/core.ll"
test -s "$tmp/core.ll"
"$compiler" emit-asm x86_64 "$root/examples/atto-core-stage0.atto" > "$tmp/core.s"
test -s "$tmp/core.s"
# Main parser must reject ALL source imports: no hidden synthetic fallback
# from a source snapshot or filesystem path may masquerade as executable Core.
if "$compiler" ir "$root/examples/atto-core-stage0-deny.atto" >"$tmp/invalid.ll" 2>"$tmp/invalid.err"; then
  echo 'Forge compiler falsely admitted a canonical atto.core module import' >&2; exit 1
fi
test ! -s "$tmp/invalid.ll" || { echo 'Forge emitted partial IR on denied import' >&2; exit 1; }
grep -F 'source imports require verified module graph lowering' "$tmp/invalid.err" >/dev/null
for namespace in atto.alloc atto.std atto.runtime.actor; do
  printf 'import module x from "%s/nosuch";\npub fnc main():int{return 42;}\n' "$namespace" > "$tmp/negative.atto"
  if "$compiler" ir "$tmp/negative.atto" > "$tmp/negative.ll" 2>"$tmp/negative.err"; then
    echo "Forge unexpectedly admitted $namespace" >&2; exit 1
  fi
  test ! -s "$tmp/negative.ll"
  grep -F 'source imports require verified module graph lowering' "$tmp/negative.err" >/dev/null
done
echo 'ATTO_FORGE_STAGE0_GUEST_EXECUTED|result=42|module_imports=UNSUPPORTED|native_asm=EMITTED|native_exec=NOT_EXECUTED'
