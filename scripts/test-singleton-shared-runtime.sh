#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
flags=(-O2)
case "${1:-release}" in
  release) ;;
  sanitized) flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer) ;;
  *) echo 'usage: test-singleton-shared-runtime.sh [release|sanitized]' >&2; exit 2 ;;
esac
"${CXX:-c++}" --version
"${CXX:-c++}" -std=c++20 "${flags[@]}" -pthread -Wall -Wextra -Werror -I. \
  verification/singleton_shared_runtime.cpp runtime/actor.cpp -o "$test_dir/runtime"
sha256sum "$test_dir/runtime"
timeout 30 "$test_dir/runtime"
"${CXX:-c++}" -std=c++20 "${flags[@]}" -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -I. compiler/frontend.cpp compiler/ir.cpp \
  compiler/call_native.cpp verification/singleton_admission.cpp -o "$test_dir/admission"
sha256sum "$test_dir/admission"
timeout 30 "$test_dir/admission"
