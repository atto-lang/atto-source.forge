#!/usr/bin/env python3
"""Source-to-native guest controls; optional real production CLI replay."""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
cxx = os.environ.get("CXX", "c++")
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-O1", "-fsanitize=undefined", "-fno-sanitize-recover=all",
         "-I" + str(root / "include")]
cli = os.environ.get("FORGE_SCALAR")
def stage(n, body, params="int v", name="fib"):
    return f"pub fnc main(): int {{ return {n} |> fnc {name}({params}): int {{ {body} }}; }}"
fib = "^ match v over on 0 -> 0, on 1 -> 1, on _ -> fib(v - 2) + fib(v - 1) end"
count = "^ match v over on 0 -> 0, on _ -> fib(v - 1) + 1 end"
positive = [
    ("fib0", stage(0,fib), 0, "0\n", ""),
    ("fib1", stage(1,fib), 0, "1\n", ""),
    ("fib10", stage(10,fib), 0, "55\n", ""),
    ("fib20", stage(20,fib), 0, "6765\n", ""),
    ("semi", stage(10,fib+";"), 0, "55\n", ""),
    ("boundary", stage(1024,count), 0, "1024\n", ""),
    ("quota", stage(1025,count), 75, "", "E_STACK\n"),
    ("max", stage(9223372036854775807,"return v;"), 0, "9223372036854775807\n", ""),
    ("min", stage(-9223372036854775808,"return v;"), 0, "-9223372036854775808\n", ""),
    ("overflow", stage(9223372036854775807,"return v+1;"), 70, "", "E_OVERFLOW\n"),
    ("divzero", stage(1,"return v/0;"), 71, "", "E_DIVZERO\n"),
    ("precedence", stage(2,"return v+3*4;"), 0, "14\n", ""),
    ("unicode", stage(42,"^ 值",params="int 值",name="递归"), 0, "42\n", ""),
    ("computed", stage("(2+3)*2",fib), 0, "55\n", ""),
    ("chain", "pub fnc main(): int {return 2*3 |> fnc f(int v): int {return v+1;} |> fnc f(int v): int {return v*6;};}", 0, "42\n", ""),
    ("upstream_first", stage("1/0","return 9223372036854775807+1;"), 71, "", "E_DIVZERO\n"),
    ("stage_first", "pub fnc main(): int {return 1 |> fnc f(int v): int {return 9223372036854775807+v;} |> fnc g(int v): int {return v/0;};}", 70, "", "E_OVERFLOW\n"),
    ("depth_reset", "pub fnc main(): int {return 1024 |> fnc fib(int v): int {"+count+"} |> fnc fib(int v): int {"+count+"};}", 0, "1024\n", ""),
]
negative = [
    stage(10,"return v+x;"), stage(10,"return fib(v,v);"),
    stage(10,"return v;",params="int fib"), stage(10,"return v;",params="int mut v"),
    stage(10,"^ match v over on 0 -> 0 end"),
    stage(10,"^ match v over on 0 -> 0, on 0 -> 1, on _ -> 2 end"),
    stage(10,"^ match v over on _ -> 0, on 0 -> 1 end"),
    stage(10,"return other(v);"), stage(10,"return v;").replace("|>","| >"),
    stage(10,"return v;").replace(": int { return v", " { return v"),
    stage(10,"return v;")+" fnc other(): int {return 1;}",
    stage("unknown+2","return v;"),
    "pub fnc main(): int {return 1 |> fnc f(int v): int {return v;} |> fnc g(int v): int {return f(v);};}",
    "pub fnc main(): int {return 1 |> fnc f(int v): int {return v;} |> ordinary;}",
    "pub fnc main(): int {return 1"+" |> fnc f(int v): int {return v;}"*17+";}",
    stage(10,"return v;").replace("}; }","}; return fib(2); }"),
    stage(10,"return "+"("*65+"v"+")"*65+";"),
]
def local(bindings, expr, body="return v;", name="f"):
    return f"pub fnc main(): int {{ {bindings} return {expr} |> fnc {name}(int v): int {{ {body} }}; }}"
