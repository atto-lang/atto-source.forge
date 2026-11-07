#!/usr/bin/env bash
# Compiles source through the REAL Forge stage-0 frontend, then executes its
# interpreted scalar result and verifies native assembly emission. This is not
# cross-module or zero-libc/OS parity.
set -euo pipefail
test "$#" -ge 2 && test "$#" -le 3 || { echo 'usage: check_atto_core_stage0_guest.sh COMPILER REPO_ROOT [CXX]' >&2; exit 2; }
cxx="${3:-${CXX:-c++}}"
command -v "$cxx" >/dev/null || { echo "missing native C++ compiler: $cxx" >&2; exit 2; }
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
"$cxx" -x assembler -c "$tmp/core.s" -o "$tmp/core.o"
"$cxx" -std=c++20 -O2 -fPIE -pie "$root/verification/atto_core_stage0_native_harness.cpp" \
  "$tmp/core.o" -o "$tmp/native-guest"
command -v readelf >/dev/null || { echo 'readelf required for ELF stack audit' >&2; exit 1; }
readelf -W -l "$tmp/native-guest" > "$tmp/elf-headers"
grep -E '^  *GNU_STACK ' "$tmp/elf-headers" >/dev/null
awk '$1 == "GNU_STACK" { found=1; if ($7 ~ /E/) executable=1 } END { exit (!found || executable) }' "$tmp/elf-headers" || {
  echo 'native binary has executable stack or lacks GNU_STACK' >&2; exit 1
}
readelf -W -h "$tmp/native-guest" | grep -E 'Type: *DYN ' >/dev/null || {
  echo 'native ELF is not a PIE executable' >&2; exit 1
}
native_answer="$("$tmp/native-guest")"
test "$native_answer" = 'ATTO_FORGE_NATIVE_GUEST=42' || {
  echo "Forge native generated assembly returned unexpected value: $native_answer" >&2
  exit 1
}
# Prove actual backend output changes for altered source, not a fixed stub.
printf 'pub fnc main():int{return 43;}\n' > "$tmp/mutated.atto"
"$compiler" emit-asm x86_64 "$tmp/mutated.atto" > "$tmp/mutated.s"
"$cxx" -x assembler -c "$tmp/mutated.s" -o "$tmp/mutated.o"
"$cxx" -std=c++20 -O2 -fPIE -pie "$root/verification/atto_core_stage0_native_harness.cpp" \
  "$tmp/mutated.o" -o "$tmp/mutated-guest"
test "$("$tmp/mutated-guest")" = 'ATTO_FORGE_NATIVE_GUEST=43' || {
  echo 'source mutation did not propagate through native codegen and execution' >&2; exit 1
}
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
printf 'ATTO_FORGE_NATIVE_ASM_SHA256=%s\n' "$(sha256sum "$tmp/core.s" | cut -d' ' -f1)"
printf 'ATTO_FORGE_NATIVE_BINARY_SHA256=%s\n' "$(sha256sum "$tmp/native-guest" | cut -d' ' -f1)"
echo 'ATTO_FORGE_STAGE0_GUEST_EXECUTED|result=42|module_imports=UNSUPPORTED|native_asm=EMITTED|native_exec=EXECUTED|mutation_result=43|pie=YES|exec_stack=NO'
