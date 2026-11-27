#!/usr/bin/env bash
# Actual compiler/link/guest gate; no host-model substitution or filesystem I/O.
set -euo pipefail
if [[ $# != 2 ]]; then
  echo "usage: $0 /absolute/path/to/oresforge /absolute/path/to/c++" >&2
  exit 2
fi
compiler=$1
cxx=$2
[[ "$compiler" = /* && -x "$compiler" && "$cxx" = /* && -x "$cxx" ]]
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cat > "$work/driver.cpp" <<'CPP'
#include <cstdio>
extern "C" long long oresforge_entry(void);
int main() {
    const auto actual = oresforge_entry();
    std::printf("%lld\n", actual);
    return actual == 42 ? 0 : 1;
}
CPP
failed=0
for module in fs_api_policy fs_io_policy; do
  library="$root/stdlib/std/$module.atto"
  fixture="$root/fixtures/fs/${module}_main.atto"
  source="$work/$module.atto"
  assembly="$work/$module.s"
  guest="$work/$module"
  # Deliberate derived single unit; not a module-import or permission test.
  { cat "$library"; printf '\n'; cat "$fixture"; } > "$source"
  sha256sum "$library" "$fixture" "$source"
  if timeout --kill-after=5s 60s "$compiler" emit-call-asm x86_64 "$source" > "$assembly" 2> "$work/$module.err"; then
    if [[ ! -s "$assembly" || -s "$work/$module.err" ]]; then
      echo "NATIVE_PURE_POLICY_INVALID_EMISSION=$module" >&2
      cat "$work/$module.err" >&2
      failed=1
      continue
    fi
  else
    status=$?
    cat "$work/$module.err" >&2
    if [[ -s "$assembly" ]]; then
      echo "NATIVE_PURE_POLICY_PARTIAL_OUTPUT=$module" >&2
    fi
    echo "NATIVE_PURE_POLICY_COMPILER_FAILURE=$module exit=$status" >&2
    failed=1
    continue
  fi
  if ! timeout --kill-after=5s 60s "$cxx" -x assembler "$assembly" -x c++ "$work/driver.cpp" -o "$guest"; then
    echo "NATIVE_PURE_POLICY_LINK_FAILURE=$module" >&2
    failed=1
    continue
  fi
  if timeout --kill-after=5s 15s "$guest" > "$work/$module.out" 2> "$work/$module.guest.err" &&
      cmp -s "$work/$module.out" <(printf '42\n') &&
      [[ ! -s "$work/$module.guest.err" ]]; then
    echo "NATIVE_PURE_POLICY_GUEST_PASS=$module"
  else
    echo "NATIVE_PURE_POLICY_GUEST_FAILURE=$module" >&2
    cat "$work/$module.out" "$work/$module.guest.err" >&2
    failed=1
  fi
done
exit "$failed"
