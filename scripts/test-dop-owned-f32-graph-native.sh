#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cxx="${CXX:-g++}"
mode="${BUILD_MODE:-release}"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

flags=(-std=c++20 -pthread "-I$repo_root" -Wall -Wextra -Wpedantic -Werror)
case "$mode" in
  release)
    flags+=(-O2 -DNDEBUG)
    ;;
  asan-ubsan)
    flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
    ;;
  tsan)
    flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=thread)
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
    ;;
  *)
    printf 'unsupported BUILD_MODE=%s\n' "$mode" >&2
    exit 2
    ;;
esac

tests=(
  dop_owned_f32_v1_test
  execution_graph_v1_admission_security_test
  execution_graph_v1_test
  dop_owned_graph_v1_test
)
for test_name in "${tests[@]}"; do
  source_file="$repo_root/verification/${test_name}.cpp"
  if [[ ! -f "$source_file" ]]; then
    printf 'missing native DOP test: %s\n' "$source_file" >&2
    exit 3
  fi
  "$cxx" "${flags[@]}" "$source_file" -o "$work_dir/$test_name"
  "$work_dir/$test_name"
done

printf 'PASS Forge DOP direct native suite: compiler=%s mode=%s source=%s tree=%s\n' \
  "$cxx" "$mode" "$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || printf unknown)" \
  "$(git -C "$repo_root" rev-parse 'HEAD^{tree}' 2>/dev/null || printf unknown)"
