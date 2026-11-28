#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?absolute compiler path required}"
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
src=tests/fixtures/state-intrinsics-v1/state-owned-tuple-snapshot.atto
test -s "$src"
cases=("$src")
i=0
for prefix in 'define state on actor' 'pub define state on actor' 'define pub state on actor' 'define state on class'; do
  printf '%s C as { let int count: 0; }\nfnc main(): int { return 42; }\n' "$prefix" >"$root/attached-$i.atto"
  cases+=("$root/attached-$i.atto")
  i=$((i+1))
done
for src in "${cases[@]}"; do
for mode in check run ir run-runtime ir-runtime run-calls emit-asm emit-runtime-asm emit-call-asm; do
  args=("$mode")
  if [[ "$mode" == emit-* ]]; then args+=(x86_64); fi
  if "$compiler" "${args[@]}" "$src" >"$root/out" 2>"$root/err"; then
    echo "unsafe acceptance: $mode" >&2; exit 1
  fi
  test ! -s "$root/out"
  grep -q STATE_CONTAINER_BACKEND_UNSUPPORTED "$root/err"
done
done
printf 'fnc main(): int { return 42; }\n' >"$root/control.atto"
test "$("$compiler" run "$root/control.atto")" = 42
test "$("$compiler" run-calls "$root/control.atto")" = 42
echo 'FORGE_STATE_FAIL_CLOSED_PASS: five source variants across nine CLI paths; scalar controls; NOT state guest parity'
