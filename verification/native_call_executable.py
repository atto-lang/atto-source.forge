#!/usr/bin/env python3
"""Real native x86-64 call/return, lazy branching and recursion proof.

The stage-0 compiler itself is C++ and this is not self-hosting.
"""
import argparse
import itertools
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile

# Exercise the full admitted local budget with real ABI-dependent values
# and previous-local references. This catches a native frame-slot collision
# or incorrectly scoped initializer that isolated single-local tests miss.
BOUNDARY_32 = ("fnc main(n:int):int{" +
               "int v0=n;" +
               "".join("const v%d=v%d+1;" % (i, i-1) for i in range(1,32)) +
               "return v31;}")
BOUNDARY_33 = ("fnc main(n:int):int{" +
               "".join("int v%d=n;" % i for i in range(33)) +
               "return v32;}")

POSITIVE = (
    ("named-nested-comment-label",
     "fnc pair(int a,int b):int{return a*10+b;}"
     " fnc main(int n):int{return pair(b/* % ^ guard /* nested */ */:2,a:n);}",
     ((4,42),(5,52))),
    ("comment-hidden-declaration",
     "/* macro bogus /* guard and % and ^ */ */"
     "fnc main():int{return 42;}", ((42,),)),
    ("named-reordered-runtime",
     "fnc pair(int a,int b):int{return a*10+b;}"
     " fnc main(n:int):int{return pair(b:2,a:n);}",
     ((4,42),(7,72),(-1,-8))),
    ("named-positional-prefix",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main(n:int):int{return pair(n,b:2);}",
     ((4,42),(7,72))),
    ("named-nested-call",
     "fnc inc(x:int):int{return x+1;}"
     " fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main(n:int):int{return pair(b:inc(x:1),a:inc(x:n));}",
     ((3,42),(0,12))),
    ("named-case-exact",
     "fnc pair(int A,int b):int{return A*10+b;}"
     " fnc main(int n):int{return pair(b:2,A:n);}",
     ((4,42),(6,62))),
    ("named-recursion",
     "fnc fact(n:int):int{return if n<=1 then 1 else n*fact(n:n-1);}"
     " fnc main(n:int):int{return fact(n:n);}",
     ((0,1),(5,120),(8,40320))),
    ("named-four-registers",
     "fnc four(a:int,b:int,c:int,d:int):int{return a*1000+b*100+c*10+d;}"
     " fnc main(n:int):int{return four(d:4,c:3,b:2,a:n);}",
     ((1,1234),(5,5234))),
    ("named-immutable-local",
     "fnc id(int x):int{return x;}"
     " fnc pair(int a,int b):int{return a*10+b;}"
     " fnc main(n:int):int{const value=id(x:n);return pair(b:2,a:value);}",
     ((4,42),(-1,-8))),
    ("immutable-32-local-boundary", BOUNDARY_32,
     ((0,31),(-31,0),(9223372036854775776,9223372036854775807))),
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
    ("no-args", "fnc main():int{return 42;}", ((42,),)),
    ("immutable-helper-bindings",
     "fnc twice(n:int):int{const value=n+1;return value*2;}"
     "fnc main(n:int):int{int result=twice(n);const answer=result+1;return answer;}",
     ((0,3),(7,17),(20,43),(-2,-1))),
    ("immutable-lazy-scope",
     "fnc main(n:int):int{const value=n+1;return if value>0 then value else -value;}",
     ((-3,2),(-1,0),(5,6))),
    ("immutable-explicit-type",
     "fnc main(n:int):int{const int v=n+2;return v*3;}",
     ((-2,0),(0,6),(5,21))),
)
TRAPS = (
    # Both operands can fail: the first SOURCE expression must win,
    # regardless of which formal it binds to.
    ("named-lexical-overflow-before-recursion",
     "fnc loop(n:int):int{return loop(n);}"
     "fnc ignore(a:int,b:int):int{return 42;}"
     "fnc main(n:int):int{return ignore(b:n+1,a:loop(n));}",
     (9223372036854775807,), "i64 add overflow"),
    ("named-lexical-recursion-before-overflow",
     "fnc loop(n:int):int{return loop(n);}"
     "fnc ignore(a:int,b:int):int{return 42;}"
     "fnc main(n:int):int{return ignore(b:loop(n),a:n+1);}",
     (9223372036854775807,), "call depth exceeded"),
    ("named-eager-unused-argument-overflow",
     "fnc ignore(a:int,b:int):int{return 42;}"
     " fnc main(n:int):int{return ignore(b:0,a:n+1);}",
     (9223372036854775807,), "i64 add overflow"),
    ("overflow-add", "fnc plus(n:int):int{return n+1;}"
     " fnc main(n:int):int{return plus(n);}",
     (9223372036854775807,), "i64 add overflow"),
    ("overflow-mul", "fnc mul(n:int):int{return n*3;}"
     " fnc main(n:int):int{return mul(n);}",
     (9223372036854775807,), "i64 mul overflow"),
    ("overflow-sub", "fnc main(n:int):int{return n-1;}",
     (-9223372036854775808,), "i64 sub overflow"),
    ("immutable-unused-eager-overflow",
     "fnc main(n:int):int{const unused=n+1;return 42;}",
     (9223372036854775807,), "i64 add overflow"),
    ("immutable-chain-overflow", BOUNDARY_32,
     (9223372036854775807,), "i64 add overflow"),
    ("unbounded-recursion", "fnc always(n:int):int{return always(n);}"
     " fnc main(n:int):int{return always(n);}",
     (1,), "call depth exceeded"),
)
INVALID = (
    ("unterminated-block-comment",
     "fnc main():int{return 42;} /* never closed",
     "unterminated block comment"),
    ("legacy-percent-op",
     "fnc main():int{return 10%3;}",
     "legacy % operator is removed"),
    ("legacy-single-caret-op",
     "fnc main():int{return 2^3;}",
     "legacy ^/^^ operators are removed"),
    ("legacy-double-caret-op",
     "fnc main():int{return 2^^3;}",
     "legacy ^/^^ operators are removed"),
    ("guard-native-local",
     "fnc main():int{const guard=1;return guard;}",
     "GUARD_BACKEND_UNSUPPORTED"),
    ("guard-native-function",
     "fnc guard():int{return 42;} fnc main():int{return guard();}",
     "GUARD_BACKEND_UNSUPPORTED"),
    ("guard-native-parameter",
     "fnc helper(guard:int):int{return 42;} fnc main():int{return helper(1);}",
     "GUARD_BACKEND_UNSUPPORTED"),
    ("named-duplicate",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(a:4,a:2);}", "duplicate named argument"),
    ("named-unknown",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(z:4,b:2);}", "unknown named argument: z"),
    ("named-positional-after",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(b:2,4);}", "positional argument after named argument"),
    ("named-positional-collision",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(4,a:2);}", "duplicate named argument"),
    ("named-case-sensitive",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(A:4,b:2);}", "unknown named argument: A"),
    ("named-missing",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(b:2);}", "function arity mismatch"),
    ("named-extra",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc main():int{return pair(a:4,b:2,z:0);}", "function arity mismatch"),
    ("named-unreachable-invalid",
     "fnc pair(a:int,b:int):int{return a*10+b;}"
     " fnc unused():int{return pair(a:4,z:2);}"
     " fnc main():int{return 42;}", "unknown named argument: z"),
    ("wrong-arity", "fnc f(n:int):int{return n;} fnc main():int{return f(1,2);}", "function arity mismatch"),
    ("unknown-target", "fnc main():int{return absent(1);}", "unknown function"),
    ("unreachable-invalid", "fnc bad():int{return absent();} fnc main():int{return 1;}", "unknown function"),
    ("duplicate-name", "fnc f():int{return 1;} fnc f():int{return 2;} fnc main():int{return 1;}", "duplicate function"),
    ("unknown-param", "fnc main():int{return oops;}", "unknown parameter"),
    ("unsupported-loop", "fnc main():int{return while 1 then 2;}", "unknown parameter"),
    ("immutable-self-reference",
     "fnc main():int{int x=x;return x;}", "unknown parameter"),
    ("immutable-duplicate",
     "fnc main():int{int x=1;const x=2;return x;}",
     "invalid or duplicate immutable local"),
    ("immutable-reassignment",
     "fnc main():int{int x=1;x=2;return x;}",
     "immutable local reassignment is forbidden"),
    ("immutable-forward-reference",
     "fnc main():int{int x=y;int y=1;return x;}",
     "unknown parameter"),
    ("immutable-param-shadow",
     "fnc main(n:int):int{int n=1;return n;}",
     "invalid or duplicate immutable local"),
    ("immutable-33-local-budget", BOUNDARY_33,
     "immutable local budget exceeded"),
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
    file.write_text(source+"\n",encoding="utf8")
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
        # Exhaust all 24 permutations of four signature labels. The native
        # ABI must bind to formal register slots, not the spelling order.
        permutation_cases=[]
        for index,order in enumerate(itertools.permutations(("a","b","c","d"))):
            values={"a":"n","b":"2","c":"3","d":"4"}
            args=",".join(name+":"+values[name] for name in order)
            source=("fnc four(a:int,b:int,c:int,d:int):int{"
                    "return a*1000+b*100+c*10+d;}"
                    "fnc main(n:int):int{return four("+args+");}")
            permutation_cases.append(("named-four-permutation-%02d"%index,source,
                                      ((1,1234),(5,5234))))
        for label,source,cases in POSITIVE+tuple(permutation_cases):
            number=len(cases[0])-1
            file,exe,asm=build(compiler,cc,root,label,source,number)
            if label in ("factorial","fibonacci","mutual","nested","immutable-helper-bindings",
                         "named-reordered-runtime","named-nested-call","named-four-registers"):
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
        # A depth of 128 is admissible; 129 must reject without publishing
        # any output, including when the comments trail valid declarations.
        boundary_comment="/*"*128+"*/"*128
        positive_boundary="fnc main():int{return 42;}"+boundary_comment
        file,exe,_=build(compiler,cc,root,"comment-depth-128",positive_boundary,0)
        for command in ((compiler,"run-calls",str(file)),):
            ok=execute(command,root)
            require((ok.returncode,ok.stdout,ok.stderr)==(0,b"42\n",b""),
                    "comment nesting depth 128 must remain admissible")
        passed=execute([str(exe)],root)
        require((passed.returncode,passed.stdout,passed.stderr)==(0,b"42\n",b""),
                "native comment nesting boundary mismatch")
        positives+=1
        too_deep="fnc main():int{return 42;}"+"/*"*129+"*/"*129
        # Include this generated 129-depth negative alongside static cases.
        for label,source,diagnostic in INVALID+(
            ("over-nested-block-comment",too_deep,"block comment nesting too deep"),):
            file=root/"bad.atto"
            file.write_text(source+"\n",encoding="utf8")
            for command in ((compiler,"run-calls",str(file)),(compiler,"emit-call-asm","x86_64",str(file))):
                failure=execute(command,root)
                require(failure.returncode==1 and failure.stdout==b"" and
                        diagnostic.encode() in failure.stderr,
                        "semantic invalid case admitted or leaked output "+label)
            negatives+=1
        file=root/"unsupported.atto"
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
