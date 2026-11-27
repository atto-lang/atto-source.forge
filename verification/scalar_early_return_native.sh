#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
cxx="$2"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
cat > "$work/driver.cpp" <<'CPP'
#include <cstdint>
#include <iostream>
extern "C" std::int64_t oresforge_entry();
int main() { std::cout << oresforge_entry() << '\n'; }
CPP
cat > "$work/positive.atto" <<'ATTO'
fnc minimum(int a, int b): int {
  if a <= b then return a; fi
  return b;
}
fnc prefix(int n): int {
  do if n == 1 then return 11; fi
  return 9223372036854775807 + n;
}
fnc suffix(int n): int {
  if n == 2 do return 13; fi
  return 9223372036854775807 + n;
}
fnc braces(int n): int {
  if n == 3 { return 15; }
  return 9223372036854775807 + n;
}
pub fnc main(): int {
  return minimum(3, 4) + prefix(1) + suffix(2) + braces(3);
}
ATTO
"$compiler" emit-call-asm x86_64 "$work/positive.atto" > "$work/positive.s"
test -s "$work/positive.s"
"$cxx" -x assembler "$work/positive.s" -x c++ "$work/driver.cpp" -o "$work/positive"
printf '42\n' > "$work/expected"
"$work/positive" > "$work/stdout" 2> "$work/stderr"
cmp "$work/expected" "$work/stdout"
test ! -s "$work/stderr"
# Both true and false paths must actually execute, including fallthrough.
cat > "$work/fallthrough.atto" <<'ATTO'
fnc choose(int n): int {
  if n == 0 then return 99; fi
  if n == 1 do return 98; fi
  if n == 2 { return 97; }
  return 42;
}
pub fnc main(): int { return choose(3); }
ATTO
"$compiler" emit-call-asm x86_64 "$work/fallthrough.atto" > "$work/fallthrough.s"
"$cxx" -x assembler "$work/fallthrough.s" -x c++ "$work/driver.cpp" -o "$work/fallthrough"
"$work/fallthrough" > "$work/stdout" 2> "$work/stderr"
cmp "$work/expected" "$work/stdout"
test ! -s "$work/stderr"
negative=0
for body in 'if 1 then return 1; fi return 42;' 'if true then return 1; return 42;' 'if true then return true; fi return 42;' 'if true then return 42; fi' 'if true then let x = 1; fi return 42;' 'if true then return 1; else return 2; fi return 42;'; do
  printf 'pub fnc main(): int { %s }\n' "$body" > "$work/negative.atto"
  status=0
  "$compiler" emit-call-asm x86_64 "$work/negative.atto" > "$work/negative.s" 2> "$work/negative.err" || status=$?
  test "$status" = 1
  test ! -s "$work/negative.s"
  test -s "$work/negative.err"
  negative=$((negative+1))
done
echo "FORGE_EARLY_RETURN positive=2 negative=$negative failures=0"
