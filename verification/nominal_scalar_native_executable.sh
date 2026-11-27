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

# Compiler-backed contextual spelling matrix for the bounded Forge profile.
# Each valid source is interpreted, emitted, compared to the same native
# instruction stream, independently linked, and executed. Obsolete markers
# must fail in both compiler modes without emitting any output.
for name in read mtd method; do
  for trivia in space tab newline block nested line; do
    case "$trivia" in
      space) separator=' ' ;;
      tab) separator="$(printf '\t')" ;;
      newline) separator='
' ;;
      block) separator=' /* declaration marker */ ' ;;
      nested) separator=' /* outer /* inner */ outer */ ' ;;
      line) separator=' // declaration marker
' ;;
    esac
    for marker in mtd fnc method; do
      printf 'define class Box as\n  let int value=40;\n  add(mut self,int n):void{self.value=self.value+n;}\n  pub %s%s%s(self):int{return self.value;}\nend\npub fnc main():int{let mut Box b=new Box();b.add(2);return b.%s();}\n' \
        "$marker" "$separator" "$name" "$name" > "$work/contextual.atto"
      if [ "$marker" = method ]; then
        for mode in run emit; do
          if [ "$mode" = run ]; then
            set -- run-class "$work/contextual.atto"
          else
            set -- emit-class-asm x86_64 "$work/contextual.atto"
          fi
          if "$compiler" "$@" > "$work/rejected.out" 2> "$work/rejected.err"; then
            echo "unexpected obsolete marker acceptance: $name/$trivia/$mode" >&2
            exit 1
          fi
          test ! -s "$work/rejected.out"
          grep -F 'obsolete method introducer' "$work/rejected.err" > /dev/null
        done
      else
        test "$("$compiler" run-class "$work/contextual.atto")" = 42
        "$compiler" emit-class-asm x86_64 "$work/contextual.atto" > "$work/contextual.s"
        cmp "$work/box.s" "$work/contextual.s"
        "$clang" -x assembler "$work/contextual.s" -x c "$work/main.c" -o "$work/contextual_native"
        "$work/contextual_native"
      fi
    done
  done
done
echo 'Forge contextual method matrix: 36 native guests and 36 diagnostic/no-output checks passed'
