#!/usr/bin/env bash
# Compiler-backed Forge x86-64 native stage-2 numeric.atto (hosted harness).
set -euo pipefail
test "$#" -eq 3 || { echo 'usage: COMPILER REPO_ROOT CXX' >&2; exit 2; }
compiler="$1"; root="$2"; cxx="$3"
test -x "$compiler"
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
  test "$("$compiler" run-core-numeric "$tmp/42.atto")" = 42
  test "$("$compiler" run-core-numeric "$tmp/43.atto")" = 43
)
"$compiler" emit-core-numeric-asm x86_64 "$tmp/42.atto" > "$tmp/42.s"
"$compiler" emit-core-numeric-asm x86_64 "$tmp/43.atto" > "$tmp/43.s"
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
  if "$compiler" emit-core-numeric-asm x86_64 "$tmp/reject.atto" > "$tmp/reject.s" 2>"$tmp/reject.err"; then
    echo "invalid numeric import accepted: $spec" >&2; exit 1
  fi
  test ! -s "$tmp/reject.s"
  grep -F 'unknown compiler-owned atto.core module' "$tmp/reject.err" >/dev/null
done
printf 'ATTO_FORGE_STAGE2_NUMERIC_EXECUTED|result=42|mutation_result=43|canonical_resource=YES|native_harness=HOSTED|guest_imports=NOT_VERIFIED|pie=YES|exec_stack=NO\n'
printf 'ATTO_FORGE_STAGE2_NATIVE_BINARY_SHA256=%s\n' "$(sha256sum "$tmp/42" | cut -d' ' -f1)"
