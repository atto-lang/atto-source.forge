#!/usr/bin/env python3
"""Execute Forge-generated checked native instructions on caller-supplied i64s.

This is NOT a native function-call implementation or self-hosting: source
helper calls are still statically inlined, but IR parameters are live inputs.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile

PROGRAMS = (
    ("two-args",
     "fnc helper(x:int,y:int):int{return x*3-y;} "
     "fnc main(a:int,b:int):int{return helper(a,b)+b*2;}",
     ((7,13,34),(-7,13,-8),(0,0,0),(10,-5,25),
      (3074457345618258602,1,9223372036854775807))),
    ("four-args",
     "fnc main(a:int,b:int,c:int,d:int):int{return a-b+c*d;}",
     ((1,2,3,4,11),(-4,-6,-2,5,-8),(0,0,0,0,0))),
    ("zero-args",
     "fnc main():int{return 23+19;}",
     ((42,),)),
)
NEGATIVE_RUNTIME = (
    ("mul-overflow",
     "fnc main(x:int):int{return x*3;}",
     (9223372036854775807,),"i64 mul overflow"),
    ("add-overflow",
     "fnc main(x:int):int{return x+1;}",
     (9223372036854775807,),"i64 add overflow"),
    ("sub-overflow",
     "fnc main(x:int):int{return x-1;}",
     (-9223372036854775808,),"i64 sub overflow"),
)
DRIVER = r"""#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern int64_t oresforge_entry(PARAMS);
int main(int argc,char **argv) {
 if(argc != COUNT + 1)return 2;
 int64_t values[COUNT ? COUNT : 1];
 for(int i=0;i<COUNT;++i) {
   char *end=0;
   errno=0;
   const long long val=strtoll(argv[i+1],&end,10);
   if(errno || !*argv[i+1] || *end)return 3;
   values[i]=(int64_t)val;
 }
 const int64_t result=oresforge_entry(ARGUMENTS);
 if(printf("%" PRId64 "\n",result)<0)return 4;
 return fflush(stdout)==0 ? 0 : 5;
}
"""

def execute(command,root,timeout=10):
    return subprocess.run(command,cwd=root,stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                          timeout=timeout,check=False)

def check(condition,message):
    if not condition: raise RuntimeError(message)

def assemble(compiler,cc,arch,root,source_text,parameter_count):
    source=root/"source.ores"
    source.write_text(source_text+"\n",encoding="utf-8")
    proc=execute([compiler,"emit-runtime-asm",arch,str(source)],root)
    check(proc.returncode==0 and not proc.stderr,
          "runtime compiler failed: "+proc.stderr.decode(errors="replace"))
    asm=proc.stdout.decode("ascii")
    check(asm.startswith(".text\n.globl oresforge_entry\n") and
          ".Loresforge_overflow:" in asm and
          len(asm)<1500000,"runtime native emission missing checked control")
    (root/"entry.s").write_text(asm,encoding="ascii")
    signature=", ".join(["int64_t"]*parameter_count) or "void"
    arguments=", ".join("values[%d]"%n for n in range(parameter_count))
    driver=DRIVER.replace("PARAMS",signature).replace("COUNT",str(parameter_count))
    driver=driver.replace("ARGUMENTS",arguments)
    (root/"driver.c").write_text(driver,encoding="utf-8")
    exe=root/"run-entry"
    linked=execute([cc,"-std=c11","-O0","-Wall","-Wextra",
                    str(root/"driver.c"),str(root/"entry.s"),
                    "-o",str(exe)],root)
    check(linked.returncode==0,"assembler/link failed: "+
          linked.stderr.decode(errors="replace"))
    return source,exe,asm

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--compiler",required=True)
    args=p.parse_args()
    machine=platform.machine().lower()
    check(sys.platform.startswith("linux"),"Linux only")
    arch={"x86_64":"x86_64","amd64":"x86_64","aarch64":"arm64","arm64":"arm64"}.get(machine)
    check(arch is not None,"unsupported host architecture")
    compiler=str(Path(args.compiler).resolve(strict=True))
    cc=shutil.which("cc")
    check(cc is not None,"missing native cc")
    executed=0
    rejected=0
    with tempfile.TemporaryDirectory(prefix="oresforge-runtime-native-") as t:
        root=Path(t)
        for label,text,cases in PROGRAMS:
            parameter_count=len(cases[0])-1
            source,exe,asm=assemble(compiler,cc,arch,root,text,parameter_count)
            # Demonstrate that assembly loads real ABI parameters, not a
            # precomputed constant-return expression.
            if parameter_count:
                check(("(%rdi" in asm or "%rdi," in asm) if arch=="x86_64"
                      else "str x0," in asm,"no runtime ABI parameter read: "+label)
            for row in cases:
                inputs=row[:-1]
                expected=row[-1]
                from_cli=execute([compiler,"run-runtime",str(source),
                                  *map(str,inputs)],root)
                check((from_cli.returncode,from_cli.stdout,from_cli.stderr)==
                      (0,(str(expected)+"\n").encode(),b""),
                      "checked interpreter divergence: "+label)
                from_native=execute([str(exe),*map(str,inputs)],root)
                check((from_native.returncode,from_native.stdout,from_native.stderr)==
                      (0,(str(expected)+"\n").encode(),b""),
                      "native ABI divergence: "+label)
                executed+=1
        for label,text,inputs,expected_error in NEGATIVE_RUNTIME:
            source,exe,asm=assemble(compiler,cc,arch,root,text,len(inputs))
            result=execute([compiler,"run-runtime",str(source),*map(str,inputs)],root)
            check(result.returncode==1 and not result.stdout and
                  expected_error.encode() in result.stderr,
                  "checked interpreter overflow admitted: "+label)
            native=execute([str(exe),*map(str,inputs)],root)
            check(native.returncode in (-signal.SIGILL,-signal.SIGTRAP) and
                  not native.stdout,
                  "native overflow did not trap: "+label+" rc="+str(native.returncode))
            rejected+=1
    print(json.dumps({"kind":"forge-runtime-input-native-proof",
                      "arch":arch,"dynamic_executed":executed,
                      "overflow_trapped":rejected,"native_function_calls":False,
                      "self_hosted":False},sort_keys=True))

if __name__=="__main__":
    try: main()
    except (RuntimeError,OSError,UnicodeError,subprocess.TimeoutExpired) as e:
        print("Runtime native execution FAILED: "+str(e),file=sys.stderr)
        sys.exit(1)
