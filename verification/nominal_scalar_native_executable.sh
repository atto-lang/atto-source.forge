#!/bin/sh
# Exact Forge compiler -> x86-64 assembly -> native executable proof.
# No cross-org carrier, dynamic library load, or private source publication.
set -eu
compiler="$1"
clang="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/box.ores" <<'ORES'
define class Box as
  let int value=40;
  add(mut self,int n):void{self.value=self.value+n;}
  read(self):int{return self.value;}
end
pub fnc main():int{
  let mut Box b=new Box();
  b.add(2);
  return b.read();
}
ORES
result="$("$compiler" run-class "$work/box.ores")"
test "$result" = 42
"$compiler" emit-class-asm x86_64 "$work/box.ores" > "$work/box.s"
cat > "$work/main.c" <<'C'
#include <stdint.h>
extern int64_t oresforge_entry(void);
int main(void) { return oresforge_entry() == 42 ? 0 : 1; }
C
"$clang" -x assembler "$work/box.s" -x c "$work/main.c" -o "$work/native"
"$work/native"

# Two independent instances and four class methods, including split mutable
# receiver syntax and subtraction. Verify the compiler's actual CLI output,
# not a stand-in assembly program.
cat > "$work/multi.ores" <<'ORES'
define class Counter as
  let int value=40;
  add(mut self,int n):void{self.value=self.value+n;}
  subtract(mut self)(int n):void{self.value=self.value-n;}
  read(self):int{return self.value;}
  get():int{return self.value;}
end
fnc main():int{
  let mut Counter a=new Counter();
  let mut Counter b=new Counter();
  a.add(8);
  b.subtract(10);
  a.subtract(6);
  b.add(12);
  return a.get();
}
ORES
result="$("$compiler" run-class "$work/multi.ores")"
test "$result" = 42
"$compiler" emit-class-asm x86_64 "$work/multi.ores" > "$work/multi.s"
"$clang" -x assembler "$work/multi.s" -x c "$work/main.c" -o "$work/multi_native"
"$work/multi_native"
echo "Forge bounded class native execution (single + multiple instances): PASS"
