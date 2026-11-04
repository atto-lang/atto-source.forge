#!/usr/bin/env python3
"""Real Forge stage-0 checked IR + native linked executable corpus runner.

Emits local observations only; never claims authenticated five-backend parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

CASES=[{"id":"canonical-add","kind":"positive","path":"contracts/atto-scalar-v1/positive/canonical-add.atto","git_blob_sha1":"015f8b0f79aafa796a148d2adeff1574e8d6c491","expected_i64":"42"},{"id":"precedence","kind":"positive","path":"contracts/atto-scalar-v1/positive/precedence.atto","git_blob_sha1":"50414e5e95c8007f066696f55aa75309555b83e2","expected_i64":"14"},{"id":"parentheses","kind":"positive","path":"contracts/atto-scalar-v1/positive/parentheses.atto","git_blob_sha1":"9f508169c741486d5cec7510ac84fb761f342def","expected_i64":"20"},{"id":"signed-minimum","kind":"positive","path":"contracts/atto-scalar-v1/positive/signed-minimum.atto","git_blob_sha1":"450ebee951733d3f0b567413e70724155d26ce13","expected_i64":"-9223372036854775808"},{"id":"signed-max-minus-one","kind":"positive","path":"contracts/atto-scalar-v1/positive/signed-max-minus-one.atto","git_blob_sha1":"c400f440fde2fbc10514ab7cfd2ffe3d19729ee1","expected_i64":"9223372036854775806"},{"id":"type-first-readonly","kind":"positive","path":"contracts/atto-scalar-v1/positive/type-first-readonly.atto","git_blob_sha1":"75ff4afc98fc65a774611eb7686e893f3d28a6da","expected_i64":"42"},{"id":"typed-const-readonly","kind":"positive","path":"contracts/atto-scalar-v1/positive/typed-const-readonly.atto","git_blob_sha1":"0325018c54dd3534ddac76dc29724aa61e979683","expected_i64":"42"},{"id":"inferred-const-readonly","kind":"positive","path":"contracts/atto-scalar-v1/positive/inferred-const-readonly.atto","git_blob_sha1":"4fe471862d90ba57e135b93728bd44690ff2c894","expected_i64":"42"},{"id":"pure-call","kind":"positive","path":"contracts/atto-scalar-v1/positive/pure-call.atto","git_blob_sha1":"e5ab32f7d8f684d655a83a3df989f16c52090c05","expected_i64":"42"},{"id":"type-first-parameter","kind":"positive","path":"contracts/atto-scalar-v1/positive/type-first-parameter.atto","git_blob_sha1":"3d6c16ed04aa86f954461885ec77c8f10b47fb5c","expected_i64":"42"},{"id":"checked-add-overflow","kind":"negative","path":"contracts/atto-scalar-v1/negative/checked-add-overflow.atto","git_blob_sha1":"eed3113842125b1d2a295a11e3cc01a5c1e3da00","diagnostic_family":"checked-overflow"},{"id":"checked-sub-overflow","kind":"negative","path":"contracts/atto-scalar-v1/negative/checked-sub-overflow.atto","git_blob_sha1":"3f80e2eff35147984b040eb0a0215b439d20d09b","diagnostic_family":"checked-overflow"},{"id":"checked-mul-overflow","kind":"negative","path":"contracts/atto-scalar-v1/negative/checked-mul-overflow.atto","git_blob_sha1":"e608942c7e84fddb6ad7ca119a4a11d88aa9434b","diagnostic_family":"checked-overflow"},{"id":"type-first-reassignment","kind":"negative","path":"contracts/atto-scalar-v1/negative/type-first-reassignment.atto","git_blob_sha1":"b118f53e68565dcb9e6acd58deb8ee47dca67606","diagnostic_family":"immutable-binding"},{"id":"typed-const-reassignment","kind":"negative","path":"contracts/atto-scalar-v1/negative/typed-const-reassignment.atto","git_blob_sha1":"784ebd47a50211e6f00cb732cc74319ed1e58fac","diagnostic_family":"immutable-binding"},{"id":"eager-unused-overflow","kind":"negative","path":"contracts/atto-scalar-v1/negative/eager-unused-overflow.atto","git_blob_sha1":"ff0c520f2bc2efcc3daf032eb61fddc8f0c91d48","diagnostic_family":"checked-overflow"},{"id":"removed-modulo","kind":"negative","path":"contracts/atto-scalar-v1/negative/removed-modulo.atto","git_blob_sha1":"45d5794870e93074d9b11b90833a849e96aaa4c1","diagnostic_family":"removed-operator"},{"id":"removed-caret","kind":"negative","path":"contracts/atto-scalar-v1/negative/removed-caret.atto","git_blob_sha1":"6889c7e83c8d94b906fbfe78723caee9580da6aa","diagnostic_family":"removed-operator"},{"id":"removed-double-caret","kind":"negative","path":"contracts/atto-scalar-v1/negative/removed-double-caret.atto","git_blob_sha1":"7ee99948018e927cce74c9ad0edc1975e4251689","diagnostic_family":"removed-operator"}]
DRIVER = """#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
extern int64_t oresforge_entry(void);
int main(void) {
  if (printf("%" PRId64 "\\n", oresforge_entry()) < 0) return 1;
  return fflush(stdout) == 0 ? 0 : 1;
}
"""
ERRORS={
    "checked-add-overflow":b"i64 add overflow",
    "checked-sub-overflow":b"i64 sub overflow",
    "checked-mul-overflow":b"i64 mul overflow",
    "eager-unused-overflow":b"i64 add overflow",
    "type-first-reassignment":b"immutable local reassignment is forbidden",
    "typed-const-reassignment":b"immutable local reassignment is forbidden",
}

def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)

def invoke(argv, cwd):
    result=subprocess.run(argv,cwd=cwd,stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                          timeout=15,check=False)
    require(len(result.stdout)<=65536 and len(result.stderr)<=65536,
            "unbounded guest output")
    return result.returncode,result.stdout,result.stderr

def require_no_symlink(root,relative):
    parts=Path(relative).parts
    require(parts and all(x not in ("",".","..") for x in parts), "unsafe path")
    path=root
    for part in parts:
        path=path / part
        require(not path.is_symlink(), "symlinked source path")
    require(path.is_file(), "missing or nonregular source")
    return path

def main():
    opts=argparse.ArgumentParser()
    opts.add_argument("--compiler",required=True,type=Path)
    opts.add_argument("--cc",default="cc")
    args=opts.parse_args()
    require(sys.platform.startswith("linux"),"native execution requires Linux")
    arch={"x86_64":"x86_64","amd64":"x86_64",
          "aarch64":"arm64","arm64":"arm64"}.get(platform.machine().lower())
    require(arch is not None,"unsupported host architecture")
    require(not args.compiler.is_symlink(),"symlinked compiler forbidden")
    compiler=args.compiler.resolve(strict=True)
    require(compiler.is_file(),"compiler must be regular file")
    cc=shutil.which(args.cc)
    require(cc is not None,"host C compiler unavailable")
    root=Path(__file__).resolve().parents[1]
    manifest_path=require_no_symlink(root,"contracts/atto-scalar-v1/cases.json")
    raw=manifest_path.read_bytes()
    manifest=json.loads(raw.decode("utf-8"),object_pairs_hook=no_duplicate_keys)
    require(manifest.get("schema")=="attolang.scalar-core.v1" and
            manifest.get("language")=="AttoLang" and
            manifest.get("compiler_routes")==["java","llvm","wasm","net","forge"] and
            manifest.get("evidence_status")=="NOT_EXECUTED" and
            manifest.get("cases")==CASES,"incorrect or tampered common Atto corpus")
    require(raw== (json.dumps(manifest,indent=2,ensure_ascii=False)+"\n").encode("utf-8"),
            "noncanonical scalar JSON manifest")
    positives=negatives=0
    observations=[]
    with tempfile.TemporaryDirectory(prefix="atto-forge-scalar-19-") as temp:
        work=Path(temp)
        driver=work/"driver.c"
        driver.write_text(DRIVER,encoding="ascii")
        assembly=work/"entry.s"
        binary=work/"guest"
        for case in CASES:
            source=require_no_symlink(root,case["path"])
            data=source.read_bytes()
            blob=hashlib.sha1(b"blob "+str(len(data)).encode("ascii")+b"\0"+data).hexdigest()
            require(blob==case["git_blob_sha1"] and 0<len(data)<=8192,
                    "fixture source identity mismatch: "+case["id"])
            # The compiler must consume the exact bytes that were hashed,
            # not a mutable original checkout path after hash verification.
            staged=work / (case["id"]+".atto")
            with staged.open("xb") as pinned:
                pinned.write(data)
            staged.chmod(0o400)
            require(staged.read_bytes()==data and not staged.is_symlink(),
                    "staged Forge source differs from reviewed fixture")
            if case["kind"]=="positive":
                expected=case["expected_i64"].encode("ascii")+b"\n"
                status,out,err=invoke([str(compiler),"run",str(staged)],work)
                require((status,out,err)==(0,expected,b""),
                        "checked IR mismatch: "+case["id"])
                status,out,err=invoke([str(compiler),"ir",str(staged)],work)
                require(status==0 and out and err==b"",
                        "IR verification mismatch: "+case["id"])
                status,asm,err=invoke([str(compiler),"emit-asm",arch,str(staged)],work)
                require(status==0 and err==b"" and
                        asm.startswith(b".text\n.globl oresforge_entry\noresforge_entry:\n") and
                        asm.rstrip().endswith(b"ret") and len(asm)<=1024,
                        "native assembly emission mismatch: "+case["id"])
                assembly.write_bytes(asm)
                # A failed/misconfigured assembler may not borrow a previous
                # case's output and report a false new native guest.
                binary.unlink(missing_ok=True)
                status,out,err=invoke([cc,"-std=c11","-Wall","-Wextra",
                                       "-Werror",str(driver),str(assembly),
                                       "-o",str(binary)],work)
                require(status==0, "native assembly/link failed: "+case["id"]+
                        " "+err.decode("utf-8","replace")[:1000])
                require(binary.is_file() and not binary.is_symlink() and
                        0 < binary.stat().st_size <= 32*1024*1024,
                        "assembler did not publish a fresh bounded native binary: "+case["id"])
                status,out,err=invoke([str(binary)],work)
                require((status,out,err)==(0,expected,b""),
                        "native linked guest mismatch: "+case["id"])
                positives+=1
                observations.append({"id":case["id"],"observed_i64":case["expected_i64"],
                    "native_sha256":hashlib.sha256(binary.read_bytes()).hexdigest()})
            else:
                for mode in ("run","ir","emit-asm"):
                    before={p.name for p in work.iterdir()}
                    cmd=[str(compiler),mode]+([arch] if mode=="emit-asm" else [])
                    status,out,err=invoke(cmd+[str(staged)],work)
                    require({p.name for p in work.iterdir()} == before,
                            "negative compiler created an artifact: "+case["id"]+"/"+mode)
                    require(status!=0 and out==b"" and err.startswith(b"oresforge: "),
                            "negative accepted or leaked success output: "+case["id"]+"/"+mode)
                    diag=ERRORS.get(case["id"])
                    if diag is not None:
                        require(diag in err,"unexpected negative diagnostic: "+case["id"]+"/"+mode)
                negatives+=1
        require(positives==10 and negatives==9,"incomplete 19-case execution")
    print(json.dumps({"kind":"forge-stage0-atto-scalar-v1-local-observations",
        "compiler_sha256":hashlib.sha256(compiler.read_bytes()).hexdigest(),
        "manifest_sha256":hashlib.sha256(raw).hexdigest(),
        "positives_executed_in_ir_and_native":positives,
        "negatives_rejected_in_three_modes":negatives,
        "runtime_scope":"constant-folded signed-i64 single-file stage-0",
        "independent_five_backend_parity_certified":False,
        "observations":observations},sort_keys=True))
    return 0

def no_duplicate_keys(pairs):
    out={}
    for key,value in pairs:
        require(key not in out,"duplicate JSON key")
        out[key]=value
    return out

if __name__=="__main__":
    try:sys.exit(main())
    except (OSError,RuntimeError,UnicodeError,ValueError,subprocess.TimeoutExpired) as exc:
        print("ATTO_FORGE_SCALAR_V1_DENIED: "+str(exc),file=sys.stderr)
        sys.exit(1)
