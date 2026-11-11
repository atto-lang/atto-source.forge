#!/usr/bin/env bash
# Executed native host measurements from a real Forge stage-0 .atto image.
# These are NOT whole-language, class, trait, actor or generic benchmarks.
set -euo pipefail
if [[ "$#" -ne 1 || ! -x "$1" ]]; then
  echo "usage: benchmarks/static_dispatch/run.sh /absolute/path/to/forge-static-dispatch-verifier" >&2
  exit 2
fi
verifier="$(realpath -- "$1")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
fixture="$root/tests/fixtures/static-dispatch/direct-42.atto"
driver="$root/benchmarks/static_dispatch/driver.cpp"
test -f "$fixture"
test -f "$driver"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
"$verifier" "$fixture" > "$work/source-proof.txt"
grep -Fq 'FORGE_STATIC_DISPATCH_AOT_SOURCE_PROOF|' "$work/source-proof.txt"
"$verifier" "$fixture" --asm > "$work/direct-42.s"
grep -Fq '  call .Lores_fn_0' "$work/direct-42.s"
grep -Fq '  call .Lores_fn_1' "$work/direct-42.s"
if grep -Eq '^[[:space:]]+(call|jmp)[[:space:]]+\*' "$work/direct-42.s"; then
  echo 'FORGE_STAGE0_BENCH_REJECT: unclosed indirect call in source-produced assembly' >&2
  exit 65
fi
printf 'FORGE_STAGE0_BENCH_SOURCE|fixture_sha256=%s|assembly_sha256=%s\n' \
  "$(sha256sum "$fixture" | cut -d ' ' -f 1)" \
  "$(sha256sum "$work/direct-42.s" | cut -d ' ' -f 1)"
for compiler in g++ clang++; do
  command -v "$compiler" >/dev/null
  "$compiler" --version | sed -n '1p'
  for opt in O0 O2; do
    output="$work/forge-${compiler//+/p}-$opt"
    start="$(date +%s%N)"
    "$compiler" "-$opt" -std=c++20 -no-pie -Wall -Wextra -Wpedantic -Werror \
      "$work/direct-42.s" "$driver" -o "$output"
    end="$(date +%s%N)"
    test -x "$output"
    size="$(stat -c %s "$output")"
    checksum="$(sha256sum "$output" | cut -d ' ' -f 1)"
    printf 'FORGE_STAGE0_BENCH_ARTIFACT|compiler=%s|opt=%s|compile_ms=%s|binary_bytes=%s|sha256=%s\n' \
      "$compiler" "$opt" "$(( (end-start)/1000000 ))" "$size" "$checksum"
    "$output"
  done
done
