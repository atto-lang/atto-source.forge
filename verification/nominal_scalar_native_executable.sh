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
echo "Forge bounded class native execution: PASS"
