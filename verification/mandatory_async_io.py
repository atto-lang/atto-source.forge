import hashlib, json, pathlib, subprocess, sys, tempfile
compiler = sys.argv[1]
raw=pathlib.Path(__file__).with_name("mandatory-async-io.json").read_bytes()
assert hashlib.sha256(raw).hexdigest()=="e3b5caa3267683e7a1ec83f04d671333455746ceb87188dba6ec7ec1e75ec042"
cases = json.loads(raw)["cases"]
with tempfile.TemporaryDirectory() as directory:
    source = pathlib.Path(directory) / "main.ores"
    for case in cases:
        source.write_text(case["source"])
        for mode in [["run"], ["ir"], ["emit-asm", "x86_64"], ["emit-asm", "arm64"]]:
            result = subprocess.run([compiler, *mode, str(source)], capture_output=True, text=True, timeout=5)
            if case["expect"] == "accept":
                assert result.returncode == 0 and result.stdout.strip(), (case["id"], mode, result)
            else:
                assert result.returncode == 1 and not result.stdout and "oresforge:" in result.stderr, (case["id"], mode, result)
    source.write_text("pub fnc main():int{return 42;}")
    for option in ["--core-io=legacy-blocking", "--core-io=off", "--allow-blocking-io", "--core-io=native-async-only"]:
        result = subprocess.run([compiler, "run", str(source), option], capture_output=True, text=True, timeout=5)
        assert result.returncode == 2 and not result.stdout, (option, result)
print(f"{len(cases)} mandatory async I/O cases checked across four Forge execution/emission paths; no policy selector")
