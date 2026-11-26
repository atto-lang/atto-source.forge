#!/usr/bin/env bash
# Actual Forge source lowering, emitted assembly, and independently linked guest.
# This stage-0 profile constant-folds the entry; no native generic frame claim.
set -Eeuo pipefail
compiler="$(realpath "$1")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"
cat > driver.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
extern int64_t oresforge_entry(void);
int main(void) { return printf("%" PRId64 "\n",oresforge_entry())<0; }
EOF
pre='fnc identity<T>(T value): T { return value; }'
count=0
for expr in 'identity<int>(42)' 'identity<T=int>(42)' 'identity<T=int>(value:42)' 'identity(42)' 'identity<T=int>(20)+identity<int>(22)'; do
  printf '%s\npub fnc main(): int { return %s; }\n' "$pre" "$expr" > main.atto
  test "$("$compiler" run main.atto)" = 42
  "$compiler" ir main.atto > main.ir
  test -s main.ir
  "$compiler" emit-asm x86_64 main.atto > main.s
  cc -std=c11 -Wall -Wextra -Werror driver.c main.s -o guest
  test "$(./guest)" = 42
  printf 'FORGE_GUEST_PASS %s\n' "$expr"
  count=$((count+1))
done
for expr in 'identity<U=int>(42)' 'identity<T=3>(42)' 'identity<T=bool>(true)' 'identity<T=int,T=int>(42)' 'identity[T=int](42)' 'identity<T=int](42)' 'identity<T=int>(true)' 'identity<T=int>(other:42)' 'identity<T=int>(value:42,value:42)'; do
  printf '%s\npub fnc main(): int { return %s; }\n' "$pre" "$expr" > main.atto
  for mode in run ir emit-asm; do
    args=("$mode"); if [[ "$mode" == emit-asm ]]; then args+=(x86_64); fi
    status=0
    "$compiler" "${args[@]}" main.atto > out 2> err || status=$?
    test "$status" -ne 0 && test ! -s out && test -s err
  done
  printf 'FORGE_REJECT_PASS %s\n' "$expr"
  count=$((count+1))
done
test "$count" -eq 14
printf 'FORGE_GENERIC_VERIFIED %s cases\n' "$count"
