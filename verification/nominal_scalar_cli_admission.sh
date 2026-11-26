#!/bin/sh
set -eu
compiler="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/box.atto" <<'ATTO'
define class Box as
  let int value=40;
  mtd add(mut self,int n):void{self.value=self.value+n;}
  mtd method():int{return self.value;}
end
pub fnc main():int{let mut Box b=new Box();b.add(2);return b.method();}
ATTO
test "$("$compiler" run-class "$work/box.atto")" = 42
reject() {
  diagnostic="$1"
  shift
  if "$compiler" "$@" > "$work/stdout" 2> "$work/stderr"; then
    echo 'unexpected nominal CLI acceptance' >&2
    exit 1
  fi
  test ! -s "$work/stdout"
  grep -F "$diagnostic" "$work/stderr" > /dev/null
}
sed 's/mtd add/method add/' "$work/box.atto" > "$work/obsolete.atto"
reject 'obsolete method introducer' run-class "$work/obsolete.atto"
reject 'obsolete method introducer' emit-class-asm x86_64 "$work/obsolete.atto"
reject 'supports only x86_64 ELF' emit-class-asm arm64 "$work/box.atto"
reject 'expected .atto input' run-class "$work/box.txt"
mkfifo "$work/fifo.atto"
reject 'source must be a regular file' run-class "$work/fifo.atto"
dd if=/dev/zero of="$work/large.atto" bs=1048576 count=2 2>/dev/null
reject 'nominal source exceeds 1 MiB limit' run-class "$work/large.atto"
echo 'Forge nominal CLI: 1 positive and 6 negative cases passed'
