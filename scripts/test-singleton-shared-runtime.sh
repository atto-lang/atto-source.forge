#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
flags=(-O2)
profile="${1:-release}"
case "$profile" in
  release) ;;
  sanitized|sanitized-leaks)
    flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    ;;
  *) echo 'usage: test-singleton-shared-runtime.sh [release|sanitized|sanitized-leaks]' >&2; exit 2 ;;
esac
"${CXX:-c++}" --version

if [[ "$profile" == sanitized-leaks ]]; then
  cat >"$test_dir/lsan_probe.cpp" <<'CPP'
#include <cstdio>
[[gnu::noinline]] void leak_once() {
  auto* value = new unsigned char[64];
  value[0] = 42;
  std::printf("%u\n", static_cast<unsigned>(value[0]));
}
int main() { leak_once(); }
CPP
  "${CXX:-c++}" -std=c++20 "${flags[@]}" "$test_dir/lsan_probe.cpp" -o "$test_dir/lsan_probe"
  set +e
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    LSAN_OPTIONS=exitcode=23 \
    "$test_dir/lsan_probe" >"$test_dir/lsan_probe.out" 2>"$test_dir/lsan_probe.err"
  probe_status=$?
  set -e
  if [[ "$probe_status" -ne 23 ]] || ! grep -q 'LeakSanitizer' "$test_dir/lsan_probe.err"; then
    cat "$test_dir/lsan_probe.out" >&2 || true
    cat "$test_dir/lsan_probe.err" >&2 || true
    echo "LeakSanitizer capability probe did not produce the required exit 23" >&2
    exit 1
  fi
  export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
  export LSAN_OPTIONS=exitcode=23
fi
"${CXX:-c++}" -std=c++20 "${flags[@]}" -pthread -Wall -Wextra -Werror -I. \
  verification/singleton_shared_runtime.cpp runtime/actor.cpp -o "$test_dir/runtime"
sha256sum "$test_dir/runtime"
timeout 30 "$test_dir/runtime"
"${CXX:-c++}" -std=c++20 "${flags[@]}" -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -I. compiler/frontend.cpp compiler/ir.cpp \
  compiler/call_native.cpp verification/singleton_admission.cpp -o "$test_dir/admission"
sha256sum "$test_dir/admission"
timeout 30 "$test_dir/admission"
