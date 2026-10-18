#!/usr/bin/env python3
"""Execute the reviewed source-i64 corpus via REAL OresForge IR interpreter CLI.

This checks OresForge source parsing and IR interpretation, NOT AOT/native parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import tempfile
import time

BLOB = "bbc319c19ecd007109d33592e210091eb267e5a2"
BYTES_SHA256 = "39bddbe558ad1c336271f91e6403c4555f2f16a7ed33a3355ee897721b6ab02a"
CASES = Path(__file__).resolve().parents[1] / "contracts/v1/source-i64/portable-i64.v1.json"
IDS = ("precedence","grouping","signed-multiplication","subtraction",
       "negative-result","max-minus-one","signed-minimum")

def require(ok, detail):
    if not ok: raise RuntimeError(detail)

def load_cases():
    raw = CASES.read_bytes()
    digest = hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
    require(0<len(raw)<=8192 and digest==BLOB and
            hashlib.sha256(raw).hexdigest()==BYTES_SHA256,
            "unreviewed source-i64 corpus")
    def nodups(pairs):
        result={}
        for k,v in pairs:
            require(k not in result,"duplicate JSON field")
            result[k]=v
        return result
    parsed=json.loads(raw.decode("utf-8"),object_pairs_hook=nodups,
                      parse_constant=lambda v:require(False,"non-JSON constant "+v))
    require(type(parsed) is dict and set(parsed)=={"schema_version","status","scope","cases"},"bad corpus schema")
    require(parsed["schema_version"]=="1.0.0" and parsed["status"]=="reference-only-not-compiled-execution","corpus status changed")
    cases=parsed["cases"]
    require(type(cases) is list and len(cases)==7,"missing or extra cases")
    for case,expected_id in zip(cases,IDS):
        require(type(case) is dict and set(case)=={"id","expression","expected"},"bad case shape")
        require(case["id"]==expected_id,"case ID or order changed")
        require(type(case["expression"]) is str and re.fullmatch(r"[0-9+*() \-]{1,256}",case["expression"]),"unsafe expression")
        require(type(case["expected"]) is str and re.fullmatch(r"-?(?:0|[1-9][0-9]*)",case["expected"]),"bad expectation")
    return cases

def execute(argv,cwd,timeout=10,max_output=8192):
    begin=time.monotonic()
    chunks=[bytearray(),bytearray()]
    timeout_hit=overflow=False
    p=subprocess.Popen(argv,cwd=cwd,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                       start_new_session=(os.name=="posix"))
    try:
        with selectors.DefaultSelector() as events:
            events.register(p.stdout,selectors.EVENT_READ,0)
            events.register(p.stderr,selectors.EVENT_READ,1)
            while events.get_map():
                remaining=timeout-(time.monotonic()-begin)
                if remaining<=0:timeout_hit=True;break
                for key,_ in events.select(min(remaining,0.2)):
                    data=os.read(key.fileobj.fileno(),4096)
                    if not data:
                        events.unregister(key.fileobj);key.fileobj.close();continue
                    out=chunks[key.data]
                    if len(out)+len(data)>max_output:overflow=True;break
                    out.extend(data)
                if overflow:break
        if not (overflow or timeout_hit):
            try:p.wait(timeout=max(0.001,timeout-(time.monotonic()-begin)))
            except subprocess.TimeoutExpired:timeout_hit=True
    finally:
        if timeout_hit or overflow or p.poll() is None:
            if os.name=="posix":
                try:os.killpg(p.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            else:p.kill()
        try:p.wait(timeout=3)
        except subprocess.TimeoutExpired:p.kill();p.wait(timeout=3)
        for stream in (p.stdout,p.stderr):
            if not stream.closed:stream.close()
    require(not timeout_hit and not overflow,"process timed out or produced oversized output")
    return p.returncode,bytes(chunks[0]).decode("utf-8","strict"),bytes(chunks[1]).decode("utf-8","strict")

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",type=Path,required=True)
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    require(not args.compiler.is_symlink(),"symlinked compiler rejected")
    compiler=args.compiler.resolve(strict=True)
    require(compiler.is_file(),"compiler not a file")
    sha=hashlib.sha256(compiler.read_bytes()).hexdigest()
    cases=load_cases()
    records=[]
    negatives=("3/2","9223372036854775808","9223372036854775807+1",
               "-9223372036854775808-1","3037000500*3037000500")
    with tempfile.TemporaryDirectory(prefix="oresforge-source-i64-") as td:
        source=Path(td)/"probe.ores"
        for case in cases:
            program="pub fnc main():int{return "+case["expression"]+";}\n"
            source.write_text(program,encoding="utf-8")
            code,out,err=execute([str(compiler),"run",str(source)],td)
            require(code==0 and err=="" and out==case["expected"]+"\n",
                    "real interpreter mismatch: "+case["id"]+" "+repr((code,out,err)))
            records.append({"id":case["id"],"observed":out.strip(),
                            "source_sha256":hashlib.sha256(program.encode()).hexdigest()})
        for expr in negatives:
            source.write_text("pub fnc main():int{return "+expr+";}\n",encoding="utf-8")
            code,out,err=execute([str(compiler),"run",str(source)],td)
            require(code==1 and out=="" and err.startswith("oresforge: "),
                    "compiler failed to reject unsupported or overflowed source: "+expr+" "+repr((code,out,err)))
    receipt={"kind":"oreslang-source-i64-executed-ir-interpreter",
             "compiler_sha256":sha,"fixture_git_blob":BLOB,
             "fixture_sha256":BYTES_SHA256,
             "executed_positive":len(records),"executed_negative":len(negatives),
             "skipped":0,"native_aot_attested":False,"observed":records}
    if args.report:
        args.report.write_text(json.dumps(receipt,sort_keys=True,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(receipt,sort_keys=True))
    return 0

if __name__=="__main__":
    try:sys.exit(main())
    except (OSError,RuntimeError,UnicodeError,ValueError,subprocess.SubprocessError) as exc:
        print("source-i64 executable conformance DENIED: "+str(exc),file=sys.stderr)
        sys.exit(1)
