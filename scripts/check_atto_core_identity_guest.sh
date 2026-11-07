#!/usr/bin/env bash
# This runs actual compiler->ASM->linked ELF, but only stage-1 Core identity.
# All other Core/Alloc/Std module syntax remains explicitly unsupported.
set -euo pipefail
test "$#" -eq 3 || { echo 'usage: ... COMPILER REPO_ROOT CXX' >&2; exit 2; }
compiler="$(realpath -- "$1")"; root="$(realpath -- "$2")"; cxx="$3"
test -x "$compiler"
# Before and after proof: guest tests must execute a stable compiler image.
# A hash printed only after execution does not identify the bytes that ran.
compiler_sha_before="$(sha256sum "$compiler" | cut -d' ' -f1)"
test -f "$root/examples/atto-core-identity.atto"
command -v "$cxx" >/dev/null
command -v readelf >/dev/null
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/atto.core"
# This local file must NEVER replace the compiler-embedded source.
printf 'define module identity as pub fnc passthrough(int x):int{return 99;} end\n' \
  > "$tmp/atto.core/identity.atto"
cp "$root/examples/atto-core-identity.atto" "$tmp/guest42.atto"
test "$("$compiler" run-core "$root/examples/atto-core-identity.atto")" = 42
printf 'import module identity from "ores.core/identity.ores"; pub fnc main():int{return identity.passthrough(43);}\n' \
  > "$tmp/guest43.atto"
(
  cd "$tmp"
  test "$("$compiler" run-core "$tmp/guest42.atto")" = 42
  test "$("$compiler" run-core "$tmp/guest43.atto")" = 43
)
"$compiler" emit-core-asm x86_64 "$tmp/guest42.atto" > "$tmp/42.s"
"$compiler" emit-core-asm x86_64 "$tmp/guest43.atto" > "$tmp/43.s"
test -s "$tmp/42.s"
grep -F '  call .Lores_fn_' "$tmp/42.s" >/dev/null
"$cxx" -x assembler -c "$tmp/42.s" -o "$tmp/42.o"
"$cxx" -x assembler -c "$tmp/43.s" -o "$tmp/43.o"
"$cxx" -std=c++20 -O2 -fPIE -pie \
  "$root/verification/atto_core_stage0_native_harness.cpp" "$tmp/42.o" -o "$tmp/guest42"
"$cxx" -std=c++20 -O2 -fPIE -pie \
  "$root/verification/atto_core_stage0_native_harness.cpp" "$tmp/43.o" -o "$tmp/guest43"
test "$("$tmp/guest42")" = 'ATTO_FORGE_NATIVE_GUEST=42'
test "$("$tmp/guest43")" = 'ATTO_FORGE_NATIVE_GUEST=43'
for binary in "$tmp/guest42" "$tmp/guest43"; do
  readelf -W -h "$binary" | grep -E 'Type: *DYN ' >/dev/null
  readelf -W -l "$binary" | awk '
    $1=="GNU_STACK" {found=1; if ($7 ~ /E/) bad=1}
    END {exit (!found || bad)}'
done
test "$(sha256sum "$tmp/42.s" | cut -d' ' -f1)" != \
     "$(sha256sum "$tmp/43.s" | cut -d' ' -f1)"
test "$(sha256sum "$tmp/guest42" | cut -d' ' -f1)" != \
     "$(sha256sum "$tmp/guest43" | cut -d' ' -f1)"
for spec in atto.core/missing atto.alloc/identity atto.std/identity \
            atto.runtime.actor/identity ./identity.atto; do
  printf 'import module identity from "%s"; fnc main():int{return 42;}\n' "$spec" \
    > "$tmp/deny.atto"
  if "$compiler" emit-core-asm x86_64 "$tmp/deny.atto" > "$tmp/deny.s" 2> "$tmp/deny.err"; then
    echo "unknown Core/host capability import was accepted: $spec" >&2; exit 1
  fi
  test ! -s "$tmp/deny.s"
  grep -F 'unknown compiler-owned atto.core module' "$tmp/deny.err" >/dev/null
done
test "$(sha256sum "$compiler" | cut -d' ' -f1)" = "$compiler_sha_before" || {
  echo 'Forge executable changed during stage-1 native test' >&2; exit 1
}
printf 'ATTO_FORGE_STAGE1_COMPILER_SHA256=%s\n' "$compiler_sha_before"
printf 'ATTO_FORGE_STAGE1_CORE_IDENTITY_EXECUTED|result=42|mutation_result=43|compiler_owned=YES|native_harness=HOSTED|guest_host_imports=NOT_VERIFIED|pie=YES|exec_stack=NO\n'
printf 'ATTO_FORGE_STAGE1_NATIVE_BINARY_SHA256=%s\n' "$(sha256sum "$tmp/guest42" | cut -d' ' -f1)"
