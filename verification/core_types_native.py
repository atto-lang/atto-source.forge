#!/usr/bin/env python3
"""Compile the unchanged core/types source with an explicit test entry."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tempfile
from native_call_executable import build, execute, require

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--compiler",required=True)
    opts=p.parse_args()
    compiler=str(Path(opts.compiler).resolve(strict=True))
    cc=shutil.which("cc")
    require(bool(cc),"C linker missing")
    data=(Path(__file__).resolve().parent.parent/"core/types.atto").read_bytes()
    require(hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
            =="d77382214d86fb55e0c9e5c77879d4565519467e","core/types source pin mismatch")
    source=data.decode("utf8")
    lo,hi=-(1<<63),(1<<63)-1
    constants={"int_value_bits":64,"bool_value_bits":1,"byte_value_bits":8,
               "int_min_value":lo,"int_max_value":hi}
    ranges={"fits_i8":(-128,127),"fits_i16":(-32768,32767),
            "fits_i32":(-(1<<31),(1<<31)-1),"fits_i64":(lo,hi),
            "fits_u8":(0,255),"fits_u16":(0,65535),
            "fits_u32":(0,(1<<32)-1),"fits_u64":(0,hi)}
    cases=0
    with tempfile.TemporaryDirectory(prefix="forge-core-types-") as tmp:
        root=Path(tmp)
        for name,expected in constants.items():
            file,exe,_=build(compiler,cc,root,name,source+"\npub fnc main():int{return "+name+"();}",0)
            for args in ([compiler,"run-calls",str(file)],[str(exe)]):
                r=execute(args,root)
                require((r.returncode,r.stdout,r.stderr)==(0,(str(expected)+"\n").encode(),b""),name+" constant mismatch")
            cases+=1
        for name,(lower,upper) in ranges.items():
            file,exe,_=build(compiler,cc,root,name,source+"\npub fnc main(int x):int{return "+name+"(x) ? 1 : 0;}",1)
            for x in sorted(v for v in {lo,hi,-1,0,1,lower-1,lower,lower+1,upper-1,upper,upper+1} if lo<=v<=hi):
                expected=(str(int(lower<=x<=upper))+"\n").encode()
                for args in ([compiler,"run-calls",str(file),str(x)],[str(exe),str(x)]):
                    r=execute(args,root)
                    require((r.returncode,r.stdout,r.stderr)==(0,expected,b""),name+" boundary mismatch "+str(x))
                cases+=1
        # Exercise Boolean results through multiple real native call frames, not
        # only a direct helper-to-main adapter. RHS overflow is observable if
        # a lazy Boolean helper accidentally evaluates its dead operand.
        compositions=(
          ("bool-call-chain",
           "fnc leaf(int x):bool{return x eq 0;} "
           "fnc middle(int x):bool{return leaf(x) eq false;} "
           "fnc main(int x):int{return middle(x) ? 42 : 7;}",
           ((0,7),(1,42),(-1,42))),
          ("bool-call-lazy-and",
           "fnc risky(int x):bool{return 9223372036854775807+x eq 0;} "
           "fnc both(int x):bool{return x eq 0 && risky(x);} "
           "fnc main(int x):int{return both(x) ? 42 : 7;}",
           ((0,7),(1,7),(-1,7))),
          ("bool-call-lazy-or",
           "fnc risky(int x):bool{return 9223372036854775807+x eq 0;} "
           "fnc either(int x):bool{return x eq 1 || risky(x);} "
           "fnc main(int x):int{return either(x) ? 42 : 7;}",
           ((1,42),(0,7),(-1,7))),
          ("bool-call-four-arguments",
           "fnc same(int a,int b,int c,int d):bool{return a eq b && c neq d;} "
           "fnc main(int a,int b,int c,int d):int{return same(a,b,c,d) ? 42 : 7;}",
           ((1,1,2,3,42),(1,2,3,4,7),(1,1,2,2,7))),
        )
        composition_cases=0
        for name,code,vectors in compositions:
            file,exe,asm=build(compiler,cc,root,name,code,len(vectors[0])-1)
            require("  call .Lores_fn_" in asm,name+" missing native call")
            for row in vectors:
                expected=(str(row[-1])+"\n").encode()
                for args in ([compiler,"run-calls",str(file),*map(str,row[:-1])],
                             [str(exe),*map(str,row[:-1])]):
                    r=execute(args,root)
                    require((r.returncode,r.stdout,r.stderr)==(0,expected,b""),
                            name+" composition mismatch "+repr(row))
                composition_cases+=1
        negatives=(
          ("fnc f(int x):bool{return x;} fnc main():int{return 1;}","condition_not_bool"),
          ("fnc f(int x):bool{return x eq 0;} fnc main():int{return f(0);}","Boolean is not an int"),
          ("fnc f(int x):bool{return x eq 0;} fnc main():int{return f(0)+1;}","Boolean is not an int"),
          ("fnc f(int x):bool{return x eq 0;} fnc main():int{return f(0) eq 1 ? 1 : 0;}","comparison operand type mismatch"),
          ("fnc main():int{return f(0);} fnc f(int x):bool{return x eq 0;}","Boolean target type mismatch"),
          ("fnc main():bool{return true;}","Boolean entry requires explicit int adapter"),
          ("fnc f(int x):bool{return x eq 0;} fnc g(int x):int{return x;} fnc main():int{return g(f(0));}","Boolean is not an int"),
          ("use namespace a; use namespace b; fnc main():int{return 1;}","FORGE_NAMESPACE_DUPLICATE"),
          ("fnc main():int{return 1;} use namespace a;","FORGE_NAMESPACE_HEADER_ORDER"),
        )
        for code,diagnostic in negatives:
            file=root/"negative.atto"
            file.write_text(code,encoding="utf8")
            r=execute([compiler,"emit-call-asm","x86_64",str(file)],root)
            require(r.returncode!=0 and not r.stdout and diagnostic.encode() in r.stderr,
                    "wrong negative diagnostic: "+repr(r.stderr))
    print("CORE_TYPES_FORGE_NATIVE=PASS functions=13 vectors="+str(cases)+" compositions="+str(composition_cases)+" negatives="+str(len(negatives)))

if __name__=="__main__":
    main()
