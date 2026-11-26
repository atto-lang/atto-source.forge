//! Actual Forge compiler -> generated C++ -> native binary. Bounded profile only.
//! Shared source fixtures copied exactly from conformance blob 193249523cf18ef725f4900c30f3925c5bf8ce4b.
use std::{fs, path::{Path,PathBuf}, process::{Command,Output}, time::{SystemTime,UNIX_EPOCH}};
struct Temp(PathBuf);
impl Drop for Temp { fn drop(&mut self) { let _=fs::remove_dir_all(&self.0); } }
fn compile(forge:&Path,src:&Path,out:&Path)->Output {
    Command::new(forge).arg(src).arg("-o").arg(out).output().expect("run Forge")
}
fn main(){
    let args:Vec<_>=std::env::args_os().collect();
    assert_eq!(args.len(),5,"usage: probe FORGE CXX INCLUDE OPT");
    let forge=PathBuf::from(&args[1]).canonicalize().unwrap();
    let cxx=&args[2]; let include=PathBuf::from(&args[3]).canonicalize().unwrap();
    let opt=&args[4];
    assert!(opt=="-O0"||opt=="-O2");
    let dir=Temp(std::env::temp_dir().join(format!("atto-forge-tuple-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos())));
    fs::create_dir(&dir.0).unwrap();
    #[cfg(unix)] { use std::os::unix::fs::PermissionsExt; fs::set_permissions(&dir.0,fs::Permissions::from_mode(0o700)).unwrap(); }
    let positive=[
        ("flat-three", "pub fnc main(): int { return (5, 3, 5) |> |(a, b, c)| -> a + b + c; }\n", 13),
        ("order-weighted", "pub fnc main(): int { return (2, 7, 11) |> |(a, b, c)| -> 2*a + 3*b + 4*c; }\n", 69),
        ("discard-middle", "pub fnc main(): int { return (5, 99, 8) |> |(a, _, c)| -> a + c; }\n", 13),
        ("discard-first", "pub fnc main(): int { return (99, 8) |> |(_, b)| -> b + 3; }\n", 11),
        ("discard-last", "pub fnc main(): int { return (6, 99) |> |(a, _)| -> a + 5; }\n", 11),
        ("discard-all", "pub fnc main(): int { return (5, 8) |> |(_, _)| -> 19; }\n", 19),
        ("singleton", "pub fnc main(): int { return (9,) |> |(a,)| -> a; }\n", 9),
        ("empty", "pub fnc main(): int { return () |> |()| -> 7; }\n", 7),
        ("typed-leaves", "pub fnc main(): int { return (5, 8) |> |(a: int, b: int)| -> a + b; }\n", 13),
        ("parenthesized-stage", "pub fnc main(): int { return ((5, 8) |> |(a, b)| -> a + b) + 1; }\n", 14),
        ("nested-scalar-stage", "pub fnc main(): int { return (5, 8) |> |(a, b)| -> ((a, 2) |> |(x, y)| -> x*y) + b; }\n", 18),
        ("signed-values", "pub fnc main(): int { return (-5, 8, 12) |> |(a, b, c)| -> a + b + c; }\n", 15),
        ("maximum-arity", "pub fnc main(): int { return (1, 2, 3, 4, 5, 6, 7, 8) |> |(a,b,c,d,e,f,g,h)| -> a+b+c+d+e+f+g+h; }\n", 36),
        ("shadow", "pub fnc main(): int { val a=19; return ((5,8) |> |(a,b)| -> a+b)+a; }\n",32),
        ("continuation","fnc double(int v): int { return v*2; } pub fnc main(): int { return (5,8) |> |(a,b)| -> a+b |> double; }\n",26),
    ];
    for (name,source,expected) in positive {
        let src=dir.0.join(format!("{name}.atto"));let cpp=dir.0.join(format!("{name}.cpp"));let exe=dir.0.join(name);
        fs::write(&src,source).unwrap(); let r=compile(&forge,&src,&cpp);
        assert!(r.status.success(),"{name}: {}",String::from_utf8_lossy(&r.stderr));
        assert!(r.stdout.is_empty()&&r.stderr.is_empty());
        assert_eq!(fs::read_to_string(&src).unwrap(),source);
        let built=Command::new(cxx).arg("-std=c++20").arg(opt).args(["-Wall","-Wextra","-Werror","-pedantic"]).arg("-I").arg(&include).arg(&cpp).arg("-o").arg(&exe).output().unwrap();
        assert!(built.status.success(),"{name}: {}",String::from_utf8_lossy(&built.stderr));
        let guest=Command::new(&exe).output().unwrap();
        assert_eq!(guest.status.code(),Some(0),"{name}");
        assert_eq!(guest.stdout,format!("{expected}\n").into_bytes(),"{name}");
        assert!(guest.stderr.is_empty(),"{name}");
    }
    let negative=[
        ("scalar","1 |> |(a,b)| -> a+b","E_PATTERN_SHAPE"),
        ("arity","(1,2,3) |> |(a,b)| -> a+b","E_ARITY"),
        ("duplicate","(1,2) |> |(a,a)| -> a","E_DUP_PARAM"),
        ("singleton","(1,) |> |(a)| -> a","E_PATTERN"),
        ("discard","(1,2) |> |(a,_)| -> _","E_SYMBOL"),
        ("typed-discard","(1,2) |> |(_:int,b)| -> b","E_PATTERN"),
        ("nested-tuple","((1,2),3) |> |(a,b)| -> b","E_UNSUPPORTED_TUPLE_VALUE"),
        ("tuple-escape","(1,2)","E_UNSUPPORTED_TUPLE_VALUE"),
        ("bad-type","(1,2) |> |(a:bool,b)| -> b","E_PARSE"),
    ];
    for (name,expr,code) in negative {
        let src=dir.0.join(format!("{name}.atto"));let cpp=dir.0.join(format!("{name}.cpp"));
        fs::write(&src,format!("pub fnc main(): int {{ return {expr}; }}\n")).unwrap();
        let r=compile(&forge,&src,&cpp);
        assert_eq!(r.status.code(),Some(2),"{name}");assert!(r.stdout.is_empty());assert_eq!(String::from_utf8_lossy(&r.stderr).trim(),code,"{name}");
        assert!(!cpp.exists()&&!cpp.with_extension("cpp.part").exists(),"{name}: artifact");
    }
    for (name,expr) in [("discard-first-overflow","(9223372036854775807+1,8) |> |(_,b)| -> b"),("discard-last-overflow","(5,9223372036854775807+1) |> |(a,_)| -> a")] {
        let src=dir.0.join(format!("{name}.atto"));let cpp=dir.0.join(format!("{name}.cpp"));let exe=dir.0.join(name);
        fs::write(&src,format!("pub fnc main(): int {{ return {expr}; }}\n")).unwrap();
        let r=compile(&forge,&src,&cpp);assert!(r.status.success(),"{name}: {:?}",r);
        assert!(Command::new(cxx).arg("-std=c++20").arg(opt).arg("-I").arg(&include).arg(&cpp).arg("-o").arg(&exe).status().unwrap().success());
        let g=Command::new(exe).output().unwrap();assert_eq!(g.status.code(),Some(70));assert!(g.stdout.is_empty());assert_eq!(g.stderr,b"E_OVERFLOW\n");
    }
    println!("FORGE_TUPLE_NATIVE_PASS 13 shared + 2 additional positive + 9 rejection + 2 overflow cases");
}
