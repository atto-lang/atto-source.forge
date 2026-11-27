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
import hashlib

POSITIVE = (
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
    ("guard-brace-continue", "// Same semantic behavior as keyword-continue.atto.\npub fnc main(): int {\n  guard true { return 0; }\n  return 42;\n}", ((42,),)),
    ("guard-brace-failure", "// False guard executes terminal failure block.\npub fnc main(): int {\n  guard false { return 0; }\n  return 42;\n}", ((0,),)),
    ("guard-keyword-continue", "// Same semantic behavior as brace-continue.atto.\npub fnc main(): int {\n  guard true else\n    return 0;\n  end\n  return 42;\n}", ((42,),)),
    ("guard-keyword-failure", "// False guard executes terminal failure block.\npub fnc main(): int {\n  guard false else\n    return 0;\n  end\n  return 42;\n}", ((0,),)),
    ("guard-canonical-scalar", (Path(__file__).resolve().parent.parent /
      "contracts/guard-v1/canonical-scalar.atto").read_text(encoding="utf8").removesuffix("\n"), ((42,),)),
    ("guard-nested-keyword", "fnc main(x:int):int{guard x>0 else guard x eq 0 {^ 9;} ^ 7; end ^ 42;}",
     ((-1,9),(0,7),(1,42))),
    ("guard-nested-brace", "fnc main(x:int):int{guard x>0 {guard x eq 0 else ^ 9; end return 7;} return 42;}",
     ((-1,9),(0,7),(1,42))),
    ("guard-nested-lazy", "fnc main(x:int):int{guard x eq 1 {guard 9223372036854775807+x eq 0 {return 9;} return 7;} return 42;}",
     ((1,42),(0,9))),
    ("guard-runtime", "fnc main(int a):int{guard a>0 {^ 1;} guard a<10, a neq 5 else return 2; end return 42;}",
     ((0,1),(3,42),(5,2),(11,2),(-1,1))),
    ("guard-lazy-checks", "fnc main(a:int):int{guard a eq 0, 9223372036854775807+a eq 0 {return 7;} return 42;}",
     ((0,7),(1,7),(-1,7))),
    ("guard-lazy-failure", "fnc main(a:int):int{guard a eq 1 {return 9223372036854775807+a;} return 42;}",
     ((1,42),(0,9223372036854775807))),
    ("guard-lazy-continuation", "fnc main(a:int):int{guard a eq 0 else return 42; end return 9223372036854775807+a;}",
     ((1,42),(0,9223372036854775807))),
    ("bool-xor-truth-table", "fnc main(a:int,b:int):int{return Xor(a eq 0,b eq 0) ? 42 : 7;}",
     ((0,0,7),(0,1,42),(1,0,42),(1,1,7))),
    ("bool-xor-composition", "fnc main(a:int):int{return Xor(a eq 0,true) && a neq 2 ? 42 : 7;}",
     ((0,7),(1,42),(2,7))),
    ("user-xor-call", "fnc Xor(a:int,b:int):int{return a+b;} fnc main():int{return Xor(19,23);}",
     ((42,),)),
    ("integer-xor-runtime", "fnc main(a:int,b:int):int{return a ^^ b;}",
     ((11,4,15),(-1,0,-1),(-1,-1,0),(-9223372036854775808,-1,9223372036854775807),(9223372036854775807,-9223372036854775808,-1))),
    ("integer-xor-precedence", "fnc main(a:int,b:int):int{return a+1 ^^ b*2;}",
     ((2,5,9),(0,1,3))),
    ("integer-xor-chain", "fnc main(a:int,b:int):int{return a ^^ b ^^ 7;}",
     ((11,4,8),(7,7,7))),
    ("integer-xor-caret", "fnc main(a:int,b:int):int{^ (a ^^ b) eq 15 ? 42 : 7;}",
     ((11,4,42),(1,2,7))),
    ("no-args", "fnc main():int{return 42;}", ((42,),)),
)
# Exact shared Git blobs, including the final newline added by build().
GUARD_FIXTURE_BLOBS = {
    "guard-brace-continue": "fa9dd0394a3ef08f0ecb4ff010e94cc72a859126",
    "guard-brace-failure": "f4cdcada6b147c77a1f0f6ae6469a31ee44f2603",
    "guard-keyword-continue": "82a2afa93e827f387407d0530febf328254599dc",
    "guard-keyword-failure": "13f41e961534f8b93895c224b04df77feb286793",
    "guard-canonical-scalar": "e2344e360ac643e25df7ca14eb01432929cecbe5",
}
TRAPS = (
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
    ("nested-guard-fallthrough", "fnc main():int{guard true {guard false {return 7;}} return 42;}", "unexpected token"),
    ("nested-guard-bool-return", "fnc main():int{guard true {guard false {return true;} return 7;} return 42;}", "Boolean is not an int"),
    ("nested-guard-failure-no-return", "fnc main():int{guard true else guard false {} return 7; end return 42;}", "guard_failure_requires_return"),
    ("logical-int-left", "fnc main():int{return 1 && true ? 42 : 7;}", "condition_not_bool"),
    ("logical-int-right", "fnc main():int{return false && 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-or-int-right", "fnc main():int{return true || 1 ? 42 : 7;}", "condition_not_bool"),
    ("logical-bool-return", "fnc main():int{return true && false;}", "Boolean is not an int"),
    ("guard-int-condition", "fnc main():int{guard 1 {return 0;} return 42;}", "condition_not_bool"),
    ("guard-int-second-check", "fnc main():int{guard false, 1 else return 0; end return 42;}", "condition_not_bool"),
    ("guard-fallthrough", "fnc main():int{guard false {} return 42;}", "guard_failure_requires_return"),
    ("guard-break", "fnc main():int{guard false else break; end return 42;}", "guard_failure_requires_return"),
    ("guard-throw", "fnc main():int{guard false {throw 1;} return 42;}", "guard_failure_requires_return"),
    ("guard-void-return", "fnc main():int{guard false {return;} return 42;}", "expected callable expression"),
    ("guard-bool-return", "fnc main():int{guard false {return true;} return 42;}", "Boolean is not an int"),
    ("guard-missing-tail", "fnc main():int{guard true {return 1;}}", "unexpected token"),
    ("guard-wrong-closer", "fnc main():int{guard true else return 1; fi return 42;}", "unexpected token"),
    ("guard-reserved-formal", "fnc helper(guard:int):int{return 42;} fnc main():int{return 42;}", "reserved predicate word"),
    ("bool-xor-mixed", "fnc main():int{return Xor(true,1) ? 42 : 7;}", "Boolean Xor requires two bool operands"),
    ("bool-xor-arity", "fnc main():int{return Xor(true) ? 42 : 7;}", "Boolean Xor requires two bool operands"),
    ("bool-xor-shadow-function", "fnc main():int{return Xor(true,false) ? 42 : 7;} fnc Xor(a:int,b:int):int{return a+b;}", "Boolean Xor intrinsic is shadowed"),
    ("bool-xor-shadow-formal", "fnc main(Xor:int):int{return Xor(true,false) ? 42 : 7;}", "Boolean Xor intrinsic is shadowed"),
    ("bool-xor-symbolic", "fnc main():int{return true ^^ false ? 42 : 7;}", "Boolean is not an int"),
    ("integer-xor-mixed", "fnc main():int{return 1 ^^ false;}", "Boolean is not an int"),
    ("integer-xor-equality-precedence", "fnc main():int{return 11 ^^ 4 eq 15 ? 42 : 7;}", "Boolean is not an int"),
    ("integer-xor-spaced", "fnc main():int{return 11 ^ ^ 4;}", "single caret is only a return statement"),
    ("integer-xor-infix-caret", "fnc main():int{return 11 ^ 4;}", "single caret is only a return statement"),
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
        for label,source,cases in POSITIVE:
            number=len(cases[0])-1
            file,exe,asm=build(compiler,cc,root,label,source,number)
            if label in GUARD_FIXTURE_BLOBS:
                data=file.read_bytes()
                blob=hashlib.sha1(b"blob "+str(len(data)).encode("ascii")+b"\0"+data).hexdigest()
                require(blob==GUARD_FIXTURE_BLOBS[label],"shared guard fixture drift "+label)
            if label in ("factorial","fibonacci","mutual","nested"):
                require(asm.count("  call .Lores_fn_")>=1,"missing real native call "+label)
            if label.startswith("integer-xor-"):
                require("  xorq " in asm,"missing native integer xor instruction "+label)
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
            file.write_text(source+"\n",encoding="utf8")
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
