#!/usr/bin/env bash
# Exact-source, no-CMake libFuzzer proof of Forge actor mailbox state transitions.
# RUNS=3000 bash scripts/test-actor-mailbox-fuzz.sh
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
compiler="${CXX:-clang++}"
command -v "$compiler" >/dev/null 2>&1 || { echo "clang compiler missing" >&2; exit 2; }
runs="${RUNS:-3000}"
[[ "$runs" =~ ^[1-9][0-9]{0,5}$ ]] || { echo "RUNS must be 1-999999" >&2; exit 2; }
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -Wconversion -Werror \
  -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -I"$root" \
  "$root/runtime/actor.cpp" "$root/verification/fuzz_actor_mailbox.cpp" \
  -o "$work/oresforge_mailbox_fuzz"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
"$work/oresforge_mailbox_fuzz" -runs="$runs" -max_len=4096 \
  -timeout=3 -rss_limit_mb=1024 -seed=20261016
printf 'ORESFORGE_MAILBOX_FUZZ_PASS runs=%s compiler=%s\n' "$runs" "$compiler"
