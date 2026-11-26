#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
compiler="$(realpath "$1")"
corpus="$(realpath "$2")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
test "$(git hash-object shared/oreslang/backend/CountedReadyGuest.hpp)" = f7d5a7b864ec2dbe443ac97d6ca4943445604b20
test "$(git hash-object shared/oreslang/runtime/NativeCountedI64Cursor.hpp)" = 3a1b3ff4cf235b721edf9dc64c1af411a94a74ee
test "$(git hash-object shared/oreslang/runtime/NativeSelectArbiter.hpp)" = 2d85351e66e55aae19eb5e6a08a434643c2dc4e0
test "$(git hash-object shared/oreslang/runtime/NativeSelectableI64Channel.hpp)" = bcb3a3eb9e0504f4bfa4808531a17126cacd0325
test "$(git hash-object shared/oreslang/runtime/NativeSelectableI64Future.hpp)" = 0c972dc190fc25533792a928461298492ba3b5ea
python3 - "$corpus" <<'PY'
import json,sys,pathlib,hashlib
root=pathlib.Path(sys.argv[1])
cases=[c for c in json.loads((root/'manifest.json').read_text())['cases'] if c['id'].startswith(('counted-','reject-counted-'))]
assert len(cases)==17 and sum(c['target']['mode']=='run' for c in cases)==10
for c in cases:
    raw=(root/c['path']).read_bytes()
    assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==c['gitBlobSha1']
PY
for name in counted-three counted-zero counted-one counted-alias counted-dynamic counted-dynamic-zero counted-mixed-future-alias counted-multi-channel-fair counted-mixed-zero counted-distinct-alias-projection; do
  "$compiler" --counted-ready-v1 "$corpus/$name.atto" > "$work/guest.cpp"
  c++ -std=c++17 -O2 -pthread -I shared -S "$work/guest.cpp" -o "$work/guest.s"
  c++ -pthread "$work/guest.s" -o "$work/guest"
  test "$("$work/guest")" = 42
  echo "FORGE_SHARED_GUEST_PASS $name"
done
for name in reject-counted-negative reject-counted-bool reject-counted-scalar reject-counted-default reject-counted-write reject-counted-dynamic-bool reject-counted-dynamic-unknown; do
  if "$compiler" --counted-ready-v1 "$corpus/$name.atto" > "$work/denial" 2> "$work/err"; then exit 1; fi
  test ! -s "$work/denial"
  grep -F E_COUNTED_READY_PROFILE "$work/err"
done
for case in invalid-count overflow-before-await empty-read would-suspend capacity; do
  case "$case" in
    invalid-count) body='val int count = 17; val Array[int] b = await select<int>(count) { when readch a: int n -> { return n; } }; return b[0];'; expected=SelectCountInvalid;;
    overflow-before-await) body='writech a, 42; val Future<Array<int>> pending = select<int>(1) { when readch a: int n -> { return n; } }; val int x = 9223372036854775807 + 1; val Array[int] b = await pending; return b[0];'; expected=E_I64_OVERFLOW;;
    empty-read) body='val Array[int] b = await select<int>(0) { when readch a: int n -> { return n; } }; return readch a;'; expected=E_READY_ONLY;;
    would-suspend) body='val Array[int] b = await select<int>(1) { when readch a: int n -> { return n; } }; return b[0];'; expected=E_READY_ONLY;;
    capacity) body='writech a, 42; writech a, 43; val Array[int] b = await select<int>(1) { when readch a: int n -> { return n; } }; return b[0];'; expected=E_READY_ONLY;;
  esac
  printf 'suspends fnc main(): int { val Channel<int> a = Channel.new<int>(1); %s }\n' "$body" > "$work/$case.atto"
  "$compiler" --counted-ready-v1 "$work/$case.atto" > "$work/guest.cpp"
  clang++ -std=c++17 -O1 -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -I shared "$work/guest.cpp" -o "$work/guest"
  status=0
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/guest" > "$work/out" 2> "$work/err" || status=$?
  test "$status" = 2
  test ! -s "$work/out"
  grep -F "$expected" "$work/err"
  ! grep -E 'AddressSanitizer|runtime error:|LeakSanitizer' "$work/err"
  echo "FORGE_SHARED_RUNTIME_REJECT_PASS $case"
done
echo FORGE_SHARED_COUNTED_NATIVE_PASS
