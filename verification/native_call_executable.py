#!/usr/bin/env python3
"""Real native x86-64 call/return, lazy branching and recursion proof.

The stage-0 compiler itself is C++ and this is not self-hosting.
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

CANONICAL_GUARD = (Path(__file__).resolve().parent.parent /
                   "contracts/guard-v1/canonical-scalar.atto").read_text(encoding="utf8")

POSITIVE = (
    ("guard-shared", CANONICAL_GUARD, ((42,),)),
    ("guard-braced", "fnc main(int x):int{guard x>0 {return 1;} guard x<10, x neq 5 {^ 2;} return 42;}",
     ((-1,1),(0,1),(1,42),(5,2),(10,2))),
    ("guard-keyword", "fnc main(x:int):int{guard x>0 else ^ 1; end guard x<10, x neq 5 else return 2; end ^ 42;}",
     ((-1,1),(0,1),(1,42),(5,2),(10,2))),
    ("guard-lazy-predicate", "fnc main(x:int):int{guard x eq 0, 9223372036854775807+x eq 0 {return 7;} return 42;}",
     ((0,7),(1,7),(-1,7))),
    ("guard-lazy-continuation", "fnc main(x:int):int{guard x eq 0 {return 42;} return 9223372036854775807+x;}",
     ((1,42),(0,9223372036854775807))),
    ("guard-lazy-failure", "fnc main(x:int):int{guard x eq 1 {return 9223372036854775807+x;} return 42;}",
     ((1,42),(0,9223372036854775807))),
    ("guard-nested", "fnc main(x:int):int{guard x>0 else guard x eq 0 {^ 9;} ^ 7; end ^ 42;}",
     ((-1,9),(0,7),(1,42))),
    ("guard-call", "fnc predicate_input(int x):int{return x+1;} fnc main(int x):int{guard predicate_input(x) eq 1 {^ 7;} ^ 42;}",
     ((0,42),(1,7))),
    ("factorial", "fnc fact(n:int):int{return if n<=1 then 1 else n*fact(n-1);}"
     " fnc main(n:int):int{return fact(n);}",
     ((0,1),(1,1),(5,120),(10,3628800),(20,2432902008176640000))),
    ("fibonacci", "fnc fib(n:int)->int{return if n<=1 then n else fib(n-1)+fib(n-2);}"
     " fnc main(n:int):int{return fib(n);}",
     ((0,0),(1,1),(6,8),(12,144))),
    ("mutual", "fnc even(n:int):int{return if n==0 then 1 else odd(n-1);}"
     " fnc odd(n:int):int{return if n==0 then 0 else even(n-1);}"
     " fnc main(n:int):int{return even(n);}",
     ((0,1),(1,0),(6,1),(11,0))),
    ("nested", "fnc scale(x:int,y:int):int{return x*y;}"
     " fnc add(x:int,y:int):int{return x+y;}"
     " fnc main(a:int,b:int):int{return add(scale(a,2),scale(b,3));}",
     ((7,5,29),(-9,8,6),(0,0,0))),
    ("lazy", "fnc main(x:int):int{return if x>0 then 100+x else if x<0 then -100+x else 0;}",
     ((-5,-105),(0,0),(5,105))),
    ("signed-comparisons", "fnc main(a:int,b:int):int{return if a<b then 1 else if a>=b then 2 else 3;}",
     ((-9223372036854775808,9223372036854775807,1),(2,2,2),(10,2,2))),
    ("typed-equality", "fnc main(a:int,b:int):int{^ a eq b ? 42 : 7;}",
     ((0,0,42),(1,2,7),(-9223372036854775808,-9223372036854775808,42))),
    ("typed-neq", "fnc main(a:int,b:int):int{return if a neq b then 42 else 7;}",
     ((1,2,42),(2,2,7))),
    ("typed-bang-eq", "fnc main(a:int,b:int):int{return a !eq b ? 42 : 7;}",
     ((1,2,42),(2,2,7))),
    ("ifnot", "fnc main(a:int):int{return ifnot a eq 0 then 42 else 7 fi;}",
     ((-1,42),(0,7),(1,42))),
    ("unless", "fnc main(a:int):int{return unless a eq 0 then 42 else 7 fi;}",
     ((-1,42),(0,7),(1,42))),
    ("lazy-ternary", "fnc main(a:int):int{return a eq 0 ? 42 : 9223372036854775807+a;}",
     ((0,42),(-1,9223372036854775806))),
    ("lazy-negated", "fnc main(a:int):int{return unless a eq 1 then 42 else 9223372036854775807+a fi;}",
     ((2,42),(0,42))),
    ("bool-equality", "fnc main(a:int):int{return (a eq 0) eq true ? 42 : 7;}",
     ((0,42),(1,7))),
    ("nested-ternary", "fnc main(a:int):int{return a eq 0 ? 42 : a eq 1 ? 7 : 9;}",
     ((0,42),(1,7),(2,9))),
    ("logical-and-lazy", "fnc main(a:int):int{return a eq 0 && 9223372036854775807+a eq 0 ? 7 : 42;}",
     ((0,42),(1,42),(-1,42))),
    ("logical-or-lazy", "fnc main(a:int):int{return a eq 1 || 9223372036854775807+a eq 0 ? 42 : 7;}",
     ((1,42),(0,7),(-1,7))),
    ("logical-precedence", "fnc main(a:int):int{return a eq 0 || a eq 1 && false ? 42 : 7;}",
     ((0,42),(1,7),(2,7))),
    ("logical-nested", "fnc main(a:int):int{return ifnot (a eq 0 || a eq 1) && true then 42 else 7 fi;}",
     ((0,7),(1,7),(2,42))),
    ("no-args", "fnc main():int{return 42;}", ((42,),)),
)
TRAPS = (
    ("guard-evaluates-rhs", "fnc main(x:int):int{guard x eq 1, 9223372036854775807+x eq 0 {return 7;} return 42;}",
     (1,), "i64 add overflow"),
    ("guard-executes-failure", "fnc main(x:int):int{guard x eq 0 {return 9223372036854775807+x;} return 42;}",
     (1,), "i64 add overflow"),
    ("overflow-add", "fnc plus(n:int):int{return n+1;}"
     " fnc main(n:int):int{return plus(n);}",
     (9223372036854775807,), "i64 add overflow"),
    ("overflow-mul", "fnc mul(n:int):int{return n*3;}"
     " fnc main(n:int):int{return mul(n);}",
     (9223372036854775807,), "i64 mul overflow"),
    ("overflow-sub", "fnc main(n:int):int{return n-1;}",
     (-9223372036854775808,), "i64 sub overflow"),
    ("logical-and-evaluates-rhs", "fnc main(a:int):int{return a eq 1 && 9223372036854775807+a eq 0 ? 7 : 42;}",
     (1,), "i64 add overflow"),
    ("logical-or-evaluates-rhs", "fnc main(a:int):int{return a eq 0 || 9223372036854775807+a eq 0 ? 7 : 42;}",
     (1,), "i64 add overflow"),
    ("unbounded-recursion", "fnc always(n:int):int{return always(n);}"
     " fnc main(n:int):int{return always(n);}",
     (1,), "call depth exceeded"),
)
INVALID = (
    ("guard-int-condition", "fnc main():int{guard 1 {return 7;} return 42;}", "condition_not_bool"),
    ("guard-int-comma", "fnc main():int{guard false, 1 {return 7;} return 42;}", "condition_not_bool"),
    ("guard-empty-failure", "fnc main():int{guard true {} return 42;}", "E_GUARD_RETURN"),
    ("guard-fallthrough", "fnc main():int{guard true else 7; end return 42;}", "E_GUARD_RETURN"),
    ("guard-void-return", "fnc main():int{guard true {return;} return 42;}", "expected callable expression"),
    ("guard-bool-return", "fnc main():int{guard true {return false;} return 42;}", "Boolean is not an int"),
    ("guard-no-final-return", "fnc main():int{guard true {return 7;}}", "E_GUARD_RETURN"),
    ("guard-wrong-closer", "fnc main():int{guard true else return 7; fi return 42;}", "unexpected token"),
    ("guard-no-delimiter", "fnc main():int{guard true return 7; return 42;}", "E_GUARD_DELIMITER"),
    ("guard-unknown-unused", "fnc hidden():int{guard true {return missing();} return 1;} fnc main():int{return 42;}", "unknown function"),
    ("logical-int-left", "fnc main():int{return 1 && true ? 42 : 7;}", "condition_not_bool"),
    ("logical-int-right", "fnc main():int{return false && 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-or-int-right", "fnc main():int{return true || 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-bool-return", "fnc main():int{return true && false;}", "Boolean is not an int"),
    ("integer-condition", "fnc main():int{return if 1 then 2 else 3;}", "condition_not_bool"),
    ("integer-ternary", "fnc main():int{return 1 ? 2 : 3;}", "condition_not_bool"),
    ("integer-unless", "fnc main():int{return unless 1 then 2 else 3 fi;}", "condition_not_bool"),
    ("bool-return", "fnc main():int{return true;}", "Boolean is not an int"),
    ("bool-arithmetic", "fnc main():int{return true+1;}", "Boolean is not an int"),
    ("bool-ordered", "fnc main():int{return true<false ? 2 : 3;}", "Boolean is not an int"),
    ("mixed-equality", "fnc main():int{return true eq 1 ? 2 : 3;}", "comparison operand type mismatch"),
    ("bool-arg", "fnc f(n:int):int{return n;} fnc main():int{return f(true);}", "Boolean is not an int"),
    ("bool-arm", "fnc main():int{return true ? false : 3;}", "Boolean is not an int"),
    ("negated-missing-fi", "fnc main():int{return unless true then 2 else 3;}", "unexpected token"),
    ("neq-boundary", "fnc main():int{return 1 !equation 2 ? 3 : 4;}", "invalid character"),
    ("unreachable-type-error", "fnc hidden():int{return true;} fnc main():int{return 42;}", "Boolean is not an int"),
    ("wrong-arity", "fnc f(n:int):int{return n;} fnc main():int{return f(1,2);}", "function arity mismatch"),
    ("unknown-target", "fnc main():int{return absent(1);}", "unknown function"),
    ("unreachable-invalid", "fnc bad():int{return absent();} fnc main():int{return 1;}", "unknown function"),
    ("duplicate-name", "fnc f():int{return 1;} fnc f():int{return 2;} fnc main():int{return 1;}", "duplicate function"),
    ("unknown-param", "fnc main():int{return oops;}", "unknown parameter"),
    ("unsupported-loop", "fnc main():int{return while 1 then 2;}", "unknown parameter"),
)
DRIVER = r"""#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern int64_t oresforge_entry(PARAMS);
int main(int argc,char **argv) {
 if (argc!=COUNT+1)return 2;
 int64_t v[4]={0,0,0,0};
 (void)v; /* zero-argument native entries do not consume the array */
 for(int i=0;i<COUNT;i++){
  char *end=0;errno=0;
  const long long val=strtoll(argv[i+1],&end,10);
  if(errno||!*argv[i+1]||*end)return 3;
  v[i]=(int64_t)val;
 }
 const int64_t result=oresforge_entry(ARGS);
 if(printf("%" PRId64 "\n",result)<0)return 4;
 return fflush(stdout)==0?0:5;
}
"""

def execute(args,cwd,timeout=10):
    # Do not create large core files for deliberate SIGILL depth/overflow tests.
    def limits():
        import resource
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    return subprocess.run(args,cwd=cwd,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,timeout=timeout,check=False,
                          preexec_fn=limits)

def require(value,message):
    if not value:raise RuntimeError(message)

def build(compiler,cc,root,label,source,param_count):
    file=root/"entry.atto"
    file.write_text(source if source.endswith("\n") else source+"\n",encoding="utf8")
    emit=execute([compiler,"emit-call-asm","x86_64",str(file)],root)
    require(emit.returncode==0 and not emit.stderr,
            "emit failed "+label+": "+emit.stderr.decode(errors="replace"))
    asm=emit.stdout.decode("ascii")
    require("  call .Lores_fn_" in asm or "call ." not in asm,
            "native call syntax invalid in "+label)
    require(".Lores_depth:" in asm and ".Lores_overflow:" in asm and
            len(asm)<5000000,"no bounded native call frame "+label)
    (root/"entry.s").write_text(asm,encoding="ascii")
    signature=", ".join("int64_t" for _ in range(param_count)) or "void"
    arguments=", ".join("v[%d]"%i for i in range(param_count))
    driver=DRIVER.replace("PARAMS",signature).replace("COUNT",str(param_count)).replace("ARGS",arguments)
    (root/"driver.c").write_text(driver,encoding="ascii")
    executable=root/"native"
    link=execute([cc,"-std=c11","-Wall","-Wextra","-Werror",
                  str(root/"driver.c"),str(root/"entry.s"),"-o",str(executable)],root)
    require(link.returncode==0,"link failed "+label+": "+link.stderr.decode(errors="replace"))
    return file,executable,asm

def main():
    a=argparse.ArgumentParser()
    a.add_argument("--compiler",required=True)
    opts=a.parse_args()
    require(sys.platform.startswith("linux") and platform.machine().lower() in ("x86_64","amd64"),
            "native call proof requires Linux x86-64")
    compiler=str(Path(opts.compiler).resolve(strict=True))
    cc=shutil.which("cc")
    require(bool(cc),"C linker missing")
    positives=negatives=0
    with tempfile.TemporaryDirectory(prefix="oresforge-native-calls-") as tmp:
        root=Path(tmp)
        for label,source,cases in POSITIVE:
            number=len(cases[0])-1
            file,exe,asm=build(compiler,cc,root,label,source,number)
            if label in ("factorial","fibonacci","mutual","nested"):
                require(asm.count("  call .Lores_fn_")>=1,"missing real native call "+label)
            if label in ("factorial","lazy"):
                require("  je .Lores_else_" in asm,"missing native lazy branch "+label)
            for row in cases:
                inp=row[:-1]
                expected=(str(row[-1])+"\n").encode()
                checked=execute([compiler,"run-calls",str(file),*map(str,inp)],root)
                native=execute([str(exe),*map(str,inp)],root)
                require((checked.returncode,checked.stdout,checked.stderr)==(0,expected,b""),
                        "interpreter returned wrong result "+label+"/"+repr(inp))
                require((native.returncode,native.stdout,native.stderr)==(0,expected,b""),
                        "native call returned wrong result "+label+"/"+repr(inp))
                positives+=1
        for label,source,inp,diagnostic in TRAPS:
            file,exe,_=build(compiler,cc,root,label,source,len(inp))
            checked=execute([compiler,"run-calls",str(file),*map(str,inp)],root)
            require(checked.returncode==1 and checked.stdout==b"" and
                    diagnostic.encode() in checked.stderr,"interpreter failed to reject "+label)
            native=execute([str(exe),*map(str,inp)],root)
            require(native.returncode==-signal.SIGILL and native.stdout==b"",
                    "native overflow/depth failed to trap "+label+": "+str(native.returncode))
            negatives+=1
        for label,source,diagnostic in INVALID:
            file=root/"bad.atto"
            file.write_text(source if source.endswith("\n") else source+"\n",encoding="utf8")
            for command in ((compiler,"run-calls",str(file)),(compiler,"emit-call-asm","x86_64",str(file))):
                failure=execute(command,root)
                require(failure.returncode==1 and failure.stdout==b"" and
                        diagnostic.encode() in failure.stderr,
                        "semantic invalid case admitted or leaked output "+label)
            negatives+=1
        file=root/"unsupported.ores"
        file.write_text("fnc main():int{return 42;}\n",encoding="ascii")
        unsupported=execute([compiler,"emit-call-asm","arm64",str(file)],root)
        require(unsupported.returncode==1 and unsupported.stdout==b"" and
                b"currently supports x86_64 only" in unsupported.stderr,
                "ARM64 must fail explicitly, not produce wrong assembly")
    print(json.dumps({"kind":"forge-real-native-function-call-execution",
                      "arch":"x86_64","positives":positives,"negative_controls":negatives,
                      "native_interfunction_calls":True,"runtime_lazy_branch":True,
                      "bounded_recursion":True,"arm64_execution":False,
                      "self_hosted":False},sort_keys=True))

if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired,UnicodeError) as error:
        print("Native call proof FAILED: "+str(error),file=sys.stderr)
        sys.exit(1)
