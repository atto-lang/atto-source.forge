#!/usr/bin/env python3
"""Execute real Forge-emitted x86-64 assembly with malloc/free instrumentation.

Exact-head CI must run this; no source-only model can substitute.
"""
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile

SOURCE = """\
define struct Point as
    int x
    int y
end
define pub fnc main(a:int,b:int):int {
    let p = Point { x: a, y: b };
    return p.x + p.y;
}
"""
DRIVER = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
extern int64_t oresforge_alloc_entry(int64_t, int64_t);
extern void* __real_malloc(size_t);
extern void __real_free(void*);
static unsigned long allocations=0, deallocations=0;
void* __wrap_malloc(size_t n) { ++allocations; return __real_malloc(n); }
void __wrap_free(void* p) { ++deallocations; __real_free(p); }
int main(int argc, char** argv) {
  if (argc>1 && strcmp(argv[1], "overflow")==0) {
    volatile int64_t result=oresforge_alloc_entry(INT64_MAX,1);
    return (int)result; // Must never return normally.
  }
  int safe=argc>1 && strcmp(argv[1], "safe")==0;
  const int64_t pairs[][2]={
    {10,20},{0,0},{-14,42},{-999,1000},{INT64_MAX,-1},{INT64_MIN,1}
  };
  for (size_t i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i) {
    int64_t x=pairs[i][0], y=pairs[i][1];
    int64_t result=oresforge_alloc_entry(x,y);
    if (result!=x+y) { fprintf(stderr,"wrong native value at %zu\n",i); return 1; }
  }
  const unsigned long expected=safe?0:sizeof(pairs)/sizeof(pairs[0]);
  if (allocations!=expected || deallocations!=expected) {
    fprintf(stderr,"wrong malloc/free counts: %lu/%lu expected %lu\n",
            allocations,deallocations,expected);
    return 2;
  }
  printf("native cases=%zu malloc=%lu free=%lu\n",
         sizeof(pairs)/sizeof(pairs[0]),allocations,deallocations);
  return 0;
}
"""


def call(args, *, env=None):
    return subprocess.run(args, capture_output=True, text=True,
                          timeout=20, check=False, env=env)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run_compiler(exe, argv):
    env = os.environ.copy()
    env.pop("ORES_OPT_ESCAPE", None)
    env.pop("ORES_JIT", None)
    env.pop("ORES_OPT_ESCAPE_REPORT", None)
    return call([exe, *argv], env=env)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: native_alloc_executable.py <oresforge>")
    if sys.platform != "linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise SystemExit("native allocation fixture requires Linux x86-64")
    compiler = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="forge-native-alloc-") as d:
        folder = Path(d)
        source = folder / "point.ores"
        source.write_text(SOURCE, encoding="utf-8")
        (folder / "driver.c").write_text(DRIVER, encoding="utf-8")
        ir = run_compiler(compiler, ["ir-alloc", str(source)])
        require(ir.returncode == 0 and "closed-world verified" in ir.stdout,
                f"allocation IR did not compile: {ir!r}")
        for mode, allocations in (("off", 6), ("safe", 0)):
            command = ["--opt-escape=" + mode, "emit-alloc-asm",
                       "x86_64", str(source)]
            emitted = run_compiler(compiler, command)
            require(emitted.returncode == 0 and emitted.stdout.startswith(".text\n"),
                    f"assembler emission failed: {emitted!r}")
            # This checks only obvious assembly calls; linked instrumentation
            # below executes the actual native malloc/free path.
            if mode == "off":
                require("call malloc@PLT" in emitted.stdout and
                        "call free@PLT" in emitted.stdout, "heap baseline vanished")
            else:
                require("call malloc" not in emitted.stdout and
                        "call free" not in emitted.stdout, "unsafe hidden allocation")
            asm = folder / (mode + ".s")
            asm.write_text(emitted.stdout, encoding="utf-8")
            native = folder / ("native-" + mode)
            built = call(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                          "-Wl,--wrap=malloc", "-Wl,--wrap=free",
                          str(folder / "driver.c"), str(asm), "-o", str(native)])
            require(built.returncode == 0, f"link failed: {built.stderr}")
            executed = call([str(native), mode])
            require(executed.returncode == 0 and
                    f"malloc={allocations} free={allocations}" in executed.stdout,
                    f"native allocation count/value mismatch: {executed!r}")
            overflow = call([str(native), "overflow"])
            require(overflow.returncode == -signal.SIGILL,
                    f"checked overflow did not trap: {overflow!r}")
        invalid = SOURCE + "\nactor.send(p);\n"
        source.write_text(invalid, encoding="utf-8")
        denied = run_compiler(compiler, ["--opt-escape=safe", "emit-alloc-asm",
                                         "x86_64", str(source)])
        require(denied.returncode == 1 and not denied.stdout and
                "unexpected trailing allocation source" in denied.stderr,
                "unsupported actor effect was silently optimized")
        bad_arch = run_compiler(compiler, ["--opt-escape=safe", "emit-alloc-asm",
                                           "arm64", str(source)])
        require(bad_arch.returncode == 1 and not bad_arch.stdout,
                "unsupported architecture unexpectedly emitted code")
        denied_general = run_compiler(compiler, ["--opt-escape=safe", "ir-alloc",
                                                  str(source)])
        require(denied_general.returncode == 1 and not denied_general.stdout,
                "safe flag incorrectly enabled outside native allocation fixture")
        print("6 actual x86-64 cases: off 6 malloc/free, safe 0; overflow traps verified")


if __name__ == "__main__":
    main()
