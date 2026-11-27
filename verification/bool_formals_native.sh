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
# Distinct fifth/sixth values and mixed types detect collision with the old
# depth register; a nested call forces depth preservation across call frames.
positive 'fnc f(int a,int b,int c,int d,int e,int f):int{return a+b+c+d+e-f;} fnc main():int{return f(1,2,3,4,37,5);}' 42
positive 'fnc f(int a,int b,int c,int d,bool e,int f):int{return e ? f : a;} fnc g(int x):int{return f(7,2,3,4,true,x);} fnc main():int{return g(42);}' 42
positive 'fnc f(int a,int b,int c,int d,int e,bool f):int{return f ? e : a;} fnc main():int{return f(7,2,3,4,42,false);}' 7
negative 'fnc f(int a,int b,int c,int d,int e,int f,int g):int{return a;} fnc main():int{return 42;}' 'function parameter budget exceeded'
negative 'fnc f(int a):int{return a;} fnc main():int{return f(1,2,3,4,5,6,7);}' 'call argument budget exceeded'
negative 'fnc main(int a,int b,int c,int d,int e):int{return a;}' 'public entry parameter budget exceeded'
negative 'fnc f(int a,int b,int c,int d,bool e,int f):int{return f;} fnc main():int{return f(1,2,3,4,5,6);}' 'int is not a Boolean argument'
# Early returns must be lazy, typed and ordered. No unselected overflow.
positive 'fnc f(bool b):bool{if b then return true; fi return false;} fnc main():int{return f(true) ? 42 : 7;}' 42
positive 'fnc f(int x):int{if x eq 0 then return 42; fi if x eq 1 then return 7; fi return 9223372036854775807+x;} fnc main():int{return f(0);}' 42
positive 'fnc f(bool b):bool{if b then return false; fi return true;} fnc main():int{return f(false) ? 42 : 7;}' 42
negative 'fnc main():int{if 1 then return 42; fi return 7;}' 'condition_not_bool'
negative 'fnc f(bool b):bool{if b then return 1; fi return false;} fnc main():int{return 42;}' 'condition_not_bool'
negative 'fnc main():int{if true then return 42; fi}' 'unexpected token'
negative 'fnc main():int{if true then return 42; return 7;}' 'unexpected token'
# Boundary: 256 literals + 255 adds = 511 nodes; 257 literals + 256 adds = 513.
# Generate a balanced expression to avoid conflating node and depth limits.
sum_tree() {
  local n=$1
  if (( n == 1 )); then printf '1'; return; fi
  printf '('
  sum_tree "$((n / 2))"
  printf '+'
  sum_tree "$((n - n / 2))"
  printf ')'
}
positive "fnc f():int{return $(sum_tree 256);} fnc main():int{return f();}" 256
negative "fnc f():int{return $(sum_tree 257);} fnc main():int{return f();}" 'callable AST node budget exceeded'
echo 'FORGE_TYPED_BOOLEAN_FORMALS_NATIVE_PASS positives=11 negatives=14'
