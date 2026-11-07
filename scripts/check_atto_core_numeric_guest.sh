#!/usr/bin/env bash
# Compiler-backed Forge x86-64 native stage-2 numeric.atto (hosted harness).
set -euo pipefail
test "$#" -eq 3 || { echo 'usage: COMPILER REPO_ROOT CXX' >&2; exit 2; }
compiler="$(realpath -- "$1")"; root="$(realpath -- "$2")"; cxx="$3"
test -x "$compiler"
# Before and after proof: guest tests must execute a stable compiler image.
# A hash printed only after execution does not identify the bytes that ran.
compiler_sha_before="$(sha256sum "$compiler" | cut -d' ' -f1)"
command -v "$cxx" >/dev/null
command -v readelf >/dev/null
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/atto.core"
printf 'define module numeric as pub fnc clamp_int(int a,int b,int c):int{return 99;} end\n' > "$tmp/atto.core/numeric.atto"
cp "$root/examples/atto-core-numeric.atto" "$tmp/42.atto"
printf 'import module numeric from "ores.core/numeric.ores"; pub fnc main():int{return numeric.clamp_int(50,0,43);}\n' > "$tmp/43.atto"
(
  cd "$tmp"
  test "$("$compiler" run-core "$tmp/42.atto")" = 42
  test "$("$compiler" run-core "$tmp/43.atto")" = 43
)
"$compiler" emit-core-asm x86_64 "$tmp/42.atto" > "$tmp/42.s"
"$compiler" emit-core-asm x86_64 "$tmp/43.atto" > "$tmp/43.s"
for case in 42 43; do
  grep -F '  call .Lores_fn_' "$tmp/$case.s" >/dev/null
  "$cxx" -x assembler -c "$tmp/$case.s" -o "$tmp/$case.o"
  "$cxx" -std=c++20 -O2 -fPIE -pie \
    "$root/verification/atto_core_stage0_native_harness.cpp" \
    "$tmp/$case.o" -o "$tmp/$case"
  test "$("$tmp/$case")" = "ATTO_FORGE_NATIVE_GUEST=$case"
  readelf -W -h "$tmp/$case" | grep -E 'Type: *DYN ' >/dev/null
  readelf -W -l "$tmp/$case" | awk '
    $1=="GNU_STACK" {found=1; if ($7 ~ /E/) bad=1}
    END {exit (!found || bad)}'
done
test "$(sha256sum "$tmp/42.s" | cut -d' ' -f1)" != \
     "$(sha256sum "$tmp/43.s" | cut -d' ' -f1)"
test "$(sha256sum "$tmp/42" | cut -d' ' -f1)" != \
     "$(sha256sum "$tmp/43" | cut -d' ' -f1)"
for spec in atto.core/missing atto.core/numeric/ atto.core/../numeric \
            atto.alloc/numeric atto.std/numeric atto.runtime.actor/numeric \
            ./numeric.atto; do
  printf 'import module numeric from "%s";fnc main():int{return 42;}\n' "$spec" > "$tmp/reject.atto"
  if "$compiler" emit-core-asm x86_64 "$tmp/reject.atto" > "$tmp/reject.s" 2>"$tmp/reject.err"; then
    echo "invalid numeric import accepted: $spec" >&2; exit 1
  fi
  test ! -s "$tmp/reject.s"
  grep -F 'unknown compiler-owned atto.core module' "$tmp/reject.err" >/dev/null
done
test "$(sha256sum "$compiler" | cut -d' ' -f1)" = "$compiler_sha_before" || {
  echo 'Forge executable changed during stage-2 numeric guest test' >&2; exit 1
}
printf 'ATTO_FORGE_STAGE2_COMPILER_SHA256=%s\n' "$compiler_sha_before"
# Parameterized source programs stay independent of the C++ reference
# oracle. Each compiler-emitted entry executes 10k deterministic i64 inputs.
# In particular the first 729 cases cover all triples from the boundary
# pool, including reversed clamp bounds and signed i64 extremes.
for mode in min max clamp; do
  case "$mode" in
    min) expression='numeric.min_int(left,right)'; signature='int left,int right'; checksum='a52c659779088c2b';;
    max) expression='numeric.max_int(left,right)'; signature='int left,int right'; checksum='bb3870b3c10fd615';;
    clamp) expression='numeric.clamp_int(value,lower,upper)'; signature='int value,int lower,int upper'; checksum='e71b3e7766936548';;
  esac
  printf 'import module numeric from "atto.core/numeric"; pub fnc main(%s):int{return %s;}\n' \
    "$signature" "$expression" > "$tmp/param-$mode.atto"
  "$compiler" emit-core-asm x86_64 "$tmp/param-$mode.atto" > "$tmp/param-$mode.s"
  test -s "$tmp/param-$mode.s"
  grep -F '  call .Lores_fn_' "$tmp/param-$mode.s" >/dev/null
  "$cxx" -x assembler -c "$tmp/param-$mode.s" -o "$tmp/param-$mode.o"
  upper_mode="${mode^^}"
  "$cxx" -std=c++20 -Wall -Wextra -Werror -O2 -fPIE -pie \
    "-DATTO_NUMERIC_$upper_mode" \
    "$root/verification/atto_core_numeric_differential_harness.cpp" \
    "$tmp/param-$mode.o" -o "$tmp/param-$mode"
  "$tmp/param-$mode" > "$tmp/param-$mode.log"
  grep -Fx "ATTO_FORGE_NUMERIC_DIFFERENTIAL_EXECUTED|mode=$mode|cases=10000|checksum=$checksum|native=YES|hosted_harness=YES" \
    "$tmp/param-$mode.log" >/dev/null
  readelf -W -h "$tmp/param-$mode" | grep -E 'Type: *DYN ' >/dev/null
  readelf -W -l "$tmp/param-$mode" | awk '
    $1=="GNU_STACK" {found=1; if ($7 ~ /E/) bad=1}
    END {exit (!found || bad)}'
  cat "$tmp/param-$mode.log"
  printf 'ATTO_FORGE_NUMERIC_DIFF_ELF_SHA256|mode=%s|sha256=%s\n' \
    "$mode" "$(sha256sum "$tmp/param-$mode" | cut -d' ' -f1)"
done
printf 'ATTO_FORGE_STAGE2_NUMERIC_EXECUTED|result=42|mutation_result=43|canonical_resource=YES|native_harness=HOSTED|guest_imports=NOT_VERIFIED|pie=YES|exec_stack=NO\n'
printf 'ATTO_FORGE_STAGE2_NATIVE_BINARY_SHA256=%s\n' "$(sha256sum "$tmp/42" | cut -d' ' -f1)"
