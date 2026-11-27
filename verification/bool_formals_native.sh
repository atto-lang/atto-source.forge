#!/usr/bin/env bash
# Linux x86_64: exercise real internal Boolean formal ABI and typed rejections.
set -euo pipefail
compiler=$1
cxx=$2
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cat > "$work/driver.cpp" <<'CPP'
#include <cstdio>
extern "C" long long oresforge_entry(void);
int main() { return std::printf("%lld\n", oresforge_entry()) < 0 ? 1 : 0; }
CPP
positive() {
  local src=$1 expected=$2
  printf '%s\n' "$src" > "$work/case.atto"
  timeout 20 "$compiler" run-calls "$work/case.atto" > "$work/model.out"
  timeout 20 "$compiler" emit-call-asm x86_64 "$work/case.atto" > "$work/case.s"
  grep -q 'call .Lores_fn_' "$work/case.s"
  timeout 30 "$cxx" "$work/case.s" "$work/driver.cpp" -o "$work/guest"
  timeout 10 "$work/guest" > "$work/guest.out"
  printf '%s\n' "$expected" > "$work/expected"
  cmp "$work/model.out" "$work/expected"
  cmp "$work/guest.out" "$work/expected"
}
negative() {
  local src=$1 diag=$2
  printf '%s\n' "$src" > "$work/case.atto"
  local status=0
  timeout 20 "$compiler" emit-call-asm x86_64 "$work/case.atto" > "$work/bad.s" 2> "$work/bad.err" || status=$?
  test "$status" = 1
  test ! -s "$work/bad.s"
  grep -Fq "$diag" "$work/bad.err"
}
positive 'fnc f(bool b):bool{return b;} fnc main():int{return f(true) ? 42 : 7;}' 42
positive 'fnc f(b:bool, x:int):int{return b ? x : 7;} fnc main():int{return f(false,42);}' 7
positive 'fnc f(bool a,int x,bool b,int y):int{return (a && b) ? x : y;} fnc main():int{return f(true,42,true,7);}' 42
positive 'fnc f(bool b):bool{return b;} fnc g(bool b):bool{return f(b) eq false;} fnc main():int{return g(false) ? 42 : 7;}' 42
negative 'fnc f(bool b):bool{return b;} fnc main():int{return f(1) ? 42 : 7;}' 'int is not a Boolean argument'
negative 'fnc f(int x):int{return x;} fnc main():int{return f(true);}' 'Boolean is not an int argument'
negative 'fnc f(bool b):int{return b+1;} fnc main():int{return 42;}' 'Boolean is not an int'
negative 'fnc main(bool b):int{return b ? 42 : 7;}' 'Boolean entry parameter requires explicit int adapter'
negative 'fnc f(bool b):bool{return 1;} fnc main():int{return 42;}' 'condition_not_bool'
echo 'FORGE_TYPED_BOOLEAN_FORMALS_NATIVE_PASS positives=4 negatives=5'
