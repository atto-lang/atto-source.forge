#!/usr/bin/env bash
# Exact repository actor mailbox proof without configuring compiler/JIT.
# Invocation: bash scripts/test-actor-mailbox-standalone.sh
# Optional: CXX=clang++ SANITIZE=1 bash scripts/test-actor-mailbox-standalone.sh
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
compiler="${CXX:-g++}"
command -v "$compiler" >/dev/null 2>&1 || {
  printf 'Missing C++ compiler: %s\n' "$compiler" >&2
  exit 2
}
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
flags=(-std=c++20 -O1 -g -pthread -Wall -Wextra -Wconversion -Werror
       -D_GLIBCXX_ASSERTIONS)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fno-omit-frame-pointer -fsanitize=address,undefined)
  export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
  export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
elif [[ "${SANITIZE:-0}" != 0 ]]; then
  echo 'SANITIZE must be 0 or 1' >&2
  exit 2
fi
for name in actor_mailbox_limits actor_mailbox_concurrency actor_mailbox_model actor_mailbox_destructor; do
  "$compiler" "${flags[@]}" -I"$root" \
    "$root/runtime/actor.cpp" "$root/verification/$name.cpp" -o "$work/$name"
  timeout 65 "$work/$name"
done
echo "ACTOR_MAILBOX_SOURCE_TEST_PASS compiler=$compiler sanitize=${SANITIZE:-0}"