positive += [
    ("local",local("val int x=6;","x*7"),0,"42\n",""),
    ("local_chain",local("val int x=6; val int y=x+1;","x*y"),0,"42\n",""),
    ("local_unused_error",local("val int bad=1/0;","42"),71,"","E_DIVZERO\n"),
    ("local_order",local("val int bad=9223372036854775807+1; val int later=1/0;","42"),70,"","E_OVERFLOW\n"),
    ("local_name_shadow",local("val int f=42;","f"),0,"42\n",""),
    ("local_param_shadow",local("val int v=42;","v-1","return v+1;"),0,"42\n",""),
    ("local_16",local("".join(f"val int x{i}={i};" for i in range(16)),"x15"),0,"15\n",""),
]
negative += [
    local("val int x=x;","x"),
    local("val int x=y; val int y=1;","x"),
    local("val int x=1; val int x=2;","x"),
    local("val int x=1;","x","return v+x;"),
    local("let int x=1;","x"),
    local("val bool x=true;","1"),
    local("val int x=1; x=2;","x"),
    local("".join(f"val int x{i}={i};" for i in range(17)),"x16"),
]
exact = """pub fnc main(): int {
    return 55 |> fnc fib(int v): int {
        ^ match v over
            on 0 -> 0,
            on 1 -> 1,
            on _ -> fib(v - 2) + fib(v - 1)
        end
    };
}
"""
assert subprocess.check_output(["git","hash-object","--stdin"],input=exact.encode()).decode().strip()=="ab60c8c4ad9726a526a4b0aa9ba76c0acca50b9d"
def run(args, **kw):
    return subprocess.run(args, text=True, capture_output=True, timeout=30, **kw)
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    emitter = tmp / "emitter"
    subprocess.run([cxx,*flags,str(root/"tests/closed_recursive_source_emit.cpp"),"-o",str(emitter)],check=True,timeout=60)
    for i, source in enumerate(negative):
        r = run([str(emitter)],input=source)
        assert (r.returncode,r.stdout,r.stderr)==(2,"",""),(i,r)
        if cli:
            inp, out = tmp/f"reject{i}.atto",tmp/f"reject{i}.cpp"
            inp.write_text(source)
            for args in [[cli,"--check",str(inp)],[cli,str(inp),"-o",str(out)]]:
                p=run(args)
                assert (p.returncode,p.stdout,p.stderr)==(2,"","E_NAMED_STAGE_PROFILE\n"),(i,p)
            assert not out.exists() and not out.is_symlink()
    print(f"PASS {len(negative)} source rejection cases",flush=True)
    for ident, source, status, stdout, stderr in positive+[("exact55",exact,None,None,None)]:
        r=run([str(emitter)],input=source)
        assert r.returncode==0 and not r.stderr,(ident,r)
        inp, out, binary = tmp/f"{ident}.atto",tmp/f"{ident}.cpp",tmp/f"{ident}.bin"
        inp.write_text(source)
        if cli:
            check=run([cli,"--check",str(inp)])
            assert (check.returncode,check.stdout,check.stderr)==(0,"",""),check
            p=run([cli,str(inp),"-o",str(out)])
            assert (p.returncode,p.stdout,p.stderr)==(0,"",""),p
            assert out.read_text()==r.stdout
            denied=tmp/f"{ident}-strict.cpp"
            strict=run([cli,"--verify=strict",str(inp),"-o",str(denied)])
            assert (strict.returncode,strict.stdout,strict.stderr)==(2,"","E_NAMED_STAGE_PROFILE\n"),strict
            assert not denied.exists() and not denied.is_symlink()
        else:
            out.write_text(r.stdout)
        subprocess.run([cxx,*flags,str(out),"-o",str(binary)],check=True,timeout=60)
        if status is not None:
            guest=run(["bash","-c",'ulimit -s 256; exec "$1"',"bash",str(binary)])
            assert (guest.returncode,guest.stdout,guest.stderr)==(status,stdout,stderr),(ident,guest)
        print(("COMPILE ONLY" if status is None else "PASS"),ident,cxx,flush=True)
